// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and
// src/graphics/guest_gpu/pm4.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/runtime.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <new>
#include <vector>

#include "core/memory/guest_memory.h"

namespace kajps5::gpu {
namespace {

bool HasValidGraphicsPreflight(
    const vulkan::VulkanTranslatedDrawRequest& request) {
  const auto& viewport = request.viewport;
  const auto mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  const bool topology_valid = request.topology == vulkan::VulkanGraphicsTopology::kTriangleList ||
      request.topology == vulkan::VulkanGraphicsTopology::kTriangleStrip ||
      request.topology == vulkan::VulkanGraphicsTopology::kLineList ||
      request.topology == vulkan::VulkanGraphicsTopology::kLineStrip ||
      request.topology == vulkan::VulkanGraphicsTopology::kPointList;
  const bool cull_valid = request.cull_mode == vulkan::VulkanGraphicsCullMode::kNone ||
      request.cull_mode == vulkan::VulkanGraphicsCullMode::kFront ||
      request.cull_mode == vulkan::VulkanGraphicsCullMode::kBack;
  const bool front_face_valid = request.front_face ==
      vulkan::VulkanGraphicsFrontFace::kCounterClockwise ||
      request.front_face == vulkan::VulkanGraphicsFrontFace::kClockwise;
  const bool blend_valid = request.blend.source_color != VK_BLEND_FACTOR_MAX_ENUM &&
      request.blend.destination_color != VK_BLEND_FACTOR_MAX_ENUM &&
      request.blend.color_op != VK_BLEND_OP_MAX_ENUM &&
      request.blend.source_alpha != VK_BLEND_FACTOR_MAX_ENUM &&
      request.blend.destination_alpha != VK_BLEND_FACTOR_MAX_ENUM &&
      request.blend.alpha_op != VK_BLEND_OP_MAX_ENUM &&
      (request.blend.write_mask & ~mask) == 0;
  return request.vertex != nullptr && request.pixel != nullptr &&
         request.vertex->program.stage == ShaderType::Vertex &&
         request.pixel->program.stage == ShaderType::Pixel &&
         !request.vertex->spirv.empty() && !request.pixel->spirv.empty() &&
         request.vertex_count != 0 &&
         request.instance_count != 0 && request.timeout_ns != 0 &&
         request.timeout_ns != std::numeric_limits<std::uint64_t>::max() &&
         std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
         std::isfinite(viewport.width) && std::isfinite(viewport.height) &&
         std::isfinite(viewport.min_depth) && std::isfinite(viewport.max_depth) &&
         viewport.width > 0.0f && viewport.height > 0.0f &&
         viewport.min_depth >= 0.0f && viewport.max_depth <= 1.0f &&
         viewport.min_depth <= viewport.max_depth &&
         viewport.scissor.extent.width != 0 && viewport.scissor.extent.height != 0 &&
         viewport.scissor.offset.x >= 0 && viewport.scissor.offset.y >= 0 &&
         topology_valid && cull_valid && front_face_valid && blend_valid;
}

std::optional<vulkan::VulkanGraphicsTopology>
ActionTopology(std::uint32_t primitive_type) noexcept {
  switch (primitive_type) {
  case 1:
    return vulkan::VulkanGraphicsTopology::kPointList;
  case 2:
    return vulkan::VulkanGraphicsTopology::kLineList;
  case 3:
    return vulkan::VulkanGraphicsTopology::kLineStrip;
  case 4:
    return vulkan::VulkanGraphicsTopology::kTriangleList;
  case 6:
    return vulkan::VulkanGraphicsTopology::kTriangleStrip;
  default:
    return std::nullopt;
  }
}

std::optional<VkCompareOp> ActionDepthCompare(std::uint32_t compare) noexcept {
  switch (compare) {
  case 0:
    return VK_COMPARE_OP_NEVER;
  case 1:
    return VK_COMPARE_OP_LESS;
  case 2:
    return VK_COMPARE_OP_EQUAL;
  case 3:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case 4:
    return VK_COMPARE_OP_GREATER;
  case 5:
    return VK_COMPARE_OP_NOT_EQUAL;
  case 6:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case 7:
    return VK_COMPARE_OP_ALWAYS;
  default:
    return std::nullopt;
  }
}

std::optional<VkBlendFactor> ActionBlendFactor(std::uint32_t factor) noexcept {
  // CB_BLEND*_CONTROL uses the hardware encoding documented in Kyty's pm4.h.
  // Keep the first bridge subset deliberately bounded to factors with a direct
  // Vulkan equivalent; dual-source factors are not silently approximated.
  switch (factor) {
  case 0:
    return VK_BLEND_FACTOR_ZERO;
  case 1:
    return VK_BLEND_FACTOR_ONE;
  case 2:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case 3:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case 4:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case 5:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case 6:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case 7:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case 8:
    return VK_BLEND_FACTOR_DST_COLOR;
  case 9:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case 10:
    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  default:
    return std::nullopt;
  }
}

std::optional<VkBlendOp> ActionBlendOp(std::uint32_t operation) noexcept {
  switch (operation) {
  case 0:
    return VK_BLEND_OP_ADD;
  case 1:
    return VK_BLEND_OP_SUBTRACT;
  case 2:
    return VK_BLEND_OP_REVERSE_SUBTRACT;
  case 3:
    return VK_BLEND_OP_MIN;
  case 4:
    return VK_BLEND_OP_MAX;
  default:
    return std::nullopt;
  }
}

std::optional<vulkan::VulkanTranslatedDrawRequest>
MakeActionDrawRequest(const GpuRenderSnapshot& state,
                      const shader::recompiler::CompileResult& vertex,
                      const shader::recompiler::CompileResult& pixel) {
  const auto topology = ActionTopology(state.primitive_type);
  const auto depth_compare = ActionDepthCompare(state.depth_compare);
  const auto source_color = ActionBlendFactor(state.blend_control & 0x1fU);
  const auto color_op = ActionBlendOp((state.blend_control >> 5U) & 7U);
  const auto destination_color =
      ActionBlendFactor((state.blend_control >> 8U) & 0x1fU);
  const auto source_alpha =
      ActionBlendFactor((state.blend_control >> 16U) & 0x1fU);
  const auto alpha_op = ActionBlendOp((state.blend_control >> 21U) & 7U);
  const auto destination_alpha =
      ActionBlendFactor((state.blend_control >> 24U) & 0x1fU);
  if (!topology.has_value() || !depth_compare.has_value() ||
      !source_color.has_value() || !color_op.has_value() ||
      !destination_color.has_value() || !source_alpha.has_value() ||
      !alpha_op.has_value() || !destination_alpha.has_value()) {
    return std::nullopt;
  }
  const float xscale = std::bit_cast<float>(state.viewport_x_scale_bits);
  const float xoffset = std::bit_cast<float>(state.viewport_x_offset_bits);
  const float yscale = std::bit_cast<float>(state.viewport_y_scale_bits);
  const float yoffset = std::bit_cast<float>(state.viewport_y_offset_bits);
  const float zmin = std::bit_cast<float>(state.viewport_z_min_bits);
  const float zmax = std::bit_cast<float>(state.viewport_z_max_bits);
  if (!std::isfinite(xscale) || !std::isfinite(xoffset) ||
      !std::isfinite(yscale) || !std::isfinite(yoffset) ||
      !std::isfinite(zmin) || !std::isfinite(zmax) || xscale <= 0.0f ||
      yscale <= 0.0f || zmin < 0.0f || zmax > 1.0f || zmin > zmax) {
    return std::nullopt;
  }
  vulkan::VulkanTranslatedDrawRequest request;
  request.vertex = &vertex;
  request.pixel = &pixel;
  request.color_target = {state.color_base,
                          state.color_format,
                          state.color_width,
                          state.color_height,
                          1,
                          1,
                          1,
                          0,
                          0,
                          Prospero::ImageType::kColor2D,
                          Prospero::TileMode::kLinear,
                          false};
  request.color_target.row_pitch_bytes = state.color_row_pitch_bytes;
  request.color_target.slice_pitch_bytes =
      state.color_row_pitch_bytes * state.color_height;
  request.topology = *topology;
  request.cull_mode =
      state.cull_mode == 0   ? vulkan::VulkanGraphicsCullMode::kNone
      : state.cull_mode == 1 ? vulkan::VulkanGraphicsCullMode::kFront
                             : vulkan::VulkanGraphicsCullMode::kBack;
  request.front_face = state.front_face_clockwise
                           ? vulkan::VulkanGraphicsFrontFace::kClockwise
                           : vulkan::VulkanGraphicsFrontFace::kCounterClockwise;
  request.blend.enabled = state.blend_enable;
  request.blend.source_color = *source_color;
  request.blend.destination_color = *destination_color;
  request.blend.color_op = *color_op;
  request.blend.source_alpha = *source_alpha;
  request.blend.destination_alpha = *destination_alpha;
  request.blend.alpha_op = *alpha_op;
  request.blend.write_mask = 0;
  if ((state.color_write_mask & 1U) != 0)
    request.blend.write_mask |= VK_COLOR_COMPONENT_R_BIT;
  if ((state.color_write_mask & 2U) != 0)
    request.blend.write_mask |= VK_COLOR_COMPONENT_G_BIT;
  if ((state.color_write_mask & 4U) != 0)
    request.blend.write_mask |= VK_COLOR_COMPONENT_B_BIT;
  if ((state.color_write_mask & 8U) != 0)
    request.blend.write_mask |= VK_COLOR_COMPONENT_A_BIT;
  request.viewport = {
      xoffset - xscale,
      yoffset - yscale,
      xscale * 2.0f,
      yscale * 2.0f,
      zmin,
      zmax,
      {{state.scissor_left, state.scissor_top},
       {static_cast<std::uint32_t>(state.scissor_right - state.scissor_left),
        static_cast<std::uint32_t>(state.scissor_bottom - state.scissor_top)}}};
  request.vertex_count = state.vertex_count;
  request.instance_count = state.instance_count;
  if (state.depth_enabled) {
    request.depth_stencil_target = {state.depth_base,
                                    Prospero::DepthFormat::kZ32F,
                                    Prospero::StencilFormat::kInvalid,
                                    state.depth_width,
                                    state.depth_height,
                                    state.depth_row_pitch_bytes,
                                    VK_SAMPLE_COUNT_1_BIT};
    request.depth_stencil.depth_test_enable = true;
    request.depth_stencil.depth_write_enable = state.depth_write_enabled;
    request.depth_stencil.depth_compare_op = *depth_compare;
  }
  return request;
}

std::optional<GuestImageLayout> PlannedDepthLayout(
    const vulkan::VulkanGraphicsDepthStencilTarget& target) {
  GuestImageLayoutInput input{};
  input.guest_address = target.guest_address;
  input.format = static_cast<std::uint32_t>(Prospero::BufferFormat::k32Float);
  input.width = target.width;
  input.height = target.height;
  input.depth = 1;
  input.image_type = Prospero::ImageType::kColor2D;
  input.tile_mode = Prospero::TileMode::kLinear;
  input.tightly_packed = target.row_pitch_bytes == 0;
  input.row_pitch_bytes = target.row_pitch_bytes;
  if (target.row_pitch_bytes != 0) {
    if (target.row_pitch_bytes > std::numeric_limits<std::uint64_t>::max() /
                                     target.height) return std::nullopt;
    input.slice_pitch_bytes = target.row_pitch_bytes * target.height;
  }
  auto layout = CalculateGuestImageLayout(input);
  return layout.ok() ? std::optional<GuestImageLayout>{std::move(layout)}
                     : std::nullopt;
}

constexpr std::uint32_t kPm4Type3 = 0xc0000000U;
constexpr std::uint32_t kPm4LengthMask = 0x3fffU;
constexpr std::uint32_t kPm4RegisterMask = 0x3fU;
constexpr std::uint32_t kPm4NopOpcode = 0x10U;
constexpr std::uint32_t kPm4SetBaseOpcode = 0x11U;
constexpr std::uint32_t kPm4IndexBufferSizeOpcode = 0x13U;
constexpr std::uint32_t kPm4DispatchDirectOpcode = 0x15U;
constexpr std::uint32_t kPm4DispatchIndirectOpcode = 0x16U;
constexpr std::uint32_t kPm4SetPredicationOpcode = 0x20U;
constexpr std::uint32_t kPm4IndexBaseOpcode = 0x26U;
constexpr std::uint32_t kPm4DrawIndexOpcode = 0x27U;
constexpr std::uint32_t kPm4DrawIndexAutoOpcode = 0x2dU;
constexpr std::uint32_t kPm4NumInstancesOpcode = 0x2fU;
constexpr std::uint32_t kPm4DrawIndexOffsetOpcode = 0x35U;
constexpr std::uint32_t kPm4IndirectBufferOpcode = 0x3fU;
constexpr std::uint32_t kPm4EventWriteOpcode = 0x46U;
constexpr std::uint32_t kPm4RewindOpcode = 0x59U;
constexpr std::uint32_t kPm4SetContextRegisterOpcode = 0x69U;
constexpr std::uint32_t kPm4SetShRegisterOpcode = 0x76U;
constexpr std::uint32_t kPm4SetUconfigRegisterOpcode = 0x79U;
constexpr std::uint32_t kPm4SetUconfigRegisterIndexOpcode = 0x7aU;
constexpr std::uint32_t kPm4GetLodStatsOpcode = 0x8eU;
constexpr std::uint32_t kPm4WaitMemory32Register = 0x0aU;
constexpr std::uint32_t kPm4WaitMemory64Register = 0x16U;
constexpr std::uint32_t kPm4WriteDataRegister = 0x15U;
constexpr std::uint32_t kPm4ReleaseMemoryRegister = 0x18U;
constexpr std::uint32_t kVgtIndexTypeRegister = 0x243U;
constexpr std::uint32_t kDirectDispatchModifierMask = 0xa038U;
constexpr std::uint32_t kDirectDispatchRequiredBits = 0x41U;

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

std::uint32_t MakePm4Header(std::uint32_t dword_count,
                           std::uint32_t opcode,
                           std::uint32_t packet_register) noexcept {
  return kPm4Type3 |
         (((dword_count - 2U) & kPm4LengthMask) << 16U) |
         ((opcode & 0xffU) << 8U) |
         ((packet_register & kPm4RegisterMask) << 2U);
}

std::uint32_t DrawIndexInitiator(std::uint64_t modifier) noexcept {
  return (modifier & (1ULL << 32U)) != 0
             ? 0
             : (static_cast<std::uint32_t>(modifier) >> 3U) & 0x20U;
}

std::uint32_t WaitPoll(std::uint32_t poll_cycles) noexcept {
  return std::min(poll_cycles >> 4U, 0xffffU);
}

std::uint32_t Wait32Control(std::uint32_t compare_function,
                            std::uint32_t operation,
                            std::uint32_t cache_policy) noexcept {
  return 0x10U | (compare_function & 0x7U) |
         ((operation & 0x3U) << 8U) | ((operation & 0xcU) << 4U) |
         ((cache_policy & 0x3U) << 25U);
}

std::uint32_t Wait64Control(std::uint32_t compare_function,
                            std::uint32_t operation,
                            std::uint32_t cache_policy) noexcept {
  return 0x10U | (compare_function & 0x7U) |
         ((operation & 0x1U) << 8U) | ((operation & 0x6U) << 5U) |
         ((cache_policy & 0x3U) << 25U);
}

std::uint32_t Read32(std::span<const std::byte> bytes) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

}  // namespace

GpuRuntime::GpuRuntime(memory::GuestMemory& memory,
                       GpuSubmissionSink* submission_sink,
                       kernel::EventQueueService* event_queues)
    : memory_(memory),
      shader_runtime_(memory_),
      resource_coherence_(GpuResourceCoherence::Create(memory_)),
      submission_queue_(*this), submission_history_(4096),
      vulkan_action_bridge_(*this, submission_sink != nullptr
                                       ? *submission_sink
                                       : submission_history_),
      event_effects_(event_queues, vulkan_action_bridge_),
      submission_effects_(memory, event_effects_),
      submission_sink_(&submission_effects_) {}

void GpuRuntime::EnableVulkanActionExecution(bool enabled) noexcept {
  vulkan_action_bridge_.Enable(enabled);
}

bool GpuRuntime::vulkan_action_execution_enabled() const noexcept {
  return vulkan_action_bridge_.enabled();
}

const VulkanActionBridgeResult&
GpuRuntime::last_vulkan_action_result() const noexcept {
  return vulkan_action_bridge_.last_result();
}

VulkanActionBridgeResult
GpuRuntime::ExecuteVulkanAction(const GpuAction& action) {
  VulkanActionBridgeResult result;
  result.packet_address = action.packet_address;
  result.opcode = action.opcode;
  result.first_register = action.render.first_register;
  result.first_value = action.render.first_value;
  if (!has_vulkan_context()) {
    result.status = VulkanActionBridgeStatus::kRendererUnavailable;
    result.message =
        "Vulkan action execution was enabled without InitializeVulkan";
    return result;
  }
  if (action.render.status != GpuRenderSnapshotStatus::kReady) {
    result.status = VulkanActionBridgeStatus::kUnsupportedState;
    result.message = "PM4 action is outside the immutable Vulkan bridge subset";
    return result;
  }
  if (action.type == GpuActionType::kDraw) {
    const GpuShaderBinding *vertex_binding = nullptr;
    const GpuShaderBinding *pixel_binding = nullptr;
    for (std::size_t index = 0; index < action.shader_binding_count; ++index) {
      const auto& binding = action.shader_bindings[index];
      if (binding.stage == GpuShaderStage::kExport) {
        vertex_binding = &binding;
      } else if (binding.stage == GpuShaderStage::kPixel) {
        pixel_binding = &binding;
      } else if (binding.program_address != 0) {
        result.status = VulkanActionBridgeStatus::kUnsupportedState;
        result.stage = binding.stage;
        result.message = "PM4 draw uses geometry, hull, or local shader state "
                         "outside the bridge subset";
        return result;
      }
    }
    if (vertex_binding == nullptr || pixel_binding == nullptr ||
        vertex_binding->status != GpuShaderBindingStatus::kRegistered ||
        pixel_binding->status != GpuShaderBindingStatus::kRegistered ||
        vertex_binding->code_address == 0 || pixel_binding->code_address == 0) {
      result.status = VulkanActionBridgeStatus::kShaderUnavailable;
      result.stage = vertex_binding == nullptr ? GpuShaderStage::kExport
                                               : GpuShaderStage::kPixel;
      result.message = "DrawIndexAuto requires registered export/vertex and "
                       "pixel shader bindings";
      return result;
    }

    shader::recompiler::CompileOptions vertex_options;
    vertex_options.stage = ShaderType::Vertex;
    vertex_options.shader_base = vertex_binding->program_address;
    vertex_options.shader_hash = vertex_binding->program_address;
    vertex_options.dump_ir = false;
    shader::recompiler::CompileResult vertex;
    const auto vertex_compile = RecompileRegisteredShader(
        vertex_binding->code_address, vertex_options, vertex);
    if (!vertex_compile) {
      result.status = VulkanActionBridgeStatus::kCompilationFailed;
      result.stage = GpuShaderStage::kExport;
      result.message = vertex_compile.error.empty()
                           ? "registered export shader compilation failed"
                           : vertex_compile.error;
      return result;
    }

    shader::recompiler::CompileOptions pixel_options;
    pixel_options.stage = ShaderType::Pixel;
    // Graphics bindings reserve set 0 for the vertex stage and set 1 for the
    // pixel stage. ShaderRuntime preserves this checked specialization while
    // it supplies the registered program base.
    pixel_options.descriptor_set = 1;
    pixel_options.shader_base = pixel_binding->program_address;
    pixel_options.shader_hash = pixel_binding->program_address;
    pixel_options.dump_ir = false;
    shader::recompiler::CompileResult pixel;
    const auto pixel_compile = RecompileRegisteredShader(
        pixel_binding->code_address, pixel_options, pixel);
    if (!pixel_compile) {
      result.status = VulkanActionBridgeStatus::kCompilationFailed;
      result.stage = GpuShaderStage::kPixel;
      result.message = pixel_compile.error.empty()
                           ? "registered pixel shader compilation failed"
                           : pixel_compile.error;
      return result;
    }
    const auto request = MakeActionDrawRequest(action.render, vertex, pixel);
    if (!request.has_value()) {
      result.status = VulkanActionBridgeStatus::kUnsupportedState;
      result.stage = GpuShaderStage::kPixel;
      result.message = "PM4 draw snapshot contains an unsupported topology, "
                       "blend, depth, or viewport state";
      return result;
    }
    const auto execution = SubmitVulkanTranslatedDraw(*request);
    result.timeline = execution.timeline;
    result.stage = GpuShaderStage::kPixel;
    if (execution.status == vulkan::VulkanGraphicsStatus::kOk) {
      result.status = VulkanActionBridgeStatus::kCompleted;
      result.message = "translated DrawIndexAuto completed";
    } else if (execution.status ==
                   vulkan::VulkanGraphicsStatus::kFenceWaitTimedOut ||
               execution.status ==
                   vulkan::VulkanGraphicsStatus::kRetainedWorkPending) {
      result.status = VulkanActionBridgeStatus::kBlocked;
      result.message = "translated DrawIndexAuto retained work; action will "
                       "resume by polling";
    } else if (execution.status == vulkan::VulkanGraphicsStatus::kDeviceLost) {
      result.status = VulkanActionBridgeStatus::kDeviceLost;
      result.message =
          "Vulkan device lost while executing translated DrawIndexAuto";
    } else {
      result.status = VulkanActionBridgeStatus::kExecutionFailed;
      result.message = execution.diagnostics.empty()
                           ? "translated DrawIndexAuto execution failed"
                           : execution.diagnostics.front().message;
    }
    return result;
  }

  if (action.type != GpuActionType::kDispatch ||
      action.shader_binding_count != 1 ||
      action.shader_bindings[0].stage != GpuShaderStage::kCompute ||
      action.shader_bindings[0].status != GpuShaderBindingStatus::kRegistered ||
      action.shader_bindings[0].code_address == 0) {
    result.status = VulkanActionBridgeStatus::kShaderUnavailable;
    result.stage = GpuShaderStage::kCompute;
    result.message =
        "direct compute requires exactly one registered CS program binding";
    return result;
  }
  shader::recompiler::CompileOptions options;
  options.stage = ShaderType::Compute;
  options.shader_base = action.shader_bindings[0].program_address;
  options.shader_hash = action.shader_bindings[0].program_address;
  options.dump_ir = false;
  shader::recompiler::CompileResult compile;
  const auto compile_result = RecompileRegisteredShader(
      action.shader_bindings[0].code_address, options, compile);
  if (!compile_result) {
    result.status = VulkanActionBridgeStatus::kCompilationFailed;
    result.stage = GpuShaderStage::kCompute;
    result.message = compile_result.error.empty()
                         ? "registered compute shader compilation failed"
                         : compile_result.error;
    return result;
  }
  const auto execution = SubmitVulkanTranslatedCompute(
      compile, action.render.group_count_x, action.render.group_count_y,
      action.render.group_count_z, 50'000'000ULL);
  result.timeline = execution.timeline;
  result.stage = GpuShaderStage::kCompute;
  if (execution.status == vulkan::VulkanComputeStatus::kOk) {
    result.status = VulkanActionBridgeStatus::kCompleted;
    result.message = "direct compute completed";
  } else if (execution.status ==
             vulkan::VulkanComputeStatus::kFenceWaitTimedOut) {
    result.status = VulkanActionBridgeStatus::kBlocked;
    result.message =
        "direct compute fence timed out; action retained for polling";
  } else if (execution.status == vulkan::VulkanComputeStatus::kDeviceLost) {
    result.status = VulkanActionBridgeStatus::kDeviceLost;
    result.message = "Vulkan device lost while executing direct compute";
  } else {
    result.status = VulkanActionBridgeStatus::kExecutionFailed;
    result.message = execution.diagnostics.empty()
                         ? "direct compute execution failed"
                         : execution.diagnostics.front().message;
  }
  return result;
}

VulkanActionBridgeResult
GpuRuntime::PollVulkanActionExecution(const GpuAction& action) {
  VulkanActionBridgeResult result;
  result.packet_address = action.packet_address;
  result.opcode = action.opcode;
  result.stage = action.type == GpuActionType::kDraw ? GpuShaderStage::kPixel
                                                     : GpuShaderStage::kCompute;
  result.first_register = action.render.first_register;
  result.first_value = action.render.first_value;
  if (action.type == GpuActionType::kDraw) {
    const auto execution = PollVulkanGraphics();
    result.timeline = execution.completed_timeline;
    if (execution.status == vulkan::VulkanGraphicsStatus::kDeviceLost) {
      result.status = VulkanActionBridgeStatus::kDeviceLost;
      result.message =
          "Vulkan device lost while polling retained translated DrawIndexAuto";
    } else if (execution.status != vulkan::VulkanGraphicsStatus::kOk) {
      result.status = VulkanActionBridgeStatus::kExecutionFailed;
      result.message = execution.diagnostics.empty()
                           ? "translated DrawIndexAuto polling failed"
                           : execution.diagnostics.front().message;
    } else if (execution.retained_submission_count != 0) {
      result.status = VulkanActionBridgeStatus::kBlocked;
      result.message =
          "translated DrawIndexAuto remains retained by an unsignalled fence";
    } else {
      result.status = VulkanActionBridgeStatus::kCompleted;
      result.message = "retained translated DrawIndexAuto completed";
    }
    return result;
  }
  const auto execution = PollVulkanCompute();
  result.timeline = execution.completed_timeline;
  if (execution.status == vulkan::VulkanComputeStatus::kDeviceLost) {
    result.status = VulkanActionBridgeStatus::kDeviceLost;
    result.message = "Vulkan device lost while polling retained direct compute";
  } else if (execution.status != vulkan::VulkanComputeStatus::kOk) {
    result.status = VulkanActionBridgeStatus::kExecutionFailed;
    result.message = execution.diagnostics.empty()
                         ? "direct compute polling failed"
                         : execution.diagnostics.front().message;
  } else if (execution.retained_submission_count != 0) {
    result.status = VulkanActionBridgeStatus::kBlocked;
    result.message = "direct compute remains retained by an unsignalled fence";
  } else {
    result.status = VulkanActionBridgeStatus::kCompleted;
    result.message = "retained direct compute completed";
  }
  return result;
}

vulkan::VulkanInitializationResult GpuRuntime::InitializeVulkan(
    const vulkan::VulkanContextOptions& options) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ != nullptr) {
    vulkan::VulkanInitializationResult result;
    result.status = vulkan::VulkanContextStatus::kAlreadyInitialized;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanDiagnosticCode::kDuplicateInitialization, std::nullopt,
         VK_SUCCESS,
         "GpuRuntime already owns a Vulkan device context; the duplicate "
         "request was rejected without changing it"});
    return result;
  }

  auto created = vulkan::VulkanDeviceContext::Create(options);
  if (!created) {
    return std::move(created.initialization);
  }
  vulkan_context_ = std::move(created.context);
  return std::move(created.initialization);
}

vulkan::VulkanPresentationResult GpuRuntime::InitializeVulkanPresentation(
    const vulkan::VulkanSurfaceFactory& surface_factory,
    const vulkan::VulkanContextOptions& requested_options) {
  std::lock_guard lock(vulkan_mutex_);
  vulkan::VulkanPresentationResult result;
  if (vulkan_context_ != nullptr || vulkan_presentation_ != nullptr) {
    result.status = vulkan::VulkanPresentationStatus::kContextUnavailable;
    result.diagnostics.push_back({result.status, VK_SUCCESS,
        "presentation must be selected when the runtime creates its Vulkan context"});
    return result;
  }
  auto options = requested_options;
  options.surface_factory = &surface_factory;
  auto created = vulkan::VulkanDeviceContext::Create(options);
  if (!created) {
    result.status = vulkan::VulkanPresentationStatus::kContextUnavailable;
    for (const auto& diagnostic : created.initialization.diagnostics) {
      result.diagnostics.push_back({result.status, diagnostic.api_result,
                                    diagnostic.message});
    }
    return result;
  }
  auto image_cache = std::make_unique<vulkan::VulkanGuestImageCache>(
      *created.context, memory_, *resource_coherence_);
  auto presentation = vulkan::VulkanPresentation::Create(
      *created.context, *image_cache, result);
  if (!presentation) return result;
  vulkan_context_ = std::move(created.context);
  vulkan_image_cache_ = std::move(image_cache);
  vulkan_presentation_ = std::move(presentation);
  return result;
}

vulkan::VulkanPresentationResult GpuRuntime::PresentVulkanGuestFrame(
    const GuestImageLayoutInput& input, std::uint64_t timeout_ns) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_presentation_ == nullptr) {
    vulkan::VulkanPresentationResult result;
    result.status = vulkan::VulkanPresentationStatus::kContextUnavailable;
    result.diagnostics.push_back({result.status, VK_SUCCESS,
        "InitializeVulkanPresentation must succeed before presenting a guest frame"});
    return result;
  }
  return vulkan_presentation_->Present(input, timeout_ns);
}

vulkan::VulkanPresentationResult GpuRuntime::PollVulkanPresentation() {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_presentation_ == nullptr) {
    vulkan::VulkanPresentationResult result;
    result.status = vulkan::VulkanPresentationStatus::kContextUnavailable;
    return result;
  }
  return vulkan_presentation_->Poll();
}

vulkan::VulkanPresentationResult GpuRuntime::ResizeVulkanPresentation(
    VkExtent2D extent) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_presentation_ == nullptr) {
    vulkan::VulkanPresentationResult result;
    result.status = vulkan::VulkanPresentationStatus::kContextUnavailable;
    return result;
  }
  return vulkan_presentation_->RequestResize(extent);
}

vulkan::VulkanInitializationResult GpuRuntime::InitializeVulkan(
    vulkan::VulkanLoader loader, const vulkan::VulkanContextOptions& options) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ != nullptr) {
    vulkan::VulkanInitializationResult result;
    result.status = vulkan::VulkanContextStatus::kAlreadyInitialized;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanDiagnosticCode::kDuplicateInitialization, std::nullopt,
         VK_SUCCESS,
         "GpuRuntime already owns a Vulkan device context; the duplicate "
         "request was rejected without changing it"});
    return result;
  }

  auto created = vulkan::VulkanDeviceContext::Create(std::move(loader), options);
  if (!created) {
    return std::move(created.initialization);
  }
  vulkan_context_ = std::move(created.context);
  return std::move(created.initialization);
}

bool GpuRuntime::has_vulkan_context() const noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_context_ != nullptr;
}

vulkan::VulkanDeviceContext* GpuRuntime::vulkan_context() noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_context_.get();
}

const vulkan::VulkanDeviceContext* GpuRuntime::vulkan_context() const
    noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_context_.get();
}

vulkan::VulkanComputeResult GpuRuntime::SubmitVulkanCompute(
    std::span<const std::uint32_t> spirv_words,
    std::uint32_t group_count_x,
    std::uint32_t group_count_y,
    std::uint32_t group_count_z,
    std::uint64_t timeout_ns) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ == nullptr) {
    vulkan::VulkanComputeResult result;
    result.status = vulkan::VulkanComputeStatus::kContextUnavailable;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanComputeDiagnosticCode::kContextUnavailable, 0,
         VK_SUCCESS,
         "GpuRuntime cannot execute SPIR-V before InitializeVulkan creates "
         "its Vulkan device context"});
    return result;
  }
  if (vulkan_execution_ == nullptr) {
    auto created = vulkan::VulkanComputeExecution::Create(*vulkan_context_);
    if (!created) {
      return std::move(created.initialization);
    }
    vulkan_execution_ = std::move(created.execution);
  }
  return vulkan_execution_->Submit(spirv_words, group_count_x, group_count_y,
                                   group_count_z, timeout_ns);
}

vulkan::VulkanComputeResult GpuRuntime::PollVulkanCompute() {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ == nullptr) {
    vulkan::VulkanComputeResult result;
    result.status = vulkan::VulkanComputeStatus::kContextUnavailable;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanComputeDiagnosticCode::kContextUnavailable, 0,
         VK_SUCCESS,
         "GpuRuntime has no Vulkan device context to poll"});
    return result;
  }
  if (vulkan_execution_ == nullptr) {
    return {};
  }
  return vulkan_execution_->PollCompleted();
}

vulkan::VulkanGraphicsResult GpuRuntime::SubmitVulkanTranslatedDraw(
    const vulkan::VulkanTranslatedDrawRequest& request) {
  std::lock_guard lock(vulkan_mutex_);
  vulkan::VulkanGraphicsResult result;
  if (vulkan_context_ == nullptr) {
    result.status = vulkan::VulkanGraphicsStatus::kContextUnavailable;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        "GpuRuntime cannot execute a translated draw before InitializeVulkan"});
    return result;
  }
  if (!HasValidGraphicsPreflight(request)) {
    result.status = vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back(
        {vulkan::VulkanGraphicsDiagnosticSeverity::kError,
         vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
         "translated draw failed runtime preflight before guest-image preparation"});
    return result;
  }
  const auto mapped_format = vulkan::MapGuestImageFormat(request.color_target.format);
  if (!mapped_format.has_value() ||
      !vulkan_context_->SupportsColorAttachmentFormat(mapped_format->format)) {
    result.status = vulkan::VulkanGraphicsStatus::kUnsupported;
    result.diagnostics.push_back(
        {vulkan::VulkanGraphicsDiagnosticSeverity::kError,
         vulkan::VulkanGraphicsDiagnosticCode::kFormatUnsupported, 0, VK_SUCCESS,
         "translated draw color-target format is not a supported color attachment"});
    return result;
  }
  if (request.depth_stencil_target.has_value()) {
    const auto& depth = *request.depth_stencil_target;
    if (depth.depth_format != Prospero::DepthFormat::kZ32F ||
        depth.stencil_format != Prospero::StencilFormat::kInvalid ||
        depth.samples != VK_SAMPLE_COUNT_1_BIT || depth.width == 0 ||
        depth.height == 0 || depth.guest_address == 0 ||
        !vulkan_context_->SupportsDepthStencilAttachmentFormat(
            VK_FORMAT_D32_SFLOAT)) {
      result.status = vulkan::VulkanGraphicsStatus::kUnsupported;
      result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
          vulkan::VulkanGraphicsDiagnosticCode::kFormatUnsupported, 0, VK_SUCCESS,
          "translated draw depth target requires supported one-sample Z32 float storage"});
      return result;
    }
  }
  if (vulkan_buffer_cache_ == nullptr) {
    vulkan_buffer_cache_ = std::make_unique<vulkan::VulkanGuestBufferCache>(
        *vulkan_context_, memory_, *resource_coherence_);
  }
  if (vulkan_image_cache_ == nullptr) {
    vulkan_image_cache_ = std::make_unique<vulkan::VulkanGuestImageCache>(
        *vulkan_context_, memory_, *resource_coherence_);
  }
  // Reclaim signalled work without blocking. If a writer remains in flight,
  // GuestMemory still contains the older generation and cannot seed either an
  // attachment load or a read-only feedback snapshot.
  if (vulkan_graphics_execution_ != nullptr) {
    const auto reclaimed = vulkan_graphics_execution_->PollCompleted();
    if (!reclaimed) return reclaimed;
  }
  const auto color_layout = CalculateGuestImageLayout(request.color_target);
  const auto depth_layout = request.depth_stencil_target.has_value()
      ? PlannedDepthLayout(*request.depth_stencil_target) : std::nullopt;
  const bool color_pending = color_layout.ok() &&
      resource_coherence_->HasGpuWritePendingOverlap(
          color_layout.storage_key.guest_address, color_layout.storage_key.byte_count);
  const bool depth_pending = depth_layout.has_value() &&
      resource_coherence_->HasGpuWritePendingOverlap(
          depth_layout->storage_key.guest_address, depth_layout->storage_key.byte_count);
  if (color_pending || depth_pending) {
    result.status = vulkan::VulkanGraphicsStatus::kRetainedWorkPending;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kWarning,
        vulkan::VulkanGraphicsDiagnosticCode::kRetainedWorkPending, 0, VK_NOT_READY,
        "translated draw retained overlapping GPU work; retry after PollVulkanGraphics"});
    return result;
  }
  auto vertex_buffers = vulkan_buffer_cache_->Prepare(*request.vertex);
  if (!vertex_buffers) {
    result.status = vertex_buffers.status == vulkan::VulkanGuestBufferStatus::kResourceLimit
        ? vulkan::VulkanGraphicsStatus::kResourceLimit : vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        result.status == vulkan::VulkanGraphicsStatus::kResourceLimit
            ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
            : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        vertex_buffers.diagnostics.empty() ? "vertex buffer preparation failed"
                                           : vertex_buffers.diagnostics.front().message});
    vulkan_buffer_cache_->Discard(vertex_buffers);
    return result;
  }
  auto pixel_buffers = vulkan_buffer_cache_->Prepare(*request.pixel);
  if (!pixel_buffers) {
    vulkan_buffer_cache_->Discard(vertex_buffers);
    result.status = pixel_buffers.status == vulkan::VulkanGuestBufferStatus::kResourceLimit
        ? vulkan::VulkanGraphicsStatus::kResourceLimit : vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        result.status == vulkan::VulkanGraphicsStatus::kResourceLimit
            ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
            : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        pixel_buffers.diagnostics.empty() ? "pixel buffer preparation failed"
                                          : pixel_buffers.diagnostics.front().message});
    vulkan_buffer_cache_->Discard(pixel_buffers);
    return result;
  }
  auto vertex_images = vulkan_image_cache_->PrepareTranslated(*request.vertex);
  if (!vertex_images) {
    vulkan_buffer_cache_->Discard(vertex_buffers);
    vulkan_buffer_cache_->Discard(pixel_buffers);
    result.status = vertex_images.status == vulkan::VulkanGuestImageSetStatus::kResourceLimit
        ? vulkan::VulkanGraphicsStatus::kResourceLimit : vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        result.status == vulkan::VulkanGraphicsStatus::kResourceLimit
            ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
            : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        vertex_images.diagnostics.empty() ? "vertex image preparation failed"
                                          : vertex_images.diagnostics.front().message});
    vulkan_image_cache_->Discard(vertex_images);
    return result;
  }
  auto pixel_images = vulkan_image_cache_->PrepareTranslated(*request.pixel);
  if (!pixel_images) {
    vulkan_buffer_cache_->Discard(vertex_buffers);
    vulkan_buffer_cache_->Discard(pixel_buffers);
    vulkan_image_cache_->Discard(vertex_images);
    result.status = pixel_images.status == vulkan::VulkanGuestImageSetStatus::kResourceLimit
        ? vulkan::VulkanGraphicsStatus::kResourceLimit : vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        result.status == vulkan::VulkanGraphicsStatus::kResourceLimit
            ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
            : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        pixel_images.diagnostics.empty() ? "pixel image preparation failed"
                                         : pixel_images.diagnostics.front().message});
    vulkan_image_cache_->Discard(pixel_images);
    return result;
  }
  vulkan::VulkanGuestImageRequest target_request;
  target_request.input = request.color_target;
  target_request.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  target_request.writable = true;
  auto target = vulkan_image_cache_->Prepare(target_request);
  if (!target) {
    result.status = target.status == vulkan::VulkanGuestImageStatus::kResourceLimit
        ? vulkan::VulkanGraphicsStatus::kResourceLimit
        : vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        target.status == vulkan::VulkanGuestImageStatus::kResourceLimit
            ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
            : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected,
        0, VK_SUCCESS, target.diagnostics.empty()
            ? "translated draw color-target preparation failed"
            : target.diagnostics.front().message});
    vulkan_image_cache_->Discard(target);
    vulkan_buffer_cache_->Discard(vertex_buffers);
    vulkan_buffer_cache_->Discard(pixel_buffers);
    vulkan_image_cache_->Discard(vertex_images);
    vulkan_image_cache_->Discard(pixel_images);
    return result;
  }
  std::optional<vulkan::VulkanGuestImagePreparation> depth_target;
  if (request.depth_stencil_target.has_value()) {
    const auto& depth = *request.depth_stencil_target;
    auto prepared_depth = vulkan_image_cache_->PrepareDepthStencil({
        depth.guest_address, depth.depth_format, depth.stencil_format,
        depth.width, depth.height, depth.row_pitch_bytes, depth.samples, true});
    if (!prepared_depth) {
      result.status = prepared_depth.status == vulkan::VulkanGuestImageStatus::kResourceLimit
          ? vulkan::VulkanGraphicsStatus::kResourceLimit
          : vulkan::VulkanGraphicsStatus::kInvalidArgument;
      result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
          result.status == vulkan::VulkanGraphicsStatus::kResourceLimit
              ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
              : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected,
          0, VK_SUCCESS, prepared_depth.diagnostics.empty()
              ? "translated draw depth-target preparation failed"
              : prepared_depth.diagnostics.front().message});
      vulkan_image_cache_->Discard(prepared_depth);
      vulkan_image_cache_->Discard(target);
      vulkan_buffer_cache_->Discard(vertex_buffers);
      vulkan_buffer_cache_->Discard(pixel_buffers);
      vulkan_image_cache_->Discard(vertex_images);
      vulkan_image_cache_->Discard(pixel_images);
      return result;
    }
    depth_target.emplace(std::move(prepared_depth));
  }
  vulkan::VulkanGraphicsBindingPlan plan;
  const auto plan_status = vulkan::BuildVulkanGraphicsBindingPlan(
      vulkan_context_->properties(), {request.vertex, &vertex_buffers, &vertex_images},
      {request.pixel, &pixel_buffers, &pixel_images}, plan);
  if (plan_status != vulkan::VulkanGraphicsBindingStatus::kOk) {
    result.status = plan_status == vulkan::VulkanGraphicsBindingStatus::kDeviceResourceLimit ||
                    plan_status == vulkan::VulkanGraphicsBindingStatus::kAllocationFailure
        ? vulkan::VulkanGraphicsStatus::kResourceLimit
        : vulkan::VulkanGraphicsStatus::kInvalidArgument;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        result.status == vulkan::VulkanGraphicsStatus::kResourceLimit
            ? vulkan::VulkanGraphicsDiagnosticCode::kResourceLimit
            : vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        plan.diagnostics.empty() ? "graphics binding plan failed"
                                 : plan.diagnostics.front().message});
    vulkan_image_cache_->Discard(target);
    if (depth_target.has_value()) vulkan_image_cache_->Discard(*depth_target);
    vulkan_buffer_cache_->Discard(vertex_buffers);
    vulkan_buffer_cache_->Discard(pixel_buffers);
    vulkan_image_cache_->Discard(vertex_images);
    vulkan_image_cache_->Discard(pixel_images);
    return result;
  }
  if (vulkan_graphics_execution_ == nullptr) {
    auto created = vulkan::VulkanGraphicsExecution::Create(*vulkan_context_);
    if (!created) {
      vulkan_image_cache_->Discard(target);
      if (depth_target.has_value()) vulkan_image_cache_->Discard(*depth_target);
      vulkan_buffer_cache_->Discard(vertex_buffers);
      vulkan_buffer_cache_->Discard(pixel_buffers);
      vulkan_image_cache_->Discard(vertex_images);
      vulkan_image_cache_->Discard(pixel_images);
      return std::move(created.initialization);
    }
    vulkan_graphics_execution_ = std::move(created.execution);
  }
  return vulkan_graphics_execution_->Submit(
      request, *vulkan_buffer_cache_, std::move(vertex_buffers),
      std::move(pixel_buffers), *vulkan_image_cache_, std::move(vertex_images),
      std::move(pixel_images), std::move(target), std::move(depth_target),
      std::move(plan));
}

vulkan::VulkanGraphicsResult GpuRuntime::PollVulkanGraphics() {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ == nullptr) {
    vulkan::VulkanGraphicsResult result;
    result.status = vulkan::VulkanGraphicsStatus::kContextUnavailable;
    result.diagnostics.push_back({vulkan::VulkanGraphicsDiagnosticSeverity::kError,
        vulkan::VulkanGraphicsDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
        "GpuRuntime has no Vulkan device context to poll"});
    return result;
  }
  return vulkan_graphics_execution_ != nullptr
      ? vulkan_graphics_execution_->PollCompleted() : vulkan::VulkanGraphicsResult{};
}

vulkan::VulkanComputeResult GpuRuntime::SubmitVulkanTranslatedCompute(
    const shader::recompiler::CompileResult& compile,
    std::uint32_t group_count_x, std::uint32_t group_count_y,
    std::uint32_t group_count_z, std::uint64_t timeout_ns) {
  std::lock_guard lock(vulkan_mutex_);
  vulkan::VulkanComputeResult result;
  if (vulkan_context_ == nullptr) {
    result.status = vulkan::VulkanComputeStatus::kContextUnavailable;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanComputeDiagnosticCode::kContextUnavailable, 0,
         VK_SUCCESS,
         "GpuRuntime cannot execute translated compute before "
         "InitializeVulkan"});
    return result;
  }
  if (timeout_ns == std::numeric_limits<std::uint64_t>::max() ||
      group_count_x == 0 || group_count_y == 0 || group_count_z == 0) {
    result.status = vulkan::VulkanComputeStatus::kInvalidArgument;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanComputeDiagnosticCode::kInputRejected, 0, VK_SUCCESS,
         "translated compute requires nonzero groups and a finite timeout"});
    return result;
  }
  if (vulkan_buffer_cache_ == nullptr) {
    vulkan_buffer_cache_ = std::make_unique<vulkan::VulkanGuestBufferCache>(
        *vulkan_context_, memory_, *resource_coherence_);
  }
  if (vulkan_image_cache_ == nullptr) {
    vulkan_image_cache_ = std::make_unique<vulkan::VulkanGuestImageCache>(
        *vulkan_context_, memory_, *resource_coherence_);
  }
  // Both preparations are leases.  Do not let one cache's partial success
  // escape if the other cache rejects the immutable specialization.
  auto buffers = vulkan_buffer_cache_->Prepare(compile);
  if (!buffers) {
    const bool resource_limit =
        buffers.status == vulkan::VulkanGuestBufferStatus::kResourceLimit;
    result.status = resource_limit ? vulkan::VulkanComputeStatus::kResourceLimit
                                   : vulkan::VulkanComputeStatus::kInvalidArgument;
    const std::string message =
        buffers.diagnostics.empty()
            ? "translated compute guest-buffer preparation failed"
            : buffers.diagnostics.front().message;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         resource_limit ? vulkan::VulkanComputeDiagnosticCode::kResourceLimit
                        : vulkan::VulkanComputeDiagnosticCode::kInputRejected,
         0, VK_SUCCESS,
         message});
    vulkan_buffer_cache_->Discard(buffers);
    return result;
  }
  auto images = vulkan_image_cache_->PrepareTranslated(compile);
  if (!images) {
    const bool resource_limit =
        images.status == vulkan::VulkanGuestImageSetStatus::kResourceLimit;
    result.status = resource_limit ? vulkan::VulkanComputeStatus::kResourceLimit
                                   : vulkan::VulkanComputeStatus::kInvalidArgument;
    const std::string message = images.diagnostics.empty()
        ? "translated compute guest-image preparation failed"
        : images.diagnostics.front().message;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         resource_limit ? vulkan::VulkanComputeDiagnosticCode::kResourceLimit
                        : vulkan::VulkanComputeDiagnosticCode::kInputRejected,
         0, VK_SUCCESS,
         message});
    vulkan_image_cache_->Discard(images);
    vulkan_buffer_cache_->Discard(buffers);
    return result;
  }
  if (vulkan_execution_ == nullptr) {
    auto created = vulkan::VulkanComputeExecution::Create(*vulkan_context_);
    if (!created) {
      vulkan_image_cache_->Discard(images);
      vulkan_buffer_cache_->Discard(buffers);
      return std::move(created.initialization);
    }
    vulkan_execution_ = std::move(created.execution);
  }
  return vulkan_execution_->SubmitTranslated(
      compile, *vulkan_buffer_cache_, std::move(buffers),
      *vulkan_image_cache_, std::move(images), group_count_x, group_count_y,
      group_count_z, timeout_ns);
}

bool GpuRuntime::has_vulkan_compute_execution() const noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_execution_ != nullptr;
}

ShaderMapResult GpuRuntime::CreateShader(std::uint64_t destination_address,
                                         std::uint64_t header_address,
                                         std::uint64_t code_address) {
  return shader_runtime_.CreateShader(destination_address, header_address,
                                      code_address);
}

std::optional<RegisteredShader> GpuRuntime::LookupRegisteredShader(
    std::uint64_t code_address) const {
  return shader_runtime_.Lookup(code_address);
}

ShaderCompileResult GpuRuntime::RecompileRegisteredShader(
    std::uint64_t code_address,
    const shader::recompiler::CompileOptions& options,
    shader::recompiler::CompileResult& result) {
  return shader_runtime_.Recompile(code_address, options, result);
}

GpuPacketResult GpuRuntime::AppendPacket(
    std::uint64_t command_buffer, std::span<const std::uint32_t> packet) {
  if (command_buffer == 0 || packet.size() < 2 ||
      packet.size() > kMaximumPm4PacketDwords) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }

  std::lock_guard lock(mutex_);
  std::uint64_t cursor_up_address = 0;
  std::uint64_t cursor_down_address = 0;
  std::uint64_t callback_address = 0;
  std::uint64_t reserved_address = 0;
  if (!Add(command_buffer, kAgcCommandBufferCursorUpOffset,
           cursor_up_address) ||
      !Add(command_buffer, kAgcCommandBufferCursorDownOffset,
           cursor_down_address) ||
      !Add(command_buffer, kAgcCommandBufferCallbackOffset,
           callback_address) ||
      !Add(command_buffer, kAgcCommandBufferReservedDwordsOffset,
           reserved_address)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  std::array<std::byte, sizeof(std::uint64_t)> cursor_up_bytes{};
  std::array<std::byte, sizeof(std::uint64_t)> cursor_down_bytes{};
  std::array<std::byte, sizeof(std::uint64_t)> callback_bytes{};
  std::array<std::byte, sizeof(std::uint32_t)> reserved_bytes{};
  if (!memory_.Read(cursor_up_address, cursor_up_bytes) ||
      !memory_.Read(cursor_down_address, cursor_down_bytes) ||
      !memory_.Read(callback_address, callback_bytes) ||
      !memory_.Read(reserved_address, reserved_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  const auto cursor_up = Read64(cursor_up_bytes);
  const auto cursor_down = Read64(cursor_down_bytes);
  const auto callback = Read64(callback_bytes);
  const auto reserved_dwords = Read32(reserved_bytes);
  if (cursor_up == 0 || cursor_down <= cursor_up ||
      (cursor_up & 3U) != 0 || (cursor_down & 3U) != 0) {
    return {GpuRuntimeStatus::kBufferTooSmall};
  }

  const auto available_bytes = cursor_down - cursor_up;
  const auto available_dwords = available_bytes / sizeof(std::uint32_t);
  const auto usable_dwords = available_dwords > reserved_dwords
                                 ? available_dwords - reserved_dwords
                                 : 0;
  if (packet.size() > usable_dwords) {
    return {callback != 0 ? GpuRuntimeStatus::kCallbackRequired
                          : GpuRuntimeStatus::kBufferTooSmall};
  }

  const auto packet_bytes_size =
      static_cast<std::uint64_t>(packet.size() * sizeof(std::uint32_t));
  std::uint64_t next_cursor = 0;
  if (!Add(cursor_up, packet_bytes_size, next_cursor) ||
      !memory_.CanAccess(cursor_up, packet_bytes_size,
                         memory::GuestMemoryProtection::kWrite) ||
      !memory_.CanAccess(cursor_up_address, sizeof(std::uint64_t),
                         memory::GuestMemoryProtection::kWrite)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  std::vector<std::byte> packet_bytes;
  try {
    packet_bytes.resize(static_cast<std::size_t>(packet_bytes_size));
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  for (std::size_t index = 0; index < packet.size(); ++index) {
    Write32(packet_bytes, index * sizeof(std::uint32_t), packet[index]);
  }
  std::array<std::byte, sizeof(std::uint64_t)> next_cursor_bytes{};
  Write64(next_cursor_bytes, next_cursor);
  if (!memory_.Write(cursor_up, packet_bytes) ||
      !memory_.Write(cursor_up_address, next_cursor_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }
  return {GpuRuntimeStatus::kOk, cursor_up};
}

GpuPacketResult GpuRuntime::WriteNop(std::uint64_t command_buffer,
                                     std::uint32_t dword_count) {
  if (dword_count < 2 || dword_count > kMaximumPm4PacketDwords) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  std::vector<std::uint32_t> packet;
  try {
    packet.assign(dword_count, 0);
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  packet[0] = MakePm4Header(dword_count, kPm4NopOpcode, 0);
  return AppendPacket(command_buffer, packet);
}

GpuPacketResult GpuRuntime::WriteDispatch(
    std::uint64_t command_buffer, std::uint32_t group_count_x,
    std::uint32_t group_count_y, std::uint32_t group_count_z,
    std::uint32_t modifier) {
  const std::array packet = {
      MakePm4Header(5, kPm4DispatchDirectOpcode, 0), group_count_x,
      group_count_y, group_count_z,
      (modifier & kDirectDispatchModifierMask) |
          kDirectDispatchRequiredBits};
  return AppendPacket(command_buffer, packet);
}

GpuPacketResult GpuRuntime::WriteAgcPacket(
    AgcPacketType type, std::span<const std::uint64_t> arguments) {
  if (arguments.empty()) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  const auto argument = [arguments](std::size_t index) noexcept {
    return index < arguments.size() ? arguments[index] : 0;
  };
  const auto command_buffer = argument(0);
  const auto append = [this, command_buffer](
                          std::initializer_list<std::uint32_t> words) {
    return AppendPacket(
        command_buffer,
        std::span<const std::uint32_t>(words.begin(), words.size()));
  };

  try {
    if (type == AgcPacketType::kSetShRegisterDirect ||
        type == AgcPacketType::kSetCxRegisterDirect ||
        type == AgcPacketType::kSetUcRegisterDirect) {
      const auto packed_register = argument(1);
      auto opcode = kPm4SetShRegisterOpcode;
      if (type == AgcPacketType::kSetCxRegisterDirect) {
        opcode = kPm4SetContextRegisterOpcode;
      } else if (type == AgcPacketType::kSetUcRegisterDirect) {
        opcode = kPm4SetUconfigRegisterOpcode;
      }
      return append({MakePm4Header(3, opcode, 0),
                     static_cast<std::uint32_t>(packed_register) & 0xffffU,
                     static_cast<std::uint32_t>(packed_register >> 32U)});
    }
    if (type == AgcPacketType::kSetIndexSize) {
      const auto index_size = static_cast<std::uint32_t>(argument(1));
      const auto cache_policy = static_cast<std::uint32_t>(argument(2));
      return append(
          {MakePm4Header(3, kPm4SetUconfigRegisterIndexOpcode, 0),
           0x20000000U | kVgtIndexTypeRegister,
           0x400U | (index_size & 0x3U) | ((cache_policy & 0x3U) << 6U)});
    }
    if (type == AgcPacketType::kSetIndexBuffer) {
      const auto address = argument(1);
      if ((address & 1U) != 0) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      return append({MakePm4Header(3, kPm4IndexBaseOpcode, 0),
                     static_cast<std::uint32_t>(address),
                     static_cast<std::uint32_t>(address >> 32U)});
    }
    if (type == AgcPacketType::kSetIndexCount) {
      return append({MakePm4Header(2, kPm4IndexBufferSizeOpcode, 0),
                     static_cast<std::uint32_t>(argument(1))});
    }
    if (type == AgcPacketType::kSetNumInstances) {
      return append({MakePm4Header(2, kPm4NumInstancesOpcode, 0),
                     static_cast<std::uint32_t>(argument(1))});
    }
    if (type == AgcPacketType::kDrawIndex) {
      const auto count = static_cast<std::uint32_t>(argument(1));
      const auto address = argument(2);
      const auto modifier = argument(3);
      if (address == 0 || (address & 1U) != 0) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      return append({MakePm4Header(6, kPm4DrawIndexOpcode, 0),
                     count == 0 ? 1U : count,
                     static_cast<std::uint32_t>(address),
                     static_cast<std::uint32_t>(address >> 32U), count,
                     DrawIndexInitiator(modifier)});
    }
    if (type == AgcPacketType::kDrawIndexMultiInstanced) {
      const auto count = static_cast<std::uint32_t>(argument(1));
      const auto index_address = argument(2);
      const auto object_address = argument(3);
      const auto instance_count = static_cast<std::uint32_t>(argument(4));
      if (index_address == 0 || object_address == 0 ||
          (index_address & 1U) != 0) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      return append(
          {MakePm4Header(9, 0x3aU, 0), count,
           static_cast<std::uint32_t>(index_address),
           static_cast<std::uint32_t>(index_address >> 32U),
           instance_count == 0 ? 1U : instance_count,
           static_cast<std::uint32_t>(object_address),
           static_cast<std::uint32_t>(object_address >> 32U), instance_count,
           DrawIndexInitiator(argument(5)) | 0x80U});
    }
    if (type == AgcPacketType::kDrawIndexAuto) {
      return append({MakePm4Header(3, kPm4DrawIndexAutoOpcode, 0),
                     static_cast<std::uint32_t>(argument(1)),
                     DrawIndexInitiator(argument(2)) | 0x2U});
    }
    if (type == AgcPacketType::kDrawIndexOffset) {
      const auto count = static_cast<std::uint32_t>(argument(2));
      return append({MakePm4Header(5, kPm4DrawIndexOffsetOpcode, 0),
                     count == 0 ? 1U : count,
                     static_cast<std::uint32_t>(argument(1)), count,
                     DrawIndexInitiator(argument(3))});
    }
    if (type == AgcPacketType::kSetBaseIndirectArgs) {
      const auto shader_type = static_cast<std::uint32_t>(argument(1));
      const auto address = argument(2);
      return append(
          {MakePm4Header(4, kPm4SetBaseOpcode, 0) | (shader_type << 1U), 1U,
           static_cast<std::uint32_t>(address) & ~0x7U,
           static_cast<std::uint32_t>(address >> 32U)});
    }
    if (type == AgcPacketType::kDispatchIndirect) {
      const auto flags = static_cast<std::uint32_t>(argument(2));
      return append({MakePm4Header(3, kPm4DispatchIndirectOpcode, 0),
                     static_cast<std::uint32_t>(argument(1)),
                     (flags & kDirectDispatchModifierMask) |
                         kDirectDispatchRequiredBits});
    }
    if (type == AgcPacketType::kJump) {
      const auto mode = static_cast<std::uint32_t>(argument(1));
      const auto cache_policy = static_cast<std::uint32_t>(argument(2));
      const auto target = argument(3);
      const auto size = static_cast<std::uint32_t>(argument(4));
      return append(
          {MakePm4Header(4, kPm4IndirectBufferOpcode, 0),
           static_cast<std::uint32_t>(target) & ~0x3U,
           static_cast<std::uint32_t>(target >> 32U),
           0x0f200000U | ((cache_policy & 0x3U) << 28U) |
               ((mode & 0x1U) << 20U) | (size & 0xfffffU)});
    }
    if (type == AgcPacketType::kRewind) {
      return append({MakePm4Header(2, kPm4RewindOpcode, 0),
                     (static_cast<std::uint32_t>(argument(1)) & 1U) << 31U});
    }
    if (type == AgcPacketType::kSetPredication) {
      const auto condition = static_cast<std::uint32_t>(argument(1));
      const auto operation = static_cast<std::uint32_t>(argument(2));
      const auto wait_operation = static_cast<std::uint32_t>(argument(3));
      const auto address = argument(4);
      return append(
          {MakePm4Header(4, kPm4SetPredicationOpcode, 0),
           ((condition & 1U) << 8U) | ((wait_operation & 1U) << 12U) |
               ((operation & 7U) << 16U),
           static_cast<std::uint32_t>(address) & ~0xfU,
           static_cast<std::uint32_t>(address >> 32U)});
    }
    if (type == AgcPacketType::kWriteData) {
      const auto data_address = argument(4);
      const auto dword_count = static_cast<std::uint32_t>(argument(5));
      if (data_address == 0 || dword_count > 0x3ffdU) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      const auto byte_count =
          static_cast<std::size_t>(dword_count) * sizeof(std::uint32_t);
      std::vector<std::byte> data(byte_count);
      if (byte_count != 0 && !memory_.Read(data_address, data)) {
        return {GpuRuntimeStatus::kMemoryFault};
      }
      std::vector<std::uint32_t> packet(4 + dword_count);
      const auto destination = static_cast<std::uint32_t>(argument(1));
      const auto cache_policy = static_cast<std::uint32_t>(argument(2));
      const auto increment = static_cast<std::uint32_t>(argument(6));
      const auto write_confirm = destination == 0
                                     ? 0U
                                     : static_cast<std::uint32_t>(argument(7)) &
                                           1U;
      packet[0] =
          MakePm4Header(4 + dword_count, kPm4NopOpcode,
                        kPm4WriteDataRegister);
      packet[1] = (destination & 0xffU) |
                  ((cache_policy & 0xffU) << 8U) |
                  ((increment & 0xffU) << 16U) |
                  ((write_confirm & 0xffU) << 24U);
      packet[2] = static_cast<std::uint32_t>(argument(3)) & ~0x3U;
      packet[3] = static_cast<std::uint32_t>(argument(3) >> 32U);
      for (std::size_t index = 0; index < dword_count; ++index) {
        packet[4 + index] = Read32(
            std::span<const std::byte>(data).subspan(index * 4U, 4U));
      }
      return AppendPacket(command_buffer, packet);
    }
    if (type == AgcPacketType::kReleaseMemory) {
      const auto action = static_cast<std::uint32_t>(argument(1));
      auto gcr_control = static_cast<std::uint32_t>(argument(2));
      const auto destination = static_cast<std::uint32_t>(argument(3));
      const auto cache_policy = static_cast<std::uint32_t>(argument(4));
      auto destination_address = argument(5);
      const auto data_selection = static_cast<std::uint32_t>(argument(6));
      auto data = argument(7);
      const auto gds_offset = static_cast<std::uint32_t>(argument(8));
      const auto gds_size = static_cast<std::uint32_t>(argument(9));
      const auto interrupt = static_cast<std::uint32_t>(argument(10));
      const auto interrupt_context =
          static_cast<std::uint32_t>(argument(11));
      if (destination > 1 ||
          (data_selection > 3 && data_selection != 5) || interrupt > 4) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      if ((gcr_control & 0x300U) == 0x100U) {
        gcr_control |= 0x200U;
      }
      if (interrupt == 4) {
        destination_address = 0;
        data = 0;
      } else if (data_selection == 5) {
        data = static_cast<std::uint64_t>(gds_offset & 0xffffU) |
               (static_cast<std::uint64_t>(gds_size & 0xffffU) << 16U);
      }
      const auto event_index = action >= 0x2fU ? 6U : 5U;
      return append(
          {MakePm4Header(8, kPm4NopOpcode, kPm4ReleaseMemoryRegister),
           (action & 0x3fU) | (event_index << 8U) |
               ((gcr_control & 0xfffU) << 12U) |
               ((cache_policy & 3U) << 25U),
           ((destination & 3U) << 16U) | ((interrupt & 7U) << 24U) |
               ((data_selection & 7U) << 29U),
           static_cast<std::uint32_t>(destination_address) & ~3U,
           static_cast<std::uint32_t>(destination_address >> 32U),
           static_cast<std::uint32_t>(data),
           static_cast<std::uint32_t>(data >> 32U),
          interrupt_context & 0x07ffffffU});
    }
    if (type == AgcPacketType::kEventWrite) {
      const auto event_type = static_cast<std::uint32_t>(argument(1));
      const auto address = argument(2);
      if (event_type > 0x3fU) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      if ((event_type & 0xfeU) == 0x38U) {
        return append(
            {MakePm4Header(4, kPm4EventWriteOpcode, 0),
             0x100U | event_type,
             static_cast<std::uint32_t>(address) & ~7U,
             static_cast<std::uint32_t>(address >> 32U)});
      }
      const auto control = event_type == 7 || event_type == 15 ||
                                   event_type == 16
                               ? 0x400U | event_type
                               : event_type;
      return append({MakePm4Header(2, kPm4EventWriteOpcode, 0), control});
    }
    if (type == AgcPacketType::kGetLodStats) {
      const auto address = argument(2);
      const auto cache_policy = static_cast<std::uint32_t>(argument(1));
      const auto reset_count = static_cast<std::uint32_t>(argument(4));
      const auto force_reset = static_cast<std::uint32_t>(argument(5));
      const auto report_and_reset = static_cast<std::uint32_t>(argument(6));
      const auto interval = static_cast<std::uint32_t>(argument(7));
      return append(
          {MakePm4Header(5, kPm4GetLodStatsOpcode, 0),
           static_cast<std::uint32_t>(argument(3)),
           static_cast<std::uint32_t>(address) & 0xffffffc0U,
           static_cast<std::uint32_t>(address >> 32U),
           ((cache_policy & 3U) << 28U) |
               ((report_and_reset & 1U) << 19U) |
               ((force_reset & 1U) << 18U) |
               ((reset_count & 0xffU) << 10U) | ((interval & 0xffU) << 2U)});
    }
    if (type == AgcPacketType::kWaitRegMem) {
      const auto size = static_cast<std::uint32_t>(argument(1));
      const auto compare = static_cast<std::uint32_t>(argument(2));
      const auto operation = static_cast<std::uint32_t>(argument(3));
      const auto cache_policy = static_cast<std::uint32_t>(argument(4));
      const auto address = argument(5);
      const auto reference = argument(6);
      const auto mask = argument(7);
      const auto poll = WaitPoll(static_cast<std::uint32_t>(argument(8)));
      if (size > 1 || compare > 7 || operation > 4 || cache_policy > 3) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      if (size == 0) {
        return append(
            {MakePm4Header(7, kPm4NopOpcode, kPm4WaitMemory32Register),
             static_cast<std::uint32_t>(address) & ~0x3U,
             static_cast<std::uint32_t>(address >> 32U) & 0x3ffffU,
             static_cast<std::uint32_t>(mask),
             static_cast<std::uint32_t>(reference),
             Wait32Control(compare, operation, cache_policy), poll});
      }
      return append(
          {MakePm4Header(9, kPm4NopOpcode, kPm4WaitMemory64Register),
           static_cast<std::uint32_t>(address) & ~0x7U,
           static_cast<std::uint32_t>(address >> 32U) & 0x3ffffU,
           static_cast<std::uint32_t>(mask),
           static_cast<std::uint32_t>(mask >> 32U),
           static_cast<std::uint32_t>(reference),
           static_cast<std::uint32_t>(reference >> 32U),
           Wait64Control(compare, operation, cache_policy), poll});
    }
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  return {GpuRuntimeStatus::kInvalidArgument};
}

GpuPacketSizeResult GpuRuntime::GetPacketSize(
    std::uint64_t packet_address) const noexcept {
  if (packet_address == 0) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  std::array<std::byte, sizeof(std::uint32_t)> header_bytes{};
  if (!memory_.Read(packet_address, header_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }
  const auto header = Read32(header_bytes);
  if ((header & 0x3fffff00U) == 0x3fff1000U) {
    return {GpuRuntimeStatus::kOk, 1};
  }
  return {GpuRuntimeStatus::kOk,
          static_cast<std::uint32_t>(((header >> 16U) & 0x3fffU) + 2U)};
}

GpuRuntimeStatus GpuRuntime::SetPacketPredication(
    std::uint64_t packet_address, std::uint32_t predication) noexcept {
  if (packet_address == 0) {
    return GpuRuntimeStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  std::array<std::byte, sizeof(std::uint32_t)> header_bytes{};
  if (!memory_.Read(packet_address, header_bytes)) {
    return GpuRuntimeStatus::kMemoryFault;
  }
  auto header = Read32(header_bytes);
  header = (header & ~1U) | (predication == 1 ? 1U : 0U);
  Write32(header_bytes, 0, header);
  return memory_.Write(packet_address, header_bytes)
             ? GpuRuntimeStatus::kOk
             : GpuRuntimeStatus::kMemoryFault;
}

const char* GpuRuntimeStatusName(GpuRuntimeStatus status) noexcept {
  switch (status) {
    case GpuRuntimeStatus::kOk:
      return "ok";
    case GpuRuntimeStatus::kInvalidArgument:
      return "invalid-argument";
    case GpuRuntimeStatus::kMemoryFault:
      return "memory-fault";
    case GpuRuntimeStatus::kBufferTooSmall:
      return "buffer-too-small";
    case GpuRuntimeStatus::kCallbackRequired:
      return "callback-required";
    case GpuRuntimeStatus::kResourceLimit:
      return "resource-limit";
  }
  return "unknown";
}

}  // namespace kajps5::gpu
