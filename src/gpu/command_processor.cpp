// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/graphics/guest_gpu/command_processor and
// src/graphics/guest_gpu/graphicsRun.cpp at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Shader-program register reference: KytyPS5 src/libs/agc.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/command_processor.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "kernel/event_queue.h"

namespace kajps5::gpu {
namespace {

constexpr std::uint32_t kPm4TypeMask = 0xc0000000U;
constexpr std::uint32_t kPm4Type2 = 0x80000000U;
constexpr std::uint32_t kPm4Type3 = 0xc0000000U;
constexpr std::uint32_t kPm4Nop = 0x10U;
constexpr std::uint32_t kPm4SetBase = 0x11U;
constexpr std::uint32_t kPm4IndexBufferSize = 0x13U;
constexpr std::uint32_t kPm4DispatchDirect = 0x15U;
constexpr std::uint32_t kPm4DispatchIndirect = 0x16U;
constexpr std::uint32_t kPm4SetPredication = 0x20U;
constexpr std::uint32_t kPm4DrawIndirect = 0x24U;
constexpr std::uint32_t kPm4DrawIndexIndirect = 0x25U;
constexpr std::uint32_t kPm4IndexBase = 0x26U;
constexpr std::uint32_t kPm4DrawIndex = 0x27U;
constexpr std::uint32_t kPm4IndexType = 0x2aU;
constexpr std::uint32_t kPm4DrawIndirectMulti = 0x2cU;
constexpr std::uint32_t kPm4DrawIndexAuto = 0x2dU;
constexpr std::uint32_t kPm4NumInstances = 0x2fU;
constexpr std::uint32_t kPm4DrawIndexOffset = 0x35U;
constexpr std::uint32_t kPm4WriteData = 0x37U;
constexpr std::uint32_t kPm4DrawIndexIndirectMulti = 0x38U;
constexpr std::uint32_t kPm4WaitRegMem = 0x3cU;
constexpr std::uint32_t kPm4DrawIndexMultiInstanced = 0x3aU;
constexpr std::uint32_t kPm4IndirectBuffer = 0x3fU;
constexpr std::uint32_t kPm4EventWrite = 0x46U;
constexpr std::uint32_t kPm4ReleaseMemory = 0x49U;
constexpr std::uint32_t kPm4Rewind = 0x59U;
constexpr std::uint32_t kPm4SetShRegisterIndirect = 0x63U;
constexpr std::uint32_t kPm4SetUcRegisterIndirect = 0x64U;
constexpr std::uint32_t kPm4SetContextRegister = 0x69U;
constexpr std::uint32_t kPm4SetShRegister = 0x76U;
constexpr std::uint32_t kPm4SetUcRegister = 0x79U;
constexpr std::uint32_t kPm4SetUcRegisterIndex = 0x7aU;
constexpr std::uint32_t kPm4GetLodStats = 0x8eU;
constexpr std::uint32_t kPm4SetContextRegisterIndirect = 0x9fU;

constexpr std::uint32_t kPm4WaitMemory32Register = 0x0aU;
constexpr std::uint32_t kPm4WriteDataRegister = 0x15U;
constexpr std::uint32_t kPm4ReleaseMemoryRegister = 0x18U;
constexpr std::uint32_t kPm4ShaderRegistersIndirect = 0x11U;
constexpr std::uint32_t kPm4ContextRegistersIndirect = 0x12U;
constexpr std::uint32_t kPm4UserConfigRegistersIndirect = 0x13U;
constexpr std::uint32_t kPm4WaitMemory64Register = 0x16U;
constexpr std::uint32_t kRegisterSelectorMask = 0x70000000U;

using RegisterMap = std::unordered_map<std::uint32_t, std::uint32_t>;

struct DecodeContext {
  memory::GuestMemory& memory;
  GpuSubmissionSink& sink;
  GpuCommandCursor& cursor;
  ShaderRuntime& shader_runtime;
  RegisterMap& context_registers;
  RegisterMap& shader_registers;
  RegisterMap& user_config_registers;
};

struct ShaderProgramPair {
  GpuShaderStage stage = GpuShaderStage::kPixel;
  std::uint32_t lo = 0;
  std::uint32_t hi = 0;
};

// Exact Kyty PGM LO/HI reconstruction uses bits 8..47. Keep graphics order
// stable for future pipeline-key construction regardless of register-write
// order in the packet stream.
constexpr std::array kGraphicsShaderProgramPairs = {
    ShaderProgramPair{GpuShaderStage::kPixel, 0x008U, 0x009U},
    ShaderProgramPair{GpuShaderStage::kGeometry, 0x088U, 0x089U},
    ShaderProgramPair{GpuShaderStage::kExport, 0x0c8U, 0x0c9U},
    ShaderProgramPair{GpuShaderStage::kHull, 0x108U, 0x109U},
    ShaderProgramPair{GpuShaderStage::kLocal, 0x148U, 0x149U},
};
constexpr ShaderProgramPair kComputeShaderProgramPair{
    GpuShaderStage::kCompute, 0x20cU, 0x20dU};
constexpr std::uint32_t kSyntheticDrawInstanceCount = 0xfffffff0U;

// KytyPS5 pm4.h context/user-config offsets at fb5ecec.  The bridge keeps
// these local rather than exposing a second mutable hardware context.
constexpr std::uint32_t kCbTargetMask = 0x08eU;
constexpr std::uint32_t kVportScissorTl = 0x094U;
constexpr std::uint32_t kVportScissorBr = 0x095U;
constexpr std::uint32_t kVportZMin = 0x0b4U;
constexpr std::uint32_t kVportZMax = 0x0b5U;
constexpr std::uint32_t kVportXScale = 0x10fU;
constexpr std::uint32_t kCbBlend0Control = 0x1e0U;
constexpr std::uint32_t kDbDepthControl = 0x200U;
constexpr std::uint32_t kPaSuScModeControl = 0x205U;
constexpr std::uint32_t kCbColor0Base = 0x318U;
constexpr std::uint32_t kCbColor0Pitch = 0x319U;
constexpr std::uint32_t kCbColor0Slice = 0x31aU;
constexpr std::uint32_t kCbColor0View = 0x31bU;
constexpr std::uint32_t kCbColor0Info = 0x31cU;
constexpr std::uint32_t kCbColor0Attrib = 0x31dU;
constexpr std::uint32_t kCbColor0Attrib2 = 0x3b0U;
constexpr std::uint32_t kCbColor0Attrib3 = 0x3b8U;
constexpr std::uint32_t kDbDepthSizeXy = 0x007U;
constexpr std::uint32_t kDbZInfo = 0x010U;
constexpr std::uint32_t kDbZWriteBase = 0x014U;
constexpr std::uint32_t kDbDepthSize = 0x016U;
constexpr std::uint32_t kDbZWriteBaseHi = 0x01cU;
constexpr std::uint32_t kVgtPrimitiveType = 0x242U;

std::uint32_t Read32(std::span<const std::byte> bytes,
                     std::size_t offset = 0) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
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

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

std::uint32_t NormalizeRegisterOffset(std::uint32_t raw_offset) noexcept {
  return raw_offset & ~kRegisterSelectorMask;
}

RegisterMap& RegistersFor(DecodeContext& context,
                          GpuRegisterSpace space) noexcept {
  switch (space) {
    case GpuRegisterSpace::kContext:
      return context.context_registers;
    case GpuRegisterSpace::kShader:
      return context.shader_registers;
    case GpuRegisterSpace::kUserConfig:
      return context.user_config_registers;
  }
  return context.context_registers;
}

GpuAction MakeAction(GpuActionType type, std::uint64_t packet_address,
                     std::uint32_t packet_dwords, std::uint32_t opcode,
                     std::uint32_t packet_register,
                     std::initializer_list<std::uint64_t> values = {}) {
  GpuAction action;
  action.type = type;
  action.packet_address = packet_address;
  action.packet_dwords = packet_dwords;
  action.opcode = opcode;
  action.packet_register = packet_register;
  action.value_count = std::min(values.size(), action.values.size());
  std::copy_n(values.begin(), action.value_count, action.values.begin());
  return action;
}

void AddProgramBindingSnapshot(const RegisterMap& shader_registers,
                               ShaderProgramPair pair,
                               GpuAction& action) noexcept {
  const auto lo = shader_registers.find(pair.lo);
  const auto hi = shader_registers.find(pair.hi);
  if (lo == shader_registers.end() || hi == shader_registers.end() ||
      action.shader_binding_count >= action.shader_bindings.size()) {
    return;
  }
  const auto program_address =
      (static_cast<std::uint64_t>(lo->second) << 8U) |
      ((static_cast<std::uint64_t>(hi->second) & 0xffU) << 40U);
  if (program_address == 0) {
    return;
  }
  auto& binding = action.shader_bindings[action.shader_binding_count++];
  binding = {};
  binding.stage = pair.stage;
  binding.program_address = program_address;
}

void SnapshotProgramBindings(const DecodeContext& context,
                             GpuAction& action) noexcept {
  action.shader_binding_count = 0;
  if (action.type == GpuActionType::kDraw) {
    for (const auto pair : kGraphicsShaderProgramPairs) {
      AddProgramBindingSnapshot(context.shader_registers, pair, action);
    }
  } else if (action.type == GpuActionType::kDispatch) {
    AddProgramBindingSnapshot(context.shader_registers,
                              kComputeShaderProgramPair, action);
  }
}

void SnapshotRenderState(const DecodeContext& context, const GpuAction& action,
                         GpuAction& enriched) noexcept {
  // The bounded bridge executes DISPATCH_DIRECT and the validated
  // DrawIndexAuto graphics subset. Their packet scalars and register state are
  // copied here with shader bindings; unrepresented draw state is never
  // guessed from an incomplete context register set.
  enriched.render = {};
  if (action.type == GpuActionType::kDispatch &&
      action.opcode == kPm4DispatchDirect && action.value_count >= 3 &&
      action.values[0] != 0 && action.values[1] != 0 && action.values[2] != 0 &&
      action.values[0] <= std::numeric_limits<std::uint32_t>::max() &&
      action.values[1] <= std::numeric_limits<std::uint32_t>::max() &&
      action.values[2] <= std::numeric_limits<std::uint32_t>::max()) {
    enriched.render.status = GpuRenderSnapshotStatus::kReady;
    enriched.render.group_count_x =
        static_cast<std::uint32_t>(action.values[0]);
    enriched.render.group_count_y =
        static_cast<std::uint32_t>(action.values[1]);
    enriched.render.group_count_z =
        static_cast<std::uint32_t>(action.values[2]);
    return;
  }
  enriched.render.status = GpuRenderSnapshotStatus::kUnsupported;
  enriched.render.first_register =
      action.type == GpuActionType::kDraw ? kCbColor0Base : 0;
  enriched.render.first_value = action.opcode;
  if (action.type != GpuActionType::kDraw ||
      action.opcode != kPm4DrawIndexAuto || action.value_count < 2 ||
      action.values[0] == 0 ||
      action.values[0] > std::numeric_limits<std::uint32_t>::max()) {
    return;
  }

  const auto missing = [&enriched](std::uint32_t reg) {
    enriched.render.first_register = reg;
    enriched.render.first_value = 0;
  };
  const auto unsupported = [&enriched](std::uint32_t reg, std::uint32_t value) {
    enriched.render.first_register = reg;
    enriched.render.first_value = value;
  };
  const auto read_context = [&context, &missing](std::uint32_t reg,
                                                 std::uint32_t& value) {
    const auto found = context.context_registers.find(reg);
    if (found == context.context_registers.end()) {
      missing(reg);
      return false;
    }
    value = found->second;
    return true;
  };
  const auto read_user = [&context, &missing](std::uint32_t reg,
                                              std::uint32_t& value) {
    const auto found = context.user_config_registers.find(reg);
    if (found == context.user_config_registers.end()) {
      missing(reg);
      return false;
    }
    value = found->second;
    return true;
  };
  std::uint32_t base = 0, pitch = 0, slice = 0, view = 0, info = 0, attrib = 0,
                attrib2 = 0, attrib3 = 0, target_mask = 0, blend = 0, mode = 0,
                primitive = 0, scissor_tl = 0, scissor_br = 0, xscale = 0,
                xoffset = 0, yscale = 0, yoffset = 0, zmin = 0, zmax = 0,
                instances = 0, depth_control = 0;
  if (!read_context(kCbColor0Base, base) ||
      !read_context(kCbColor0Pitch, pitch) ||
      !read_context(kCbColor0Slice, slice) ||
      !read_context(kCbColor0View, view) ||
      !read_context(kCbColor0Info, info) ||
      !read_context(kCbColor0Attrib, attrib) ||
      !read_context(kCbColor0Attrib2, attrib2) ||
      !read_context(kCbColor0Attrib3, attrib3) ||
      !read_context(kCbTargetMask, target_mask) ||
      !read_context(kCbBlend0Control, blend) ||
      !read_context(kPaSuScModeControl, mode) ||
      !read_context(kVportScissorTl, scissor_tl) ||
      !read_context(kVportScissorBr, scissor_br) ||
      !read_context(kVportXScale, xscale) ||
      !read_context(kVportXScale + 1U, xoffset) ||
      !read_context(kVportXScale + 2U, yscale) ||
      !read_context(kVportXScale + 3U, yoffset) ||
      !read_context(kVportZMin, zmin) || !read_context(kVportZMax, zmax) ||
      !read_context(kDbDepthControl, depth_control) ||
      !read_user(kVgtPrimitiveType, primitive) ||
      !read_user(kSyntheticDrawInstanceCount, instances))
    return;

  // Slot 0 only; a nonzero slot-1 base or any target-mask bit beyond RGBA is
  // MRT.
  if ((target_mask & ~0xfU) != 0) {
    unsupported(kCbTargetMask, target_mask);
    return;
  }
  const auto second_target =
      context.context_registers.find(kCbColor0Base + 15U);
  if (second_target != context.context_registers.end() &&
      second_target->second != 0) {
    unsupported(kCbColor0Base + 15U, second_target->second);
    return;
  }
  // Linear, one-sample, 2D RGBA8 UNORM only.  These are the exact fields used
  // by Kyty HwCtxSetRenderTarget: FORMAT[6:2], TILE_MODE_INDEX[4:0], samples.
  if (((info >> 2U) & 0x1fU) != 10U || ((info >> 8U) & 7U) != 0 ||
      ((info >> 11U) & 3U) != 0 ||
      (info & ((1U << 14U) | (1U << 28U) | (1U << 31U))) != 0) {
    unsupported(kCbColor0Info, info);
    return;
  }
  if ((attrib & 0x1fU) != 0 || ((attrib >> 12U) & 7U) != 0 ||
      ((attrib >> 15U) & 3U) != 0 || attrib3 != 0 || slice != 0 || view != 0) {
    unsupported((attrib3 != 0) ? kCbColor0Attrib3 : kCbColor0Attrib,
                attrib3 != 0 ? attrib3 : attrib);
    return;
  }
  if ((mode & 3U) == 3U ||
      (mode & ((1U << 11U) | (1U << 12U) | (1U << 16U))) != 0) {
    unsupported(kPaSuScModeControl, mode);
    return;
  }
  // Basic Vulkan-compatible blend factors/operations. Dual-source and
  // hardware-only encodings remain an explicit bridge rejection.
  const auto blend_basic =
      (blend & 0x1fU) <= 10U && ((blend >> 5U) & 7U) <= 4U &&
      ((blend >> 8U) & 0x1fU) <= 10U && ((blend >> 16U) & 0x1fU) <= 10U &&
      ((blend >> 21U) & 7U) <= 4U && ((blend >> 24U) & 0x1fU) <= 10U;
  if (!blend_basic) {
    unsupported(kCbBlend0Control, blend);
    return;
  }
  const auto primitive_type = primitive & 0x3fU;
  if (primitive_type < 1U || primitive_type > 6U || primitive_type == 5U) {
    unsupported(kVgtPrimitiveType, primitive);
    return;
  }
  const auto width = ((attrib2 >> 14U) & 0x3fffU) + 1U;
  const auto height = (attrib2 & 0x3fffU) + 1U;
  const auto row_pitch = (static_cast<std::uint64_t>(pitch & 0x7ffU) + 1U) * 8U;
  if (base == 0 || width == 0 || height == 0 ||
      row_pitch < static_cast<std::uint64_t>(width) * 4U || instances == 0 ||
      (scissor_tl >> 31U) != 0) {
    unsupported(kCbColor0Base, base);
    return;
  }

  enriched.render.status = GpuRenderSnapshotStatus::kReady;
  enriched.render.vertex_count = static_cast<std::uint32_t>(action.values[0]);
  enriched.render.instance_count = instances;
  enriched.render.color_base = static_cast<std::uint64_t>(base) << 8U;
  enriched.render.color_row_pitch_bytes = row_pitch;
  enriched.render.color_width = width;
  enriched.render.color_height = height;
  enriched.render.color_format = 56U; // Prospero::BufferFormat::k8_8_8_8UNorm
  enriched.render.color_write_mask = target_mask & 0xfU;
  enriched.render.primitive_type = primitive_type;
  enriched.render.cull_mode = mode & 3U;
  enriched.render.front_face_clockwise = (mode & (1U << 2U)) != 0;
  enriched.render.blend_enable = (blend & (1U << 30U)) != 0;
  enriched.render.blend_control = blend;
  enriched.render.viewport_x_scale_bits = xscale;
  enriched.render.viewport_x_offset_bits = xoffset;
  enriched.render.viewport_y_scale_bits = yscale;
  enriched.render.viewport_y_offset_bits = yoffset;
  enriched.render.viewport_z_min_bits = zmin;
  enriched.render.viewport_z_max_bits = zmax;
  enriched.render.scissor_left =
      static_cast<std::int32_t>(scissor_tl & 0x7fffU);
  enriched.render.scissor_top =
      static_cast<std::int32_t>((scissor_tl >> 16U) & 0x7fffU);
  enriched.render.scissor_right =
      static_cast<std::int32_t>(scissor_br & 0x7fffU);
  enriched.render.scissor_bottom =
      static_cast<std::int32_t>((scissor_br >> 16U) & 0x7fffU);
  if (enriched.render.scissor_right <= enriched.render.scissor_left ||
      enriched.render.scissor_bottom <= enriched.render.scissor_top) {
    unsupported(kVportScissorBr, scissor_br);
    enriched.render.status = GpuRenderSnapshotStatus::kUnsupported;
    return;
  }
  enriched.render.depth_enabled = (depth_control & (1U << 1U)) != 0;
  enriched.render.depth_write_enabled = (depth_control & (1U << 2U)) != 0;
  enriched.render.depth_compare = (depth_control >> 4U) & 7U;
  if (!enriched.render.depth_enabled)
    return;
  std::uint32_t zinfo = 0, zbase = 0, zbase_hi = 0, zsize_xy = 0, zsize = 0;
  if (!read_context(kDbZInfo, zinfo) || !read_context(kDbZWriteBase, zbase) ||
      !read_context(kDbZWriteBaseHi, zbase_hi) ||
      !read_context(kDbDepthSizeXy, zsize_xy) ||
      !read_context(kDbDepthSize, zsize))
    return;
  if ((zinfo & 3U) != 3U || ((zinfo >> 2U) & 3U) != 0 ||
      ((zinfo >> 20U) & 7U) != 0 || (zinfo & (1U << 29U)) != 0 || zbase == 0) {
    unsupported(kDbZInfo, zinfo);
    enriched.render.status = GpuRenderSnapshotStatus::kUnsupported;
    return;
  }
  enriched.render.depth_base =
      (static_cast<std::uint64_t>(zbase) << 8U) |
      ((static_cast<std::uint64_t>(zbase_hi) & 0xffU) << 40U);
  enriched.render.depth_width = (zsize_xy & 0x3fffU) + 1U;
  enriched.render.depth_height = ((zsize_xy >> 16U) & 0x3fffU) + 1U;
  enriched.render.depth_row_pitch_bytes =
      (static_cast<std::uint64_t>(zsize & 0x7ffU) + 1U) * 8U;
  if (enriched.render.depth_row_pitch_bytes <
      static_cast<std::uint64_t>(enriched.render.depth_width) * 4U) {
    unsupported(kDbDepthSize, zsize);
    enriched.render.status = GpuRenderSnapshotStatus::kUnsupported;
  }
}

GpuCommandStatus Stop(DecodeContext& context, GpuCommandStatus status,
                      std::uint64_t packet_address,
                      std::uint32_t opcode = 0) noexcept {
  context.cursor.result.status = status;
  context.cursor.result.packet_address = packet_address;
  context.cursor.result.opcode = opcode;
  return status;
}

GpuCommandStatus Emit(DecodeContext& context,
                      const GpuAction& action) noexcept {
  if (context.cursor.result.submitted_actions >=
      context.cursor.limits.max_actions) {
    return Stop(context, GpuCommandStatus::kResourceLimit,
                action.packet_address, action.opcode);
  }
  if (action.type == GpuActionType::kDraw ||
      action.type == GpuActionType::kDispatch) {
    try {
      // Bind against a local copy so later register writes cannot change an
      // already delivered action, while preserving the decoder's packet and
      // scalar action fields unchanged.
      GpuAction enriched = action;
      SnapshotProgramBindings(context, enriched);
      SnapshotRenderState(context, action, enriched);
      const auto binding_status = context.shader_runtime.ResolveProgramBindings(
          std::span<GpuShaderBinding>(enriched.shader_bindings.data(),
                                      enriched.shader_binding_count));
      if (binding_status != ShaderRuntimeStatus::kOk) {
        return Stop(context,
                    binding_status == ShaderRuntimeStatus::kResourceLimit
                        ? GpuCommandStatus::kResourceLimit
                        : GpuCommandStatus::kInvalidArgument,
                    action.packet_address, action.opcode);
      }
      const auto sink_status = context.sink.Submit(enriched);
      if (sink_status != GpuCommandStatus::kComplete) {
        return Stop(context, sink_status, action.packet_address, action.opcode);
      }
    } catch (...) {
      return Stop(context, GpuCommandStatus::kResourceLimit,
                  action.packet_address, action.opcode);
    }
    ++context.cursor.result.submitted_actions;
    return GpuCommandStatus::kComplete;
  }
  const auto sink_status = context.sink.Submit(action);
  if (sink_status != GpuCommandStatus::kComplete) {
    return Stop(context, sink_status, action.packet_address, action.opcode);
  }
  ++context.cursor.result.submitted_actions;
  return GpuCommandStatus::kComplete;
}

GpuCommandStatus ReadDwords(DecodeContext& context, std::uint64_t address,
                            std::uint32_t dword_count,
                            std::vector<std::uint32_t>& words) {
  if (address == 0 || (address & 3U) != 0 || dword_count == 0 ||
      dword_count > context.cursor.limits.max_buffer_dwords) {
    return Stop(context, GpuCommandStatus::kInvalidArgument, address);
  }
  const auto byte_count = static_cast<std::size_t>(dword_count) * 4U;
  std::vector<std::byte> bytes;
  try {
    bytes.resize(byte_count);
    words.resize(dword_count);
  } catch (const std::bad_alloc&) {
    return Stop(context, GpuCommandStatus::kResourceLimit, address);
  }
  if (!context.memory.Read(address, bytes)) {
    return Stop(context, GpuCommandStatus::kMemoryFault, address);
  }
  for (std::size_t index = 0; index < words.size(); ++index) {
    words[index] = Read32(bytes, index * 4U);
  }
  return GpuCommandStatus::kComplete;
}

std::optional<std::uint64_t> ReadGuestValue(
    DecodeContext& context, std::uint64_t address, bool is_64_bit) noexcept {
  if (is_64_bit) {
    std::array<std::byte, 8> bytes{};
    if (!context.memory.Read(address, bytes)) {
      return std::nullopt;
    }
    return Read64(bytes);
  }
  std::array<std::byte, 4> bytes{};
  if (!context.memory.Read(address, bytes)) {
    return std::nullopt;
  }
  return Read32(bytes);
}

bool CompareWait(std::uint64_t value, std::uint64_t reference,
                 std::uint64_t mask, std::uint32_t function) noexcept {
  const auto masked = value & mask;
  switch (function) {
    case 0:
    case 7:
      return true;
    case 1:
      return masked < reference;
    case 2:
      return masked <= reference;
    case 3:
      return masked == reference;
    case 4:
      return masked != reference;
    case 5:
      return masked >= reference;
    case 6:
      return masked > reference;
  }
  return true;
}

GpuCommandStatus ApplyRegisterWrite(
    DecodeContext& context, GpuRegisterSpace space,
    std::uint64_t packet_address, std::uint32_t packet_dwords,
    std::uint32_t opcode, std::uint32_t packet_register,
    std::uint32_t raw_offset, std::uint32_t value,
    std::uint64_t table_address = 0, std::uint64_t table_index = 0) {
  const auto offset = NormalizeRegisterOffset(raw_offset);
  const auto action = MakeAction(
      GpuActionType::kRegisterWrite, packet_address, packet_dwords, opcode,
      packet_register,
      {static_cast<std::uint64_t>(space), offset, value, table_address,
       table_index});
  const auto status = Emit(context, action);
  if (status == GpuCommandStatus::kComplete) {
    RegistersFor(context, space)[offset] = value;
  }
  return status;
}

GpuCommandStatus ApplyDirectRegisters(
    DecodeContext& context, GpuRegisterSpace space,
    std::uint64_t packet_address, std::uint32_t packet_dwords,
    std::uint32_t opcode, std::uint32_t packet_register,
    std::span<const std::uint32_t> packet) {
  if (packet_dwords < 3) {
    return Stop(context, GpuCommandStatus::kMalformedPacket, packet_address,
                opcode);
  }
  const auto start = packet[1];
  for (std::uint32_t index = 0; index < packet_dwords - 2U; ++index) {
    const auto status = ApplyRegisterWrite(
        context, space, packet_address, packet_dwords, opcode,
        packet_register, start + index, packet[2U + index]);
    if (status != GpuCommandStatus::kComplete) {
      return status;
    }
  }
  return GpuCommandStatus::kComplete;
}

GpuCommandStatus ApplyIndirectRegisters(
    DecodeContext& context, GpuRegisterSpace space,
    std::uint64_t packet_address, std::uint32_t packet_dwords,
    std::uint32_t opcode, std::uint32_t packet_register,
    std::uint64_t table_address, std::uint32_t register_count) {
  if (register_count == 0) {
    return GpuCommandStatus::kComplete;
  }
  if (register_count > context.cursor.limits.max_buffer_dwords / 2U ||
      register_count > context.cursor.limits.max_actions) {
    return Stop(context, GpuCommandStatus::kResourceLimit, packet_address,
                opcode);
  }
  std::vector<std::uint32_t> table;
  const auto read_status = ReadDwords(context, table_address,
                                     register_count * 2U, table);
  if (read_status != GpuCommandStatus::kComplete) {
    context.cursor.result.packet_address = packet_address;
    context.cursor.result.opcode = opcode;
    return read_status;
  }
  for (std::uint32_t index = 0; index < register_count; ++index) {
    const auto status = ApplyRegisterWrite(
        context, space, packet_address, packet_dwords, opcode,
        packet_register, table[index * 2U], table[index * 2U + 1U],
        table_address, index);
    if (status != GpuCommandStatus::kComplete) {
      return status;
    }
  }
  return GpuCommandStatus::kComplete;
}

GpuCommandStatus ProcessWriteData(
    DecodeContext& context, std::uint64_t packet_address,
    std::uint32_t packet_dwords, std::uint32_t opcode,
    std::uint32_t packet_register,
    std::span<const std::uint32_t> packet) {
  if (packet_dwords < 4) {
    return Stop(context, GpuCommandStatus::kMalformedPacket,
                packet_address, opcode);
  }
  const auto destination = static_cast<std::uint64_t>(packet[2]) |
                           (static_cast<std::uint64_t>(packet[3]) << 32U);
  const auto data_count = packet_dwords - 4U;
  auto action = MakeAction(
      GpuActionType::kWriteData, packet_address, packet_dwords, opcode,
      packet_register,
      {destination, packet[1], data_count,
       data_count > 0 ? packet[4] : 0,
       data_count > 1 ? packet[5] : 0});
  try {
    action.payload.assign(packet.begin() + 4U, packet.end());
  } catch (...) {
    return Stop(context, GpuCommandStatus::kResourceLimit,
                packet_address, opcode);
  }
  return Emit(context, action);
}

GpuCommandStatus ProcessReleaseMemory(
    DecodeContext& context, std::uint64_t packet_address,
    std::uint32_t packet_dwords, std::uint32_t opcode,
    std::uint32_t packet_register,
    std::span<const std::uint32_t> packet) {
  if (packet_dwords != 8) {
    return Stop(context, GpuCommandStatus::kMalformedPacket,
                packet_address, opcode);
  }
  const auto destination = static_cast<std::uint64_t>(packet[3]) |
                           (static_cast<std::uint64_t>(packet[4]) << 32U);
  const auto data = static_cast<std::uint64_t>(packet[5]) |
                    (static_cast<std::uint64_t>(packet[6]) << 32U);
  return Emit(context, MakeAction(
      GpuActionType::kReleaseMemory, packet_address, packet_dwords, opcode,
      packet_register,
      {destination, data, packet[1], packet[2], packet[7]}));
}

GpuCommandStatus ProcessEventWrite(
    DecodeContext& context, std::uint64_t packet_address,
    std::uint32_t packet_dwords, std::uint32_t opcode,
    std::uint32_t packet_register,
    std::span<const std::uint32_t> packet) {
  if (packet_dwords != 2 && packet_dwords != 4) {
    return Stop(context, GpuCommandStatus::kMalformedPacket,
                packet_address, opcode);
  }
  const auto event_type = packet[1] & 0x3fU;
  const auto address = packet_dwords == 4
                           ? static_cast<std::uint64_t>(packet[2] & ~7U) |
                                 (static_cast<std::uint64_t>(packet[3]) << 32U)
                           : 0;
  return Emit(context, MakeAction(
      GpuActionType::kEventWrite, packet_address, packet_dwords, opcode,
      packet_register, {event_type, address, packet[1]}));
}

GpuCommandStatus ProcessWait(DecodeContext& context,
                             std::uint64_t packet_address,
                             std::uint32_t packet_dwords,
                             std::uint32_t opcode,
                             std::uint32_t packet_register,
                             std::uint64_t address, std::uint64_t mask,
                             std::uint64_t reference,
                             std::uint32_t control, std::uint32_t poll,
                             bool is_64_bit) {
  const auto function = control & 7U;
  std::uint64_t current = 0;
  if (function != 0 && function != 7) {
    const auto alignment = is_64_bit ? 8U : 4U;
    if (address == 0 || (address & (alignment - 1U)) != 0 || mask == 0) {
      return Stop(context, GpuCommandStatus::kMalformedPacket,
                  packet_address, opcode);
    }
    const auto value = ReadGuestValue(context, address, is_64_bit);
    if (!value.has_value()) {
      return Stop(context, GpuCommandStatus::kMemoryFault, packet_address,
                  opcode);
    }
    current = *value;
  }
  const auto action = MakeAction(
      GpuActionType::kWaitMemory, packet_address, packet_dwords, opcode,
      packet_register,
      {address, mask, reference, current, function, is_64_bit ? 64ULL : 32ULL,
       control, poll});
  const auto emit_status = Emit(context, action);
  if (emit_status != GpuCommandStatus::kComplete) {
    return emit_status;
  }
  if (!CompareWait(current, reference, mask, function)) {
    context.cursor.pending_wait = GpuCommandPendingWait{
        packet_address, address, mask, reference, packet_dwords,
        opcode, function, is_64_bit};
    return Stop(context, GpuCommandStatus::kBlocked, packet_address, opcode);
  }
  return GpuCommandStatus::kComplete;
}

GpuCommandStatus SnapshotFrame(DecodeContext& context,
                               std::uint64_t address,
                               std::uint32_t dword_count) {
  const auto depth = context.cursor.frames.size();
  if (depth > context.cursor.limits.max_indirect_depth) {
    return Stop(context, GpuCommandStatus::kResourceLimit, address,
                kPm4IndirectBuffer);
  }
  for (const auto& frame : context.cursor.frames) {
    if (frame.address == address && frame.dword_count == dword_count) {
      return Stop(context, GpuCommandStatus::kResourceLimit, address,
                  kPm4IndirectBuffer);
    }
  }
  GpuCommandFrame frame;
  frame.address = address;
  frame.dword_count = dword_count;
  const auto status = ReadDwords(context, address, dword_count, frame.words);
  if (status != GpuCommandStatus::kComplete) {
    return status;
  }
  try {
    context.cursor.frames.push_back(std::move(frame));
  } catch (...) {
    return Stop(context, GpuCommandStatus::kResourceLimit, address);
  }
  return GpuCommandStatus::kComplete;
}

GpuCommandStatus ResumePendingWait(DecodeContext& context) noexcept {
  if (!context.cursor.pending_wait.has_value()) {
    return GpuCommandStatus::kComplete;
  }
  const auto wait = *context.cursor.pending_wait;
  const auto value = ReadGuestValue(context, wait.address, wait.is_64_bit);
  if (!value.has_value()) {
    return Stop(context, GpuCommandStatus::kMemoryFault,
                wait.packet_address, wait.opcode);
  }
  if (!CompareWait(*value, wait.reference, wait.mask, wait.function)) {
    return Stop(context, GpuCommandStatus::kBlocked, wait.packet_address,
                wait.opcode);
  }
  if (context.cursor.frames.empty() ||
      context.cursor.frames.back().offset >
          context.cursor.frames.back().dword_count - wait.packet_dwords) {
    return Stop(context, GpuCommandStatus::kMalformedPacket,
                wait.packet_address, wait.opcode);
  }
  context.cursor.frames.back().offset += wait.packet_dwords;
  context.cursor.pending_wait.reset();
  return GpuCommandStatus::kComplete;
}

GpuCommandStatus ProcessIndirectBuffer(
    DecodeContext& context, std::uint64_t packet_address,
    std::uint32_t packet_dwords, std::uint32_t opcode,
    std::uint32_t packet_register, std::uint64_t target,
    std::uint32_t target_dwords, std::uint32_t control,
    std::uint32_t depth, std::uint64_t branch_value = 0) {
  const auto action = MakeAction(
      GpuActionType::kIndirectBuffer, packet_address, packet_dwords, opcode,
      packet_register,
      {target, target_dwords, control, depth, branch_value});
  const auto emit_status = Emit(context, action);
  if (emit_status != GpuCommandStatus::kComplete || target_dwords == 0) {
    return emit_status;
  }
  return SnapshotFrame(context, target, target_dwords);
}

GpuCommandStatus ProcessFrames(DecodeContext& context) {
  const auto wait_status = ResumePendingWait(context);
  if (wait_status != GpuCommandStatus::kComplete) {
    return wait_status;
  }
  while (!context.cursor.frames.empty()) {
    const auto frame_index = context.cursor.frames.size() - 1U;
    if (context.cursor.frames[frame_index].offset ==
        context.cursor.frames[frame_index].dword_count) {
      context.cursor.frames.pop_back();
      continue;
    }
    const auto address = context.cursor.frames[frame_index].address;
    const auto dword_count = context.cursor.frames[frame_index].dword_count;
    const auto offset = context.cursor.frames[frame_index].offset;
    const auto depth = static_cast<std::uint32_t>(frame_index);
    const auto& words = context.cursor.frames[frame_index].words;
    std::uint64_t packet_address = 0;
    if (!Add(address, static_cast<std::uint64_t>(offset) * 4U,
             packet_address)) {
      return Stop(context, GpuCommandStatus::kMemoryFault, address);
    }
    const auto header = words[offset];
    const auto packet_type = header & kPm4TypeMask;
    if (packet_type == kPm4Type2) {
      if (context.cursor.result.processed_dwords >=
          context.cursor.limits.max_processed_dwords) {
        return Stop(context, GpuCommandStatus::kResourceLimit,
                    packet_address);
      }
      ++context.cursor.result.processed_dwords;
      const auto status = Emit(context, MakeAction(
          GpuActionType::kNop, packet_address, 1, 0, 0));
      if (status != GpuCommandStatus::kComplete) {
        return status;
      }
      ++context.cursor.frames[frame_index].offset;
      continue;
    }
    if (packet_type != kPm4Type3) {
      return Stop(context, GpuCommandStatus::kMalformedPacket,
                  packet_address);
    }

    const auto opcode = (header >> 8U) & 0xffU;
    const auto packet_register = (header >> 2U) & 0x3fU;
    const auto single_dword_custom =
        (header & 0x3fffff00U) == 0x3fff1000U;
    const auto packet_dwords = single_dword_custom
                                   ? 1U
                                   : ((header >> 16U) & 0x3fffU) + 2U;
    if (packet_dwords == 0 || packet_dwords > dword_count - offset) {
      return Stop(context, GpuCommandStatus::kMalformedPacket,
                  packet_address, opcode);
    }
    if (packet_dwords > context.cursor.limits.max_processed_dwords -
                            context.cursor.result.processed_dwords) {
      return Stop(context, GpuCommandStatus::kResourceLimit,
                  packet_address, opcode);
    }
    context.cursor.result.processed_dwords += packet_dwords;
    const std::span<const std::uint32_t> packet(words.data() + offset,
                                                packet_dwords);
    GpuCommandStatus status = GpuCommandStatus::kComplete;

    if (opcode == kPm4Nop && packet_register == 0) {
      status = Emit(context, MakeAction(GpuActionType::kNop, packet_address,
                                        packet_dwords, opcode,
                                        packet_register));
    } else if (opcode == kPm4Nop &&
               (packet_register == kPm4WaitMemory32Register ||
                packet_register == kPm4WaitMemory64Register)) {
      const auto is_64_bit = packet_register == kPm4WaitMemory64Register;
      const auto expected = is_64_bit ? 9U : 7U;
      if (packet_dwords != expected) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto wait_address = static_cast<std::uint64_t>(packet[1]) |
                                (static_cast<std::uint64_t>(packet[2]) << 32U);
      const auto mask = is_64_bit
                            ? static_cast<std::uint64_t>(packet[3]) |
                                  (static_cast<std::uint64_t>(packet[4]) << 32U)
                            : packet[3];
      const auto reference =
          is_64_bit ? static_cast<std::uint64_t>(packet[5]) |
                          (static_cast<std::uint64_t>(packet[6]) << 32U)
                    : packet[4];
      const auto control = packet[is_64_bit ? 7U : 5U];
      const auto poll = packet[is_64_bit ? 8U : 6U];
      status = ProcessWait(context, packet_address, packet_dwords, opcode,
                           packet_register, wait_address, mask, reference,
                           control, poll, is_64_bit);
    } else if (opcode == kPm4WaitRegMem) {
      if (packet_dwords < 7) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto wait_address = static_cast<std::uint64_t>(packet[2]) |
                                (static_cast<std::uint64_t>(packet[3]) << 32U);
      status = ProcessWait(context, packet_address, packet_dwords, opcode,
                           packet_register, wait_address, packet[5], packet[4],
                           packet[1], packet[6], false);
    } else if (opcode == kPm4Nop &&
               (packet_register == kPm4ContextRegistersIndirect ||
                packet_register == kPm4ShaderRegistersIndirect ||
                packet_register == kPm4UserConfigRegistersIndirect)) {
      if (packet_dwords != 4) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto space = packet_register == kPm4ContextRegistersIndirect
                             ? GpuRegisterSpace::kContext
                         : packet_register == kPm4ShaderRegistersIndirect
                             ? GpuRegisterSpace::kShader
                             : GpuRegisterSpace::kUserConfig;
      const auto table_address = static_cast<std::uint64_t>(packet[2]) |
                                 (static_cast<std::uint64_t>(packet[3]) << 32U);
      status = ApplyIndirectRegisters(
          context, space, packet_address, packet_dwords, opcode,
          packet_register, table_address, packet[1]);
    } else if (opcode == kPm4Nop &&
               packet_register == kPm4WriteDataRegister) {
      status = ProcessWriteData(context, packet_address, packet_dwords,
                                opcode, packet_register, packet);
    } else if (opcode == kPm4Nop &&
               packet_register == kPm4ReleaseMemoryRegister) {
      status = ProcessReleaseMemory(context, packet_address, packet_dwords,
                                    opcode, packet_register, packet);
    } else if (opcode == kPm4SetContextRegister ||
               opcode == kPm4SetShRegister ||
               opcode == kPm4SetUcRegister ||
               opcode == kPm4SetUcRegisterIndex) {
      const auto space = opcode == kPm4SetContextRegister
                             ? GpuRegisterSpace::kContext
                         : opcode == kPm4SetShRegister
                             ? GpuRegisterSpace::kShader
                             : GpuRegisterSpace::kUserConfig;
      status = ApplyDirectRegisters(context, space, packet_address,
                                    packet_dwords, opcode, packet_register,
                                    packet);
    } else if (opcode == kPm4SetContextRegisterIndirect ||
               opcode == kPm4SetShRegisterIndirect ||
               opcode == kPm4SetUcRegisterIndirect) {
      if (packet_dwords != 5) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto space = opcode == kPm4SetContextRegisterIndirect
                             ? GpuRegisterSpace::kContext
                         : opcode == kPm4SetShRegisterIndirect
                             ? GpuRegisterSpace::kShader
                             : GpuRegisterSpace::kUserConfig;
      const auto table_address = static_cast<std::uint64_t>(packet[1] & ~3U) |
                                 (static_cast<std::uint64_t>(packet[2]) << 32U);
      status = ApplyIndirectRegisters(
          context, space, packet_address, packet_dwords, opcode,
          packet_register, table_address, packet[4] & 0x3fffU);
    } else if (opcode == kPm4SetBase) {
      if (packet_dwords < 4) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto base = static_cast<std::uint64_t>(packet[2]) |
                        (static_cast<std::uint64_t>(packet[3]) << 32U);
      status = Emit(context, MakeAction(
          GpuActionType::kSetBase, packet_address, packet_dwords, opcode,
          packet_register, {packet[1], base, (header >> 1U) & 1U}));
    } else if (opcode == kPm4IndexBase) {
      if (packet_dwords < 3) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto index_address = static_cast<std::uint64_t>(packet[1]) |
                                 (static_cast<std::uint64_t>(packet[2]) << 32U);
      status = Emit(context, MakeAction(
          GpuActionType::kSetIndexBuffer, packet_address, packet_dwords,
          opcode, packet_register, {index_address}));
    } else if (opcode == kPm4IndexBufferSize || opcode == kPm4IndexType) {
      if (packet_dwords < 2) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      status = Emit(context, MakeAction(
          opcode == kPm4IndexType ? GpuActionType::kSetIndexSize
                                  : GpuActionType::kSetIndexCount,
          packet_address, packet_dwords, opcode, packet_register,
          {packet[1]}));
    } else if (opcode == kPm4NumInstances) {
      if (packet_dwords < 2) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      // DrawIndexAuto consumes this retained scalar.  Keep it in the
      // decoder-owned state map so the ensuing action snapshots it exactly.
      context.user_config_registers[kSyntheticDrawInstanceCount] =
          std::max(packet[1], 1U);
      status = Emit(context, MakeAction(
          GpuActionType::kSetNumInstances, packet_address, packet_dwords,
          opcode, packet_register, {std::max(packet[1], 1U)}));
    } else if (opcode == kPm4DispatchDirect) {
      if (packet_dwords != 5) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      status = Emit(context, MakeAction(
          GpuActionType::kDispatch, packet_address, packet_dwords, opcode,
          packet_register, {packet[1], packet[2], packet[3], packet[4], 0}));
    } else if (opcode == kPm4DispatchIndirect) {
      if (packet_dwords != 3 && packet_dwords != 4) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto indirect = packet_dwords == 4
                                ? static_cast<std::uint64_t>(packet[1]) |
                                      (static_cast<std::uint64_t>(packet[2]) <<
                                       32U)
                                : packet[1];
      status = Emit(context, MakeAction(
          GpuActionType::kDispatch, packet_address, packet_dwords, opcode,
          packet_register,
          {indirect, packet[packet_dwords - 1U],
           packet_dwords == 4 ? 1ULL : 0ULL}));
    } else if (opcode == kPm4DrawIndex ||
               opcode == kPm4DrawIndexMultiInstanced ||
               opcode == kPm4DrawIndexAuto ||
               opcode == kPm4DrawIndexOffset ||
               opcode == kPm4DrawIndirect ||
               opcode == kPm4DrawIndexIndirect ||
               opcode == kPm4DrawIndirectMulti ||
               opcode == kPm4DrawIndexIndirectMulti) {
      if (packet_dwords < 2) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      GpuAction action = MakeAction(GpuActionType::kDraw, packet_address,
                                    packet_dwords, opcode, packet_register);
      action.value_count = std::min<std::size_t>(packet_dwords - 1U,
                                                 action.values.size());
      for (std::size_t index = 0; index < action.value_count; ++index) {
        action.values[index] = packet[index + 1U];
      }
      status = Emit(context, action);
    } else if (opcode == kPm4SetPredication) {
      if (packet_dwords < 4) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto predicate_address =
          static_cast<std::uint64_t>(packet[2] & ~0xfU) |
          (static_cast<std::uint64_t>(packet[3]) << 32U);
      status = Emit(context, MakeAction(
          GpuActionType::kPredication, packet_address, packet_dwords, opcode,
          packet_register, {predicate_address, packet[1]}));
    } else if (opcode == kPm4WriteData) {
      status = ProcessWriteData(context, packet_address, packet_dwords,
                                opcode, packet_register, packet);
    } else if (opcode == kPm4ReleaseMemory) {
      status = ProcessReleaseMemory(context, packet_address, packet_dwords,
                                    opcode, packet_register, packet);
    } else if (opcode == kPm4EventWrite) {
      status = ProcessEventWrite(context, packet_address, packet_dwords,
                                 opcode, packet_register, packet);
    } else if (opcode == kPm4IndirectBuffer) {
      if (packet_dwords == 4) {
        const auto target = static_cast<std::uint64_t>(packet[1] & ~3U) |
                            (static_cast<std::uint64_t>(packet[2]) << 32U);
        status = ProcessIndirectBuffer(
            context, packet_address, packet_dwords, opcode, packet_register,
            target, packet[3] & 0xfffffU, packet[3], depth);
      } else if (packet_dwords == 14) {
        const auto compare_address =
            static_cast<std::uint64_t>(packet[2] & ~7U) |
            (static_cast<std::uint64_t>(packet[3]) << 32U);
        const auto mask = static_cast<std::uint64_t>(packet[4]) |
                          (static_cast<std::uint64_t>(packet[5]) << 32U);
        const auto reference = static_cast<std::uint64_t>(packet[6]) |
                               (static_cast<std::uint64_t>(packet[7]) << 32U);
        const auto mode = packet[1] & 3U;
        const auto function = (packet[1] >> 8U) & 7U;
        if (compare_address == 0 || (compare_address & 7U) != 0 ||
            mask == 0 || function > 6 || (mode != 1 && mode != 2)) {
          return Stop(context, GpuCommandStatus::kMalformedPacket,
                      packet_address, opcode);
        }
        const auto value = ReadGuestValue(context, compare_address, true);
        if (!value.has_value()) {
          return Stop(context, GpuCommandStatus::kMemoryFault,
                      packet_address, opcode);
        }
        const auto take_then = CompareWait(*value, reference, mask, function);
        const auto then_target = static_cast<std::uint64_t>(packet[8] & ~3U) |
                                 (static_cast<std::uint64_t>(packet[9]) << 32U);
        const auto else_target = static_cast<std::uint64_t>(packet[11] & ~3U) |
                                 (static_cast<std::uint64_t>(packet[12]) << 32U);
        const auto target = take_then ? then_target : else_target;
        const auto target_dwords =
            take_then ? packet[10] & 0xfffffU : packet[13] & 0xfffffU;
        status = ProcessIndirectBuffer(
            context, packet_address, packet_dwords, opcode, packet_register,
            target, mode == 2 || take_then ? target_dwords : 0, packet[1],
            depth, take_then ? 1 : 0);
      } else {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
    } else if (opcode == kPm4Rewind) {
      if (packet_dwords < 2) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      status = Emit(context, MakeAction(
          GpuActionType::kRewind, packet_address, packet_dwords, opcode,
          packet_register, {packet[1]}));
    } else if (opcode == kPm4GetLodStats) {
      if (packet_dwords != 5) {
        return Stop(context, GpuCommandStatus::kMalformedPacket,
                    packet_address, opcode);
      }
      const auto destination = static_cast<std::uint64_t>(packet[2]) |
                               (static_cast<std::uint64_t>(packet[3]) << 32U);
      status = Emit(context, MakeAction(
          GpuActionType::kLodStats, packet_address, packet_dwords, opcode,
          packet_register, {destination, packet[1], packet[4]}));
    } else {
      return Stop(context, GpuCommandStatus::kUnsupportedPacket,
                  packet_address, opcode);
    }

    if (status != GpuCommandStatus::kComplete) {
      return status;
    }
    context.cursor.frames[frame_index].offset += packet_dwords;
  }
  return GpuCommandStatus::kComplete;
}

}  // namespace

GpuActionTrace::GpuActionTrace(std::size_t capacity) noexcept
    : capacity_(capacity) {
  try {
    actions_.reserve(capacity_);
  } catch (...) {
    capacity_ = 0;
  }
}

GpuCommandStatus GpuActionTrace::Submit(
    const GpuAction& action) noexcept {
  if (actions_.size() >= capacity_) {
    return GpuCommandStatus::kResourceLimit;
  }
  try {
    actions_.push_back(action);
  } catch (...) {
    return GpuCommandStatus::kResourceLimit;
  }
  return GpuCommandStatus::kComplete;
}

std::span<const GpuAction> GpuActionTrace::actions() const noexcept {
  return actions_;
}

void GpuActionTrace::Clear() noexcept { actions_.clear(); }

GpuActionRing::GpuActionRing(std::size_t capacity) noexcept
    : capacity_(capacity) {
  try {
    actions_.reserve(capacity_);
  } catch (...) {
    capacity_ = 0;
  }
}

GpuCommandStatus GpuActionRing::Submit(
    const GpuAction& action) noexcept {
  if (capacity_ == 0) {
    return GpuCommandStatus::kResourceLimit;
  }
  if (actions_.size() < capacity_) {
    try {
      actions_.push_back(action);
    } catch (...) {
      return GpuCommandStatus::kResourceLimit;
    }
    return GpuCommandStatus::kComplete;
  }
  try {
    actions_[next_] = action;
  } catch (...) {
    return GpuCommandStatus::kResourceLimit;
  }
  next_ = (next_ + 1U) % capacity_;
  ++dropped_count_;
  return GpuCommandStatus::kComplete;
}

std::size_t GpuActionRing::size() const noexcept { return actions_.size(); }

std::uint64_t GpuActionRing::dropped_count() const noexcept {
  return dropped_count_;
}

const GpuAction* GpuActionRing::At(std::size_t index) const noexcept {
  if (index >= actions_.size()) {
    return nullptr;
  }
  const auto physical = actions_.size() < capacity_
                            ? index
                            : (next_ + index) % capacity_;
  return &actions_[physical];
}

void GpuActionRing::Clear() noexcept {
  actions_.clear();
  next_ = 0;
  dropped_count_ = 0;
}

GpuEventSubmissionSink::GpuEventSubmissionSink(
    kernel::EventQueueService* event_queues,
    GpuSubmissionSink& downstream) noexcept
    : event_queues_(event_queues), downstream_(downstream) {}

GpuCommandStatus GpuEventSubmissionSink::Submit(
    const GpuAction& action) noexcept {
  if (action.type != GpuActionType::kEventWrite) {
    return downstream_.Submit(action);
  }
  if (action.value_count < 1 || action.values[0] > 0x3fU) {
    return GpuCommandStatus::kMalformedPacket;
  }
  if (event_queues_ == nullptr) {
    return GpuCommandStatus::kUnsupportedPacket;
  }
  const auto downstream_status = downstream_.Submit(action);
  if (downstream_status != GpuCommandStatus::kComplete) {
    return downstream_status;
  }
  (void)event_queues_->TriggerGraphicsEvents(action.values[0]);
  return GpuCommandStatus::kComplete;
}

GpuMemorySubmissionSink::GpuMemorySubmissionSink(
    memory::GuestMemory& memory, GpuSubmissionSink& downstream) noexcept
    : memory_(memory), downstream_(downstream) {}

GpuCommandStatus GpuMemorySubmissionSink::Submit(
    const GpuAction& action) noexcept {
  if (action.type != GpuActionType::kWriteData &&
      action.type != GpuActionType::kReleaseMemory) {
    return downstream_.Submit(action);
  }
  if (action.type == GpuActionType::kReleaseMemory) {
    if (action.value_count < 5) {
      return GpuCommandStatus::kMalformedPacket;
    }
    const auto standard_packet = action.opcode == kPm4ReleaseMemory;
    const auto wrapper_packet = action.opcode == kPm4Nop &&
                                action.packet_register ==
                                    kPm4ReleaseMemoryRegister;
    if (!standard_packet && !wrapper_packet) {
      return GpuCommandStatus::kMalformedPacket;
    }

    const auto action_control =
        static_cast<std::uint32_t>(action.values[2]);
    const auto release_control =
        static_cast<std::uint32_t>(action.values[3]);
    const auto kyty_wrapper = wrapper_packet &&
                              (((action_control >> 8U) & 7U) == 5U ||
                               ((action_control >> 8U) & 7U) == 6U);
    const auto destination =
        standard_packet || kyty_wrapper
            ? (release_control >> 16U) & 3U
            : 0U;
    const auto data_selection =
        standard_packet || kyty_wrapper
            ? (release_control >> 29U) & 7U
            : (release_control >> 16U) & 0xffU;
    if (data_selection > 5 ||
        (!standard_packet && data_selection == 4)) {
      return GpuCommandStatus::kMalformedPacket;
    }
    if (data_selection == 5) {
      return GpuCommandStatus::kUnsupportedPacket;
    }
    if (data_selection == 0 || destination > 1 ||
        action.values[0] == 0) {
      return downstream_.Submit(action);
    }
    if ((action.values[0] & 3U) != 0) {
      return GpuCommandStatus::kMalformedPacket;
    }

    const auto downstream_status = downstream_.Submit(action);
    if (downstream_status != GpuCommandStatus::kComplete) {
      return downstream_status;
    }
    if (data_selection == 1) {
      std::array<std::byte, sizeof(std::uint32_t)> bytes{};
      Write32(bytes, 0, static_cast<std::uint32_t>(action.values[1]));
      return memory_.Write(action.values[0], bytes)
                 ? GpuCommandStatus::kComplete
                 : GpuCommandStatus::kMemoryFault;
    }
    std::array<std::byte, sizeof(std::uint64_t)> bytes{};
    const auto value = data_selection == 2
                           ? action.values[1]
                           : next_timestamp_.fetch_add(
                                 1, std::memory_order_relaxed);
    Write64(bytes, value);
    return memory_.Write(action.values[0], bytes)
               ? GpuCommandStatus::kComplete
               : GpuCommandStatus::kMemoryFault;
  }
  if (action.value_count < 3 ||
      action.payload.size() != action.values[2]) {
    return GpuCommandStatus::kMalformedPacket;
  }

  const auto destination_address = action.values[0];
  const auto control = static_cast<std::uint32_t>(action.values[1]);
  const auto standard_packet = action.opcode == kPm4WriteData;
  const auto agc_packet = action.opcode == kPm4Nop &&
                          action.packet_register == kPm4WriteDataRegister;
  if (!standard_packet && !agc_packet) {
    return GpuCommandStatus::kMalformedPacket;
  }
  const auto destination = standard_packet ? (control >> 8U) & 0xfU
                                           : control & 0xffU;
  if (destination != 1U && destination != 2U && destination != 4U &&
      destination != 5U) {
    return downstream_.Submit(action);
  }
  if (destination_address == 0 || (destination_address & 3U) != 0) {
    return GpuCommandStatus::kMalformedPacket;
  }
  const auto downstream_status = downstream_.Submit(action);
  if (downstream_status != GpuCommandStatus::kComplete) {
    return downstream_status;
  }
  if (action.payload.empty()) {
    return GpuCommandStatus::kComplete;
  }

  const auto increment_address = standard_packet
                                     ? (control & (1U << 16U)) == 0
                                     : ((control >> 16U) & 0xffU) == 0;
  if (!increment_address) {
    std::array<std::byte, sizeof(std::uint32_t)> bytes{};
    Write32(bytes, 0, action.payload.back());
    return memory_.Write(destination_address, bytes)
               ? GpuCommandStatus::kComplete
               : GpuCommandStatus::kMemoryFault;
  }

  std::vector<std::byte> bytes;
  try {
    bytes.resize(action.payload.size() * sizeof(std::uint32_t));
  } catch (...) {
    return GpuCommandStatus::kResourceLimit;
  }
  for (std::size_t index = 0; index < action.payload.size(); ++index) {
    Write32(bytes, index * sizeof(std::uint32_t), action.payload[index]);
  }
  return memory_.Write(destination_address, bytes)
             ? GpuCommandStatus::kComplete
             : GpuCommandStatus::kMemoryFault;
}

GpuCommandResult GpuRuntime::ProcessCommandBuffer(
    std::uint64_t address, std::uint32_t dword_count,
    GpuSubmissionSink& sink, GpuCommandLimits limits) {
  auto cursor = BeginCommandBuffer(address, dword_count, limits);
  return ResumeCommandBuffer(cursor, sink);
}

GpuCommandCursor GpuRuntime::BeginCommandBuffer(
    std::uint64_t address, std::uint32_t dword_count,
    GpuCommandLimits limits) {
  GpuCommandCursor cursor;
  cursor.limits = limits;
  if (address == 0 || (address & 3U) != 0 || dword_count == 0 ||
      limits.max_actions == 0 || limits.max_buffer_dwords == 0 ||
      limits.max_processed_dwords == 0) {
    cursor.result = {GpuCommandStatus::kInvalidArgument, address};
    cursor.terminal = true;
    return cursor;
  }
  std::lock_guard lock(mutex_);
  GpuActionTrace unused_sink(0);
  DecodeContext context{memory_,
                        unused_sink,
                        cursor,
                        shader_runtime_,
                        context_registers_,
                        shader_registers_,
                        user_config_registers_};
  try {
    cursor.result.status = SnapshotFrame(context, address, dword_count);
  } catch (...) {
    cursor.result.status = GpuCommandStatus::kResourceLimit;
    cursor.result.packet_address = address;
  }
  cursor.terminal = cursor.result.status != GpuCommandStatus::kComplete;
  return cursor;
}

GpuCommandResult GpuRuntime::ResumeCommandBuffer(
    GpuCommandCursor& cursor, GpuSubmissionSink& sink) {
  if (cursor.terminal) {
    return cursor.result;
  }
  std::lock_guard lock(mutex_);
  DecodeContext context{memory_, sink, cursor, shader_runtime_,
                        context_registers_, shader_registers_,
                        user_config_registers_};
  cursor.result.status = GpuCommandStatus::kComplete;
  cursor.result.packet_address = 0;
  cursor.result.opcode = 0;
  try {
    cursor.result.status = ProcessFrames(context);
  } catch (...) {
    cursor.result.status = GpuCommandStatus::kResourceLimit;
    cursor.result.packet_address = cursor.frames.empty()
                                       ? 0
                                       : cursor.frames.back().address;
  }
  cursor.terminal = cursor.result.status != GpuCommandStatus::kBlocked;
  return cursor.result;
}

std::optional<std::uint32_t> GpuRuntime::ReadRegister(
    GpuRegisterSpace space, std::uint32_t offset) const noexcept {
  std::lock_guard lock(mutex_);
  const RegisterMap* registers = &context_registers_;
  if (space == GpuRegisterSpace::kShader) {
    registers = &shader_registers_;
  } else if (space == GpuRegisterSpace::kUserConfig) {
    registers = &user_config_registers_;
  }
  const auto found = registers->find(NormalizeRegisterOffset(offset));
  return found == registers->end()
             ? std::nullopt
             : std::optional<std::uint32_t>(found->second);
}

const char* GpuCommandStatusName(GpuCommandStatus status) noexcept {
  switch (status) {
    case GpuCommandStatus::kComplete:
      return "complete";
    case GpuCommandStatus::kBlocked:
      return "blocked";
    case GpuCommandStatus::kInvalidArgument:
      return "invalid-argument";
    case GpuCommandStatus::kMemoryFault:
      return "memory-fault";
    case GpuCommandStatus::kMalformedPacket:
      return "malformed-packet";
    case GpuCommandStatus::kUnsupportedPacket:
      return "unsupported-packet";
    case GpuCommandStatus::kResourceLimit:
      return "resource-limit";
  }
  return "unknown";
}

}  // namespace kajps5::gpu
