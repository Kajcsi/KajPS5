// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 renderer/pipeline/{pipelineCache,renderDraw}.*
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior reference: SharpEmu VulkanVideoPresenter.cs at
// 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/graphics_execution.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>

#include "gpu/vulkan/device.h"

namespace kajps5::gpu::vulkan {
namespace {

constexpr std::size_t kMaximumRetainedSubmissions = 8;
constexpr std::size_t kMaximumPipelines = 32;

struct Dispatch {
  PFN_vkCreateCommandPool create_command_pool = nullptr;
  PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
  PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
  PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
  PFN_vkEndCommandBuffer end_command_buffer = nullptr;
  PFN_vkCreateFence create_fence = nullptr;
  PFN_vkDestroyFence destroy_fence = nullptr;
  PFN_vkWaitForFences wait_for_fences = nullptr;
  PFN_vkGetFenceStatus get_fence_status = nullptr;
  PFN_vkQueueSubmit queue_submit = nullptr;
  PFN_vkCreateShaderModule create_shader_module = nullptr;
  PFN_vkDestroyShaderModule destroy_shader_module = nullptr;
  PFN_vkCreatePipelineLayout create_pipeline_layout = nullptr;
  PFN_vkDestroyPipelineLayout destroy_pipeline_layout = nullptr;
  PFN_vkCreateGraphicsPipelines create_graphics_pipelines = nullptr;
  PFN_vkDestroyPipeline destroy_pipeline = nullptr;
  PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
  PFN_vkCmdSetViewport cmd_set_viewport = nullptr;
  PFN_vkCmdSetScissor cmd_set_scissor = nullptr;
  PFN_vkCmdBeginRendering cmd_begin_rendering = nullptr;
  PFN_vkCmdEndRendering cmd_end_rendering = nullptr;
  PFN_vkCmdDraw cmd_draw = nullptr;
};

template <typename T>
T Resolve(VulkanDeviceContext& context, const char* name) {
  return reinterpret_cast<T>(context.ResolveDeviceFunction(name));
}

bool Load(VulkanDeviceContext& context, Dispatch& dispatch) {
  dispatch.create_command_pool =
      Resolve<PFN_vkCreateCommandPool>(context, "vkCreateCommandPool");
  dispatch.destroy_command_pool =
      Resolve<PFN_vkDestroyCommandPool>(context, "vkDestroyCommandPool");
  dispatch.allocate_command_buffers = Resolve<PFN_vkAllocateCommandBuffers>(
      context, "vkAllocateCommandBuffers");
  dispatch.begin_command_buffer =
      Resolve<PFN_vkBeginCommandBuffer>(context, "vkBeginCommandBuffer");
  dispatch.end_command_buffer =
      Resolve<PFN_vkEndCommandBuffer>(context, "vkEndCommandBuffer");
  dispatch.create_fence = Resolve<PFN_vkCreateFence>(context, "vkCreateFence");
  dispatch.destroy_fence =
      Resolve<PFN_vkDestroyFence>(context, "vkDestroyFence");
  dispatch.wait_for_fences =
      Resolve<PFN_vkWaitForFences>(context, "vkWaitForFences");
  dispatch.get_fence_status =
      Resolve<PFN_vkGetFenceStatus>(context, "vkGetFenceStatus");
  dispatch.queue_submit = Resolve<PFN_vkQueueSubmit>(context, "vkQueueSubmit");
  dispatch.create_shader_module =
      Resolve<PFN_vkCreateShaderModule>(context, "vkCreateShaderModule");
  dispatch.destroy_shader_module =
      Resolve<PFN_vkDestroyShaderModule>(context, "vkDestroyShaderModule");
  dispatch.create_pipeline_layout =
      Resolve<PFN_vkCreatePipelineLayout>(context, "vkCreatePipelineLayout");
  dispatch.destroy_pipeline_layout = Resolve<PFN_vkDestroyPipelineLayout>(
      context, "vkDestroyPipelineLayout");
  dispatch.create_graphics_pipelines = Resolve<PFN_vkCreateGraphicsPipelines>(
      context, "vkCreateGraphicsPipelines");
  dispatch.destroy_pipeline =
      Resolve<PFN_vkDestroyPipeline>(context, "vkDestroyPipeline");
  dispatch.cmd_bind_pipeline =
      Resolve<PFN_vkCmdBindPipeline>(context, "vkCmdBindPipeline");
  dispatch.cmd_set_viewport =
      Resolve<PFN_vkCmdSetViewport>(context, "vkCmdSetViewport");
  dispatch.cmd_set_scissor =
      Resolve<PFN_vkCmdSetScissor>(context, "vkCmdSetScissor");
  dispatch.cmd_begin_rendering =
      Resolve<PFN_vkCmdBeginRendering>(context, "vkCmdBeginRendering");
  dispatch.cmd_end_rendering =
      Resolve<PFN_vkCmdEndRendering>(context, "vkCmdEndRendering");
  dispatch.cmd_draw = Resolve<PFN_vkCmdDraw>(context, "vkCmdDraw");

  return dispatch.create_command_pool && dispatch.destroy_command_pool &&
         dispatch.allocate_command_buffers && dispatch.begin_command_buffer &&
         dispatch.end_command_buffer && dispatch.create_fence &&
         dispatch.destroy_fence && dispatch.wait_for_fences &&
         dispatch.get_fence_status && dispatch.queue_submit &&
         dispatch.create_shader_module && dispatch.destroy_shader_module &&
         dispatch.create_pipeline_layout && dispatch.destroy_pipeline_layout &&
         dispatch.create_graphics_pipelines && dispatch.destroy_pipeline &&
         dispatch.cmd_bind_pipeline && dispatch.cmd_set_viewport &&
         dispatch.cmd_set_scissor && dispatch.cmd_begin_rendering &&
         dispatch.cmd_end_rendering && dispatch.cmd_draw;
}

void Add(VulkanGraphicsResult& result, VulkanGraphicsDiagnosticSeverity severity,
         VulkanGraphicsDiagnosticCode code, std::string message,
         std::uint64_t timeline = 0, VkResult api_result = VK_SUCCESS) {
  result.diagnostics.push_back(
      {severity, code, timeline, static_cast<std::int32_t>(api_result),
       std::move(message)});
}

bool IsDeviceLost(VkResult result) noexcept {
  return result == VK_ERROR_DEVICE_LOST;
}

VkPrimitiveTopology ToTopology(VulkanGraphicsTopology topology) noexcept {
  switch (topology) {
    case VulkanGraphicsTopology::kTriangleList:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case VulkanGraphicsTopology::kTriangleStrip:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case VulkanGraphicsTopology::kLineList:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case VulkanGraphicsTopology::kLineStrip:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case VulkanGraphicsTopology::kPointList:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  }
  return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

VkCullModeFlags ToCullMode(VulkanGraphicsCullMode cull_mode) noexcept {
  switch (cull_mode) {
    case VulkanGraphicsCullMode::kNone:
      return VK_CULL_MODE_NONE;
    case VulkanGraphicsCullMode::kFront:
      return VK_CULL_MODE_FRONT_BIT;
    case VulkanGraphicsCullMode::kBack:
      return VK_CULL_MODE_BACK_BIT;
  }
  return VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
}

VkFrontFace ToFrontFace(VulkanGraphicsFrontFace front_face) noexcept {
  return front_face == VulkanGraphicsFrontFace::kClockwise
             ? VK_FRONT_FACE_CLOCKWISE
             : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

bool IsDescriptorFree(const shader::recompiler::CompileResult& result) {
  const auto& info = result.program.info;
  return result.program.bindings.ShaderDataDwords() == 0 &&
         info.buffers.empty() && info.addresses.empty() && info.images.empty() &&
         info.samplers.empty() && result.resources.buffers.empty() &&
         result.resources.images.empty() && result.resources.samplers.empty() &&
         result.resources.addresses.empty() && result.resources.user_data.empty();
}

bool IsValidViewport(const VulkanGraphicsViewportState& viewport) noexcept {
  return std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
         std::isfinite(viewport.width) && std::isfinite(viewport.height) &&
         std::isfinite(viewport.min_depth) && std::isfinite(viewport.max_depth) &&
         viewport.width > 0.0f && viewport.height > 0.0f &&
         viewport.min_depth >= 0.0f && viewport.max_depth <= 1.0f &&
         viewport.min_depth <= viewport.max_depth && viewport.scissor.extent.width &&
         viewport.scissor.extent.height;
}

bool IsValidBlend(const VulkanGraphicsBlendState& blend) noexcept {
  const auto mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  return blend.source_color != VK_BLEND_FACTOR_MAX_ENUM &&
         blend.destination_color != VK_BLEND_FACTOR_MAX_ENUM &&
         blend.color_op != VK_BLEND_OP_MAX_ENUM &&
         blend.source_alpha != VK_BLEND_FACTOR_MAX_ENUM &&
         blend.destination_alpha != VK_BLEND_FACTOR_MAX_ENUM &&
         blend.alpha_op != VK_BLEND_OP_MAX_ENUM && (blend.write_mask & ~mask) == 0;
}

bool EqualBlend(const VulkanGraphicsBlendState& left,
                const VulkanGraphicsBlendState& right) noexcept {
  return left.enabled == right.enabled &&
         left.source_color == right.source_color &&
         left.destination_color == right.destination_color &&
         left.color_op == right.color_op && left.source_alpha == right.source_alpha &&
         left.destination_alpha == right.destination_alpha &&
         left.alpha_op == right.alpha_op && left.write_mask == right.write_mask;
}

}  // namespace

struct VulkanGraphicsExecution::Impl {
  struct Pipeline {
    std::vector<std::uint32_t> vertex_spirv;
    std::vector<std::uint32_t> pixel_spirv;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VulkanGraphicsBlendState blend{};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
  };

  struct Submission {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VulkanGuestImageCache* image_cache = nullptr;
    std::optional<VulkanGuestImagePreparation> target;
    std::uint64_t timeline = 0;
  };

  explicit Impl(VulkanDeviceContext& device_context) : context(device_context) {}

  void DestroySubmission(Submission& submission) noexcept {
    if (submission.fence != VK_NULL_HANDLE) {
      dispatch.destroy_fence(context.device(), submission.fence, nullptr);
    }
    if (submission.target.has_value() && submission.image_cache != nullptr) {
      submission.image_cache->Discard(*submission.target);
    }
    if (submission.command_pool != VK_NULL_HANDLE) {
      dispatch.destroy_command_pool(context.device(), submission.command_pool,
                                    nullptr);
    }
    submission = {};
  }

  void Snapshot(VulkanGraphicsResult& result) const noexcept {
    result.completed_timeline = completed_timeline;
    result.retained_submission_count = retained.size();
    result.lost_dirty_resource_count = lost_dirty_resource_count;
  }

  void MarkDeviceLost() noexcept {
    device_lost = true;
    context.MarkDeviceLost();
  }

  void DestroyLostSubmissions() noexcept {
    VulkanGuestImageCache* image_cache = nullptr;
    for (Submission& submission : retained) {
      image_cache = submission.image_cache != nullptr ? submission.image_cache
                                                       : image_cache;
      DestroySubmission(submission);
    }
    retained.clear();
    if (image_cache != nullptr) {
      lost_dirty_resource_count = std::max(
          lost_dirty_resource_count, image_cache->lost_dirty_resource_count());
    }
  }

  VulkanDeviceContext& context;
  Dispatch dispatch;
  std::mutex mutex;
  std::vector<Pipeline> pipelines;
  std::vector<Submission> retained;
  std::uint64_t next_timeline = 1;
  std::uint64_t completed_timeline = 0;
  std::size_t lost_dirty_resource_count = 0;
  bool owns_context_execution_slot = false;
  bool device_lost = false;
};

VulkanGraphicsExecution::VulkanGraphicsExecution(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

VulkanGraphicsExecution::~VulkanGraphicsExecution() {
  if (!impl_) {
    return;
  }
  if (!impl_->retained.empty()) {
    const VkResult result = impl_->context.WaitIdle();
    if (IsDeviceLost(result)) {
      impl_->MarkDeviceLost();
    } else if (result != VK_SUCCESS) {
      std::terminate();
    }
  }
  for (Impl::Submission& submission : impl_->retained) {
    impl_->DestroySubmission(submission);
  }
  for (Impl::Pipeline& pipeline : impl_->pipelines) {
    if (pipeline.pipeline != VK_NULL_HANDLE) {
      impl_->dispatch.destroy_pipeline(impl_->context.device(), pipeline.pipeline,
                                       nullptr);
    }
    if (pipeline.layout != VK_NULL_HANDLE) {
      impl_->dispatch.destroy_pipeline_layout(impl_->context.device(),
                                              pipeline.layout, nullptr);
    }
  }
  if (impl_->owns_context_execution_slot) {
    impl_->context.ReleaseGraphicsExecutionOwner();
  }
}

VulkanGraphicsExecutionCreateResult VulkanGraphicsExecution::Create(
    VulkanDeviceContext& context) {
  VulkanGraphicsExecutionCreateResult result;
  if (context.IsDeviceLost()) {
    result.initialization.status = VulkanGraphicsStatus::kDeviceLost;
    Add(result.initialization, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kDeviceLost,
        "Vulkan graphics execution is unavailable after device loss");
    return result;
  }

  auto impl = std::make_unique<Impl>(context);
  if (!Load(context, impl->dispatch)) {
    result.initialization.status = VulkanGraphicsStatus::kDeviceFunctionUnavailable;
    Add(result.initialization, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kDeviceFunctionUnavailable,
        "Vulkan graphics execution is missing a required dynamic-rendering entry point");
    return result;
  }

  bool context_is_device_lost = false;
  if (!context.TryAcquireGraphicsExecutionOwner(context_is_device_lost)) {
    result.initialization.status = context_is_device_lost
                                       ? VulkanGraphicsStatus::kDeviceLost
                                       : VulkanGraphicsStatus::kInvalidArgument;
    Add(result.initialization, VulkanGraphicsDiagnosticSeverity::kError,
        context_is_device_lost ? VulkanGraphicsDiagnosticCode::kDeviceLost
                               : VulkanGraphicsDiagnosticCode::kInputRejected,
        "Vulkan graphics executor is unavailable");
    return result;
  }

  impl->owns_context_execution_slot = true;
  result.execution = std::unique_ptr<VulkanGraphicsExecution>(
      new VulkanGraphicsExecution(std::move(impl)));
  return result;
}

VulkanGraphicsResult VulkanGraphicsExecution::PollCompleted() {
  VulkanGraphicsResult result;
  std::lock_guard lock(impl_->mutex);
  if (impl_->device_lost || impl_->context.IsDeviceLost()) {
    result.status = VulkanGraphicsStatus::kDeviceLost;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kDeviceLost,
        "Vulkan graphics cannot poll after device loss");
    impl_->Snapshot(result);
    return result;
  }

  for (auto it = impl_->retained.begin(); it != impl_->retained.end();) {
    const VkResult status =
        impl_->dispatch.get_fence_status(impl_->context.device(), it->fence);
    if (status == VK_NOT_READY) {
      ++it;
      continue;
    }
    if (status == VK_SUCCESS) {
      if (!it->image_cache->Complete(*it->target)) {
        result.status = VulkanGraphicsStatus::kReadbackFailed;
        Add(result, VulkanGraphicsDiagnosticSeverity::kError,
            VulkanGraphicsDiagnosticCode::kReadbackFailed,
            "completed Vulkan graphics draw could not publish its color target",
            it->timeline);
        impl_->Snapshot(result);
        return result;
      }
      impl_->completed_timeline = std::max(impl_->completed_timeline, it->timeline);
      Add(result, VulkanGraphicsDiagnosticSeverity::kInfo,
          VulkanGraphicsDiagnosticCode::kSubmissionReclaimed,
          "reclaimed completed Vulkan graphics submission", it->timeline);
      ++result.reclaimed_submission_count;
      impl_->DestroySubmission(*it);
      it = impl_->retained.erase(it);
      continue;
    }
    if (IsDeviceLost(status)) {
      const std::uint64_t timeline = it->timeline;
      impl_->MarkDeviceLost();
      impl_->DestroyLostSubmissions();
      result.status = VulkanGraphicsStatus::kDeviceLost;
      Add(result, VulkanGraphicsDiagnosticSeverity::kError,
          VulkanGraphicsDiagnosticCode::kDeviceLost,
          "vkGetFenceStatus reported device loss while polling graphics work",
          timeline, status);
    } else {
      result.status = VulkanGraphicsStatus::kFenceStatusFailed;
      Add(result, VulkanGraphicsDiagnosticSeverity::kError,
          VulkanGraphicsDiagnosticCode::kReadbackFailed,
          "vkGetFenceStatus failed while polling graphics work", it->timeline,
          status);
    }
    impl_->Snapshot(result);
    return result;
  }
  impl_->Snapshot(result);
  return result;
}

VulkanGraphicsResult VulkanGraphicsExecution::Submit(
    const VulkanTranslatedDrawRequest& request, VulkanGuestImageCache& cache,
    VulkanGuestImagePreparation target) {
  VulkanGraphicsResult result;
  std::lock_guard lock(impl_->mutex);
  const auto discard = [&] { cache.Discard(target); };
  const auto reject = [&](VulkanGraphicsStatus status,
                          VulkanGraphicsDiagnosticCode code,
                          std::string message) {
    result.status = status;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError, code,
        std::move(message));
    discard();
    impl_->Snapshot(result);
    return result;
  };

  if (impl_->device_lost || impl_->context.IsDeviceLost()) {
    return reject(VulkanGraphicsStatus::kDeviceLost,
                  VulkanGraphicsDiagnosticCode::kDeviceLost,
                  "Vulkan graphics cannot submit after device loss");
  }
  if (request.vertex == nullptr || request.pixel == nullptr || !target ||
      !target.writable || request.vertex_count == 0 ||
      request.instance_count == 0 || request.timeout_ns == 0 ||
      request.timeout_ns == std::numeric_limits<std::uint64_t>::max()) {
    return reject(VulkanGraphicsStatus::kInvalidArgument,
                  VulkanGraphicsDiagnosticCode::kInputRejected,
                  "translated Vulkan graphics draw has invalid required input");
  }
  if (request.vertex->program.stage != ShaderType::Vertex ||
      request.pixel->program.stage != ShaderType::Pixel ||
      request.vertex->spirv.empty() || request.pixel->spirv.empty() ||
      !IsDescriptorFree(*request.vertex) || !IsDescriptorFree(*request.pixel)) {
    return reject(VulkanGraphicsStatus::kUnsupported,
                  VulkanGraphicsDiagnosticCode::kUnsupportedState,
                  "M8 graphics accepts only descriptor-free vertex and pixel SPIR-V");
  }
  if (!IsValidViewport(request.viewport) || !IsValidBlend(request.blend) ||
      ToTopology(request.topology) == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM ||
      ToCullMode(request.cull_mode) == VK_CULL_MODE_FLAG_BITS_MAX_ENUM) {
    return reject(VulkanGraphicsStatus::kInvalidArgument,
                  VulkanGraphicsDiagnosticCode::kInputRejected,
                  "translated Vulkan graphics draw has invalid fixed or dynamic state");
  }
  if (request.viewport.scissor.offset.x < 0 || request.viewport.scissor.offset.y < 0 ||
      static_cast<std::uint64_t>(request.viewport.scissor.offset.x) +
              request.viewport.scissor.extent.width > target.layout.mips[0].width ||
      static_cast<std::uint64_t>(request.viewport.scissor.offset.y) +
              request.viewport.scissor.extent.height > target.layout.mips[0].height) {
    return reject(VulkanGraphicsStatus::kInvalidArgument,
                  VulkanGraphicsDiagnosticCode::kInputRejected,
                  "translated Vulkan graphics scissor is outside the color target");
  }
  if (!impl_->context.SupportsColorAttachmentFormat(target.format.format)) {
    return reject(VulkanGraphicsStatus::kUnsupported,
                  VulkanGraphicsDiagnosticCode::kFormatUnsupported,
                  "the color target format does not support optimal color attachment use");
  }
  if (impl_->retained.size() >= kMaximumRetainedSubmissions) {
    return reject(VulkanGraphicsStatus::kResourceLimit,
                  VulkanGraphicsDiagnosticCode::kResourceLimit,
                  "the retained Vulkan graphics submission limit is exhausted");
  }

  const VkPrimitiveTopology topology = ToTopology(request.topology);
  const VkCullModeFlags cull_mode = ToCullMode(request.cull_mode);
  const VkFrontFace front_face = ToFrontFace(request.front_face);
  Impl::Pipeline* cached_pipeline = nullptr;
  for (Impl::Pipeline& pipeline : impl_->pipelines) {
    if (pipeline.vertex_spirv == request.vertex->spirv &&
        pipeline.pixel_spirv == request.pixel->spirv &&
        pipeline.format == target.format.format && pipeline.topology == topology &&
        pipeline.cull_mode == cull_mode && pipeline.front_face == front_face &&
        EqualBlend(pipeline.blend, request.blend)) {
      cached_pipeline = &pipeline;
      break;
    }
  }
  if (cached_pipeline == nullptr && impl_->pipelines.size() >= kMaximumPipelines) {
    return reject(VulkanGraphicsStatus::kResourceLimit,
                  VulkanGraphicsDiagnosticCode::kResourceLimit,
                  "the M8 graphics pipeline cache is full");
  }

  const VkDevice device = impl_->context.device();
  if (cached_pipeline == nullptr) {
    VkShaderModule vertex_module = VK_NULL_HANDLE;
    VkShaderModule pixel_module = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto cleanup = [&] {
      if (pipeline != VK_NULL_HANDLE) impl_->dispatch.destroy_pipeline(device, pipeline, nullptr);
      if (layout != VK_NULL_HANDLE) impl_->dispatch.destroy_pipeline_layout(device, layout, nullptr);
      if (pixel_module != VK_NULL_HANDLE) impl_->dispatch.destroy_shader_module(device, pixel_module, nullptr);
      if (vertex_module != VK_NULL_HANDLE) impl_->dispatch.destroy_shader_module(device, vertex_module, nullptr);
    };
    const auto fail_pipeline = [&](VkResult api_result, const char* step) {
      cleanup();
      if (IsDeviceLost(api_result)) {
        impl_->MarkDeviceLost();
        result.status = VulkanGraphicsStatus::kDeviceLost;
        Add(result, VulkanGraphicsDiagnosticSeverity::kError,
            VulkanGraphicsDiagnosticCode::kDeviceLost, step, 0, api_result);
      } else {
        result.status = VulkanGraphicsStatus::kUnsupported;
        Add(result, VulkanGraphicsDiagnosticSeverity::kError,
            VulkanGraphicsDiagnosticCode::kPipelineCreationFailed, step, 0,
            api_result);
      }
      discard();
      impl_->Snapshot(result);
      return result;
    };
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = request.vertex->spirv.size() * sizeof(std::uint32_t);
    module_info.pCode = request.vertex->spirv.data();
    VkResult api = impl_->dispatch.create_shader_module(device, &module_info, nullptr,
                                                        &vertex_module);
    if (api != VK_SUCCESS) return fail_pipeline(api, "vkCreateShaderModule(vertex)");
    module_info.codeSize = request.pixel->spirv.size() * sizeof(std::uint32_t);
    module_info.pCode = request.pixel->spirv.data();
    api = impl_->dispatch.create_shader_module(device, &module_info, nullptr,
                                                &pixel_module);
    if (api != VK_SUCCESS) return fail_pipeline(api, "vkCreateShaderModule(pixel)");
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    api = impl_->dispatch.create_pipeline_layout(device, &layout_info, nullptr, &layout);
    if (api != VK_SUCCESS) return fail_pipeline(api, "vkCreatePipelineLayout");

    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex_module, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, pixel_module, "main", nullptr},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = cull_mode;
    raster.frontFace = front_face;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = request.blend.enabled ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = request.blend.source_color;
    attachment.dstColorBlendFactor = request.blend.destination_color;
    attachment.colorBlendOp = request.blend.color_op;
    attachment.srcAlphaBlendFactor = request.blend.source_alpha;
    attachment.dstAlphaBlendFactor = request.blend.destination_alpha;
    attachment.alphaBlendOp = request.blend.alpha_op;
    attachment.colorWriteMask = request.blend.write_mask;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                              VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &target.format.format;
    VkGraphicsPipelineCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.pNext = &rendering;
    create_info.stageCount = 2;
    create_info.pStages = stages;
    create_info.pVertexInputState = &vertex_input;
    create_info.pInputAssemblyState = &assembly;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster;
    create_info.pMultisampleState = &multisample;
    create_info.pColorBlendState = &blend;
    create_info.pDynamicState = &dynamic;
    create_info.layout = layout;
    api = impl_->dispatch.create_graphics_pipelines(device, VK_NULL_HANDLE, 1,
                                                     &create_info, nullptr, &pipeline);
    if (api != VK_SUCCESS) return fail_pipeline(api, "vkCreateGraphicsPipelines");
    impl_->dispatch.destroy_shader_module(device, pixel_module, nullptr);
    impl_->dispatch.destroy_shader_module(device, vertex_module, nullptr);
    cached_pipeline = &impl_->pipelines.emplace_back(
        Impl::Pipeline{request.vertex->spirv, request.pixel->spirv, target.format.format,
                       topology, cull_mode, front_face, request.blend, layout, pipeline});
  }

  Impl::Submission submission;
  submission.image_cache = &cache;
  submission.target.emplace(std::move(target));
  const auto fail_submission = [&](VulkanGraphicsStatus status,
                                   VulkanGraphicsDiagnosticCode code,
                                   const char* step, VkResult api_result) {
    if (IsDeviceLost(api_result)) {
      impl_->MarkDeviceLost();
      status = VulkanGraphicsStatus::kDeviceLost;
      code = VulkanGraphicsDiagnosticCode::kDeviceLost;
    }
    result.status = status;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError, code, step, 0,
        api_result);
    impl_->DestroySubmission(submission);
    impl_->Snapshot(result);
    return result;
  };
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pool_info.queueFamilyIndex = impl_->context.queue_family_index();
  VkResult api = impl_->dispatch.create_command_pool(device, &pool_info, nullptr,
                                                      &submission.command_pool);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kResourceLimit,
                                                 VulkanGraphicsDiagnosticCode::kResourceLimit,
                                                 "vkCreateCommandPool", api);
  VkCommandBufferAllocateInfo command_info{};
  command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_info.commandPool = submission.command_pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  api = impl_->dispatch.allocate_command_buffers(device, &command_info,
                                                  &submission.command_buffer);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kResourceLimit,
                                                 VulkanGraphicsDiagnosticCode::kResourceLimit,
                                                 "vkAllocateCommandBuffers", api);
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  api = impl_->dispatch.create_fence(device, &fence_info, nullptr, &submission.fence);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kResourceLimit,
                                                 VulkanGraphicsDiagnosticCode::kResourceLimit,
                                                 "vkCreateFence", api);
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  api = impl_->dispatch.begin_command_buffer(submission.command_buffer, &begin_info);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kFenceWaitFailed,
                                                 VulkanGraphicsDiagnosticCode::kInputRejected,
                                                 "vkBeginCommandBuffer", api);
  if (!cache.RecordUpload(submission.command_buffer, *submission.target,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) {
    return fail_submission(VulkanGraphicsStatus::kReadbackFailed,
                           VulkanGraphicsDiagnosticCode::kReadbackFailed,
                           "color-target upload recording", VK_SUCCESS);
  }
  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = submission.target->view;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering_info.renderArea = request.viewport.scissor;
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &color_attachment;
  VkViewport viewport{request.viewport.x, request.viewport.y, request.viewport.width,
                      request.viewport.height, request.viewport.min_depth,
                      request.viewport.max_depth};
  impl_->dispatch.cmd_begin_rendering(submission.command_buffer, &rendering_info);
  impl_->dispatch.cmd_bind_pipeline(submission.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    cached_pipeline->pipeline);
  impl_->dispatch.cmd_set_viewport(submission.command_buffer, 0, 1, &viewport);
  impl_->dispatch.cmd_set_scissor(submission.command_buffer, 0, 1,
                                   &request.viewport.scissor);
  impl_->dispatch.cmd_draw(submission.command_buffer, request.vertex_count,
                           request.instance_count, request.first_vertex,
                           request.first_instance);
  impl_->dispatch.cmd_end_rendering(submission.command_buffer);
  if (!cache.RecordReadback(submission.command_buffer, *submission.target,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) {
    return fail_submission(VulkanGraphicsStatus::kReadbackFailed,
                           VulkanGraphicsDiagnosticCode::kReadbackFailed,
                           "color-target readback recording", VK_SUCCESS);
  }
  api = impl_->dispatch.end_command_buffer(submission.command_buffer);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kFenceWaitFailed,
                                                 VulkanGraphicsDiagnosticCode::kInputRejected,
                                                 "vkEndCommandBuffer", api);
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &submission.command_buffer;
  submission.timeline = impl_->next_timeline++;
  {
    std::lock_guard queue_lock(impl_->context.queue_mutex());
    api = impl_->dispatch.queue_submit(impl_->context.queue(), 1, &submit_info,
                                        submission.fence);
  }
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kFenceWaitFailed,
                                                 VulkanGraphicsDiagnosticCode::kInputRejected,
                                                 "vkQueueSubmit", api);
  if (!cache.MarkSubmitted(*submission.target)) {
    result.status = VulkanGraphicsStatus::kReadbackFailed;
    result.timeline = submission.timeline;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kReadbackFailed,
        "submitted color target could not be marked GPU-dirty", result.timeline);
    impl_->retained.push_back(std::move(submission));
    impl_->Snapshot(result);
    return result;
  }
  result.timeline = submission.timeline;
  const VkFence fence = submission.fence;
  api = impl_->dispatch.wait_for_fences(device, 1, &fence, VK_TRUE, request.timeout_ns);
  if (api == VK_SUCCESS) {
    if (!cache.Complete(*submission.target)) {
      result.status = VulkanGraphicsStatus::kReadbackFailed;
      Add(result, VulkanGraphicsDiagnosticSeverity::kError,
          VulkanGraphicsDiagnosticCode::kReadbackFailed,
          "signalled color target could not publish guest readback", result.timeline);
      impl_->retained.push_back(std::move(submission));
      impl_->Snapshot(result);
      return result;
    }
    impl_->completed_timeline = std::max(impl_->completed_timeline, result.timeline);
    impl_->DestroySubmission(submission);
    impl_->Snapshot(result);
    return result;
  }
  if (api == VK_TIMEOUT) {
    result.status = VulkanGraphicsStatus::kFenceWaitTimedOut;
    Add(result, VulkanGraphicsDiagnosticSeverity::kWarning,
        VulkanGraphicsDiagnosticCode::kFenceWaitTimedOut,
        "translated Vulkan graphics timed out; retaining the color target lease",
        result.timeline, api);
  } else if (IsDeviceLost(api)) {
    impl_->MarkDeviceLost();
    impl_->DestroySubmission(submission);
    impl_->DestroyLostSubmissions();
    result.status = VulkanGraphicsStatus::kDeviceLost;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kDeviceLost,
        "vkWaitForFences reported device loss for Vulkan graphics work",
        result.timeline, api);
  } else {
    result.status = VulkanGraphicsStatus::kFenceWaitFailed;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kReadbackFailed,
        "vkWaitForFences failed for Vulkan graphics work", result.timeline, api);
  }
  if (result.status != VulkanGraphicsStatus::kDeviceLost) {
    impl_->retained.push_back(std::move(submission));
  }
  impl_->Snapshot(result);
  return result;
}

}  // namespace kajps5::gpu::vulkan
