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
  PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout = nullptr;
  PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout = nullptr;
  PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
  PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
  PFN_vkAllocateDescriptorSets allocate_descriptor_sets = nullptr;
  PFN_vkUpdateDescriptorSets update_descriptor_sets = nullptr;
  PFN_vkCreateGraphicsPipelines create_graphics_pipelines = nullptr;
  PFN_vkDestroyPipeline destroy_pipeline = nullptr;
  PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
  PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets = nullptr;
  PFN_vkCmdPushConstants cmd_push_constants = nullptr;
  PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
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
  dispatch.create_descriptor_set_layout = Resolve<PFN_vkCreateDescriptorSetLayout>(
      context, "vkCreateDescriptorSetLayout");
  dispatch.destroy_descriptor_set_layout = Resolve<PFN_vkDestroyDescriptorSetLayout>(
      context, "vkDestroyDescriptorSetLayout");
  dispatch.create_descriptor_pool = Resolve<PFN_vkCreateDescriptorPool>(
      context, "vkCreateDescriptorPool");
  dispatch.destroy_descriptor_pool = Resolve<PFN_vkDestroyDescriptorPool>(
      context, "vkDestroyDescriptorPool");
  dispatch.allocate_descriptor_sets = Resolve<PFN_vkAllocateDescriptorSets>(
      context, "vkAllocateDescriptorSets");
  dispatch.update_descriptor_sets = Resolve<PFN_vkUpdateDescriptorSets>(
      context, "vkUpdateDescriptorSets");
  dispatch.create_graphics_pipelines = Resolve<PFN_vkCreateGraphicsPipelines>(
      context, "vkCreateGraphicsPipelines");
  dispatch.destroy_pipeline =
      Resolve<PFN_vkDestroyPipeline>(context, "vkDestroyPipeline");
  dispatch.cmd_bind_pipeline =
      Resolve<PFN_vkCmdBindPipeline>(context, "vkCmdBindPipeline");
  dispatch.cmd_bind_descriptor_sets = Resolve<PFN_vkCmdBindDescriptorSets>(
      context, "vkCmdBindDescriptorSets");
  dispatch.cmd_push_constants = Resolve<PFN_vkCmdPushConstants>(
      context, "vkCmdPushConstants");
  dispatch.cmd_pipeline_barrier = Resolve<PFN_vkCmdPipelineBarrier>(
      context, "vkCmdPipelineBarrier");
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
         dispatch.create_descriptor_set_layout && dispatch.destroy_descriptor_set_layout &&
         dispatch.create_descriptor_pool && dispatch.destroy_descriptor_pool &&
         dispatch.allocate_descriptor_sets && dispatch.update_descriptor_sets &&
         dispatch.create_graphics_pipelines && dispatch.destroy_pipeline &&
         dispatch.cmd_bind_pipeline && dispatch.cmd_bind_descriptor_sets &&
         dispatch.cmd_push_constants && dispatch.cmd_pipeline_barrier && dispatch.cmd_set_viewport &&
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

bool EqualSetLayouts(const std::vector<VulkanGraphicsDescriptorSetPlan>& left,
                     const std::vector<VulkanGraphicsDescriptorSetPlan>& right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t set = 0; set < left.size(); ++set) {
    if (left[set].set != right[set].set ||
        left[set].bindings.size() != right[set].bindings.size()) return false;
    for (std::size_t binding = 0; binding < left[set].bindings.size(); ++binding) {
      const auto& a = left[set].bindings[binding].layout;
      const auto& b = right[set].bindings[binding].layout;
      if (a.binding != b.binding || a.descriptorType != b.descriptorType ||
          a.descriptorCount != b.descriptorCount || a.stageFlags != b.stageFlags)
        return false;
    }
  }
  return true;
}

bool EqualPushRanges(const std::vector<VulkanGraphicsPushConstantPlan>& left,
                     const std::vector<VulkanGraphicsPushConstantPlan>& right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (left[i].range.stageFlags != right[i].range.stageFlags ||
        left[i].range.offset != right[i].range.offset ||
        left[i].range.size != right[i].range.size) return false;
  }
  return true;
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
    std::vector<VulkanGraphicsDescriptorSetPlan> set_layouts;
    std::vector<VulkanGraphicsPushConstantPlan> push_constants;
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
  };

  struct Submission {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets;
    VulkanGuestBufferCache* buffer_cache = nullptr;
    std::optional<VulkanGuestBufferPreparation> vertex_buffers;
    std::optional<VulkanGuestBufferPreparation> pixel_buffers;
    VulkanGuestImageCache* image_cache = nullptr;
    std::optional<VulkanGuestImageSetPreparation> vertex_images;
    std::optional<VulkanGuestImageSetPreparation> pixel_images;
    std::optional<VulkanGuestImagePreparation> target;
    VulkanGraphicsBindingPlan plan;
    std::uint64_t timeline = 0;
  };

  explicit Impl(VulkanDeviceContext& device_context) : context(device_context) {}

  void DestroySubmission(Submission& submission) noexcept {
    if (submission.fence != VK_NULL_HANDLE) {
      dispatch.destroy_fence(context.device(), submission.fence, nullptr);
    }
    if (submission.descriptor_pool != VK_NULL_HANDLE) {
      dispatch.destroy_descriptor_pool(context.device(), submission.descriptor_pool,
                                      nullptr);
    }
    if (submission.vertex_buffers.has_value() && submission.buffer_cache != nullptr)
      submission.buffer_cache->Discard(*submission.vertex_buffers);
    if (submission.pixel_buffers.has_value() && submission.buffer_cache != nullptr)
      submission.buffer_cache->Discard(*submission.pixel_buffers);
    if (submission.vertex_images.has_value() && submission.image_cache != nullptr)
      submission.image_cache->Discard(*submission.vertex_images);
    if (submission.pixel_images.has_value() && submission.image_cache != nullptr)
      submission.image_cache->Discard(*submission.pixel_images);
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
    VulkanGuestBufferCache* buffer_cache = nullptr;
    for (Submission& submission : retained) {
      image_cache = submission.image_cache != nullptr ? submission.image_cache
                                                       : image_cache;
      buffer_cache = submission.buffer_cache != nullptr ? submission.buffer_cache
                                                         : buffer_cache;
      DestroySubmission(submission);
    }
    retained.clear();
    // Image sets and the target share one image-cache ledger, so account it
    // once; buffers use their own ledger and may be added without overlap.
    const std::size_t image_lost = image_cache == nullptr
        ? 0 : image_cache->lost_dirty_resource_count();
    const std::size_t buffer_lost = buffer_cache == nullptr
        ? 0 : buffer_cache->lost_dirty_resource_count();
    lost_dirty_resource_count = image_lost + buffer_lost;
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
    for (VkDescriptorSetLayout set_layout : pipeline.descriptor_set_layouts) {
      if (set_layout != VK_NULL_HANDLE) {
        impl_->dispatch.destroy_descriptor_set_layout(impl_->context.device(),
                                                      set_layout, nullptr);
      }
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
      const bool vertex_buffers_complete =
          it->buffer_cache->Complete(*it->vertex_buffers);
      const bool pixel_buffers_complete =
          it->buffer_cache->Complete(*it->pixel_buffers);
      bool vertex_images_complete = true;
      for (auto& image : it->vertex_images->images)
        vertex_images_complete = it->image_cache->Complete(image) && vertex_images_complete;
      bool pixel_images_complete = true;
      for (auto& image : it->pixel_images->images)
        pixel_images_complete = it->image_cache->Complete(image) && pixel_images_complete;
      if (!vertex_buffers_complete || !pixel_buffers_complete ||
          !vertex_images_complete || !pixel_images_complete ||
          !it->image_cache->Complete(*it->target)) {
        result.status = VulkanGraphicsStatus::kReadbackFailed;
        Add(result, VulkanGraphicsDiagnosticSeverity::kError,
            VulkanGraphicsDiagnosticCode::kReadbackFailed,
            "completed Vulkan graphics draw could not publish all retained guest resources",
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
    const VulkanTranslatedDrawRequest& request, VulkanGuestBufferCache& buffer_cache,
    VulkanGuestBufferPreparation vertex_buffers,
    VulkanGuestBufferPreparation pixel_buffers, VulkanGuestImageCache& image_cache,
    VulkanGuestImageSetPreparation vertex_images,
    VulkanGuestImageSetPreparation pixel_images, VulkanGuestImagePreparation target,
    VulkanGraphicsBindingPlan plan) {
  VulkanGraphicsResult result;
  std::lock_guard lock(impl_->mutex);
  const auto discard = [&] {
    buffer_cache.Discard(vertex_buffers);
    buffer_cache.Discard(pixel_buffers);
    image_cache.Discard(vertex_images);
    image_cache.Discard(pixel_images);
    image_cache.Discard(target);
  };
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
      request.vertex->spirv.empty() || request.pixel->spirv.empty() || !plan) {
    return reject(VulkanGraphicsStatus::kUnsupported,
                  VulkanGraphicsDiagnosticCode::kUnsupportedState,
                  "translated Vulkan graphics requires a complete binding plan");
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

  const auto overlaps_target = [&](std::uint64_t address, std::uint64_t size) {
    const auto target_address = target.layout.storage_key.guest_address;
    const auto target_size = target.layout.storage_key.byte_count;
    return size != 0 && target_size != 0 &&
        address <= std::numeric_limits<std::uint64_t>::max() - size &&
        target_address <= std::numeric_limits<std::uint64_t>::max() - target_size &&
        address < target_address + target_size && target_address < address + size;
  };
  for (const auto& view : vertex_buffers.views) {
    if (overlaps_target(view.guest_address, view.size)) {
      return reject(VulkanGraphicsStatus::kInvalidArgument,
                    VulkanGraphicsDiagnosticCode::kInputRejected,
                    "color target overlaps a planned vertex buffer guest range");
    }
  }
  for (const auto& view : pixel_buffers.views) {
    if (overlaps_target(view.guest_address, view.size)) {
      return reject(VulkanGraphicsStatus::kInvalidArgument,
                    VulkanGraphicsDiagnosticCode::kInputRejected,
                    "color target overlaps a planned pixel buffer guest range");
    }
  }
  for (const auto& upload : plan.image_uploads) {
    if (overlaps_target(upload.guest_address, upload.guest_size)) {
      return reject(VulkanGraphicsStatus::kInvalidArgument,
                    VulkanGraphicsDiagnosticCode::kInputRejected,
                    "color target overlaps a planned shader image guest range");
    }
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
        EqualBlend(pipeline.blend, request.blend) &&
        EqualSetLayouts(pipeline.set_layouts, plan.set_layouts) &&
        EqualPushRanges(pipeline.push_constants, plan.push_constants)) {
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
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto cleanup = [&] {
      if (pipeline != VK_NULL_HANDLE) impl_->dispatch.destroy_pipeline(device, pipeline, nullptr);
      if (layout != VK_NULL_HANDLE) impl_->dispatch.destroy_pipeline_layout(device, layout, nullptr);
      for (VkDescriptorSetLayout set_layout : descriptor_set_layouts)
        if (set_layout != VK_NULL_HANDLE)
          impl_->dispatch.destroy_descriptor_set_layout(device, set_layout, nullptr);
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
    descriptor_set_layouts.reserve(plan.set_layouts.size());
    for (const auto& set_plan : plan.set_layouts) {
      std::vector<VkDescriptorSetLayoutBinding> bindings;
      bindings.reserve(set_plan.bindings.size());
      for (const auto& binding : set_plan.bindings) bindings.push_back(binding.layout);
      VkDescriptorSetLayoutCreateInfo set_info{};
      set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      set_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
      set_info.pBindings = bindings.empty() ? nullptr : bindings.data();
      VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
      api = impl_->dispatch.create_descriptor_set_layout(device, &set_info, nullptr,
                                                         &set_layout);
      if (api != VK_SUCCESS) return fail_pipeline(api, "vkCreateDescriptorSetLayout");
      descriptor_set_layouts.push_back(set_layout);
    }
    std::vector<VkPushConstantRange> push_ranges;
    push_ranges.reserve(plan.push_constants.size());
    for (const auto& push : plan.push_constants) push_ranges.push_back(push.range);
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = static_cast<std::uint32_t>(descriptor_set_layouts.size());
    layout_info.pSetLayouts = descriptor_set_layouts.empty() ? nullptr : descriptor_set_layouts.data();
    layout_info.pushConstantRangeCount = static_cast<std::uint32_t>(push_ranges.size());
    layout_info.pPushConstantRanges = push_ranges.empty() ? nullptr : push_ranges.data();
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
    cached_pipeline = &impl_->pipelines.emplace_back(Impl::Pipeline{
        request.vertex->spirv, request.pixel->spirv, target.format.format,
        topology, cull_mode, front_face, request.blend, plan.set_layouts,
        plan.push_constants, std::move(descriptor_set_layouts), layout, pipeline});
  }

  Impl::Submission submission;
  submission.buffer_cache = &buffer_cache;
  submission.vertex_buffers.emplace(std::move(vertex_buffers));
  submission.pixel_buffers.emplace(std::move(pixel_buffers));
  submission.image_cache = &image_cache;
  submission.vertex_images.emplace(std::move(vertex_images));
  submission.pixel_images.emplace(std::move(pixel_images));
  submission.target.emplace(std::move(target));
  submission.plan = std::move(plan);
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
  VkDescriptorPoolCreateInfo descriptor_pool_info{};
  descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_info.maxSets = static_cast<std::uint32_t>(
      cached_pipeline->descriptor_set_layouts.size());
  descriptor_pool_info.poolSizeCount = static_cast<std::uint32_t>(
      submission.plan.pool_sizes.size());
  descriptor_pool_info.pPoolSizes = submission.plan.pool_sizes.empty()
      ? nullptr : submission.plan.pool_sizes.data();
  api = impl_->dispatch.create_descriptor_pool(device, &descriptor_pool_info, nullptr,
                                                &submission.descriptor_pool);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kResourceLimit,
                                                 VulkanGraphicsDiagnosticCode::kResourceLimit,
                                                 "vkCreateDescriptorPool", api);
  submission.descriptor_sets.resize(cached_pipeline->descriptor_set_layouts.size());
  VkDescriptorSetAllocateInfo descriptor_set_info{};
  descriptor_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_info.descriptorPool = submission.descriptor_pool;
  descriptor_set_info.descriptorSetCount = static_cast<std::uint32_t>(
      cached_pipeline->descriptor_set_layouts.size());
  descriptor_set_info.pSetLayouts = cached_pipeline->descriptor_set_layouts.data();
  api = impl_->dispatch.allocate_descriptor_sets(device, &descriptor_set_info,
                                                 submission.descriptor_sets.data());
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kResourceLimit,
                                                 VulkanGraphicsDiagnosticCode::kResourceLimit,
                                                 "vkAllocateDescriptorSets", api);
  std::vector<VkWriteDescriptorSet> writes;
  for (std::size_t set_index = 0; set_index < submission.plan.set_layouts.size(); ++set_index) {
    const auto& set = submission.plan.set_layouts[set_index];
    for (const auto& binding : set.bindings) {
      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = submission.descriptor_sets[set_index];
      write.dstBinding = binding.layout.binding;
      write.descriptorCount = binding.layout.descriptorCount;
      write.descriptorType = binding.layout.descriptorType;
      write.pBufferInfo = binding.buffer_infos.empty() ? nullptr : binding.buffer_infos.data();
      write.pImageInfo = binding.image_infos.empty() ? nullptr : binding.image_infos.data();
      writes.push_back(write);
    }
  }
  if (!writes.empty()) impl_->dispatch.update_descriptor_sets(
      device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  api = impl_->dispatch.begin_command_buffer(submission.command_buffer, &begin_info);
  if (api != VK_SUCCESS) return fail_submission(VulkanGraphicsStatus::kFenceWaitFailed,
                                                 VulkanGraphicsDiagnosticCode::kInputRejected,
                                                 "vkBeginCommandBuffer", api);
  for (const auto& upload : submission.plan.image_uploads) {
    auto& images = upload.stage_set == 0 ? *submission.vertex_images
                                         : *submission.pixel_images;
    if (upload.preparation_index >= images.images.size()) {
      return fail_submission(VulkanGraphicsStatus::kInvalidArgument,
                             VulkanGraphicsDiagnosticCode::kInputRejected,
                             "binding plan image preparation index", VK_SUCCESS);
    }
    auto& image = images.images[upload.preparation_index];
    if (!image.upload_recorded && !image_cache.RecordUpload(
            submission.command_buffer, image, upload.layout, upload.shader_stages,
            upload.shader_access)) {
      return fail_submission(VulkanGraphicsStatus::kReadbackFailed,
                             VulkanGraphicsDiagnosticCode::kReadbackFailed,
                             "shader image upload recording", VK_SUCCESS);
    }
  }
  if (!image_cache.RecordUpload(submission.command_buffer, *submission.target,
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
  if (!submission.descriptor_sets.empty()) {
    impl_->dispatch.cmd_bind_descriptor_sets(submission.command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS, cached_pipeline->layout, 0,
        static_cast<std::uint32_t>(submission.descriptor_sets.size()),
        submission.descriptor_sets.data(), 0, nullptr);
  }
  for (const auto& push : submission.plan.push_constants) {
    impl_->dispatch.cmd_push_constants(submission.command_buffer,
        cached_pipeline->layout, push.range.stageFlags, push.range.offset,
        push.range.size, push.data_dwords.data());
  }
  impl_->dispatch.cmd_set_viewport(submission.command_buffer, 0, 1, &viewport);
  impl_->dispatch.cmd_set_scissor(submission.command_buffer, 0, 1,
                                   &request.viewport.scissor);
  impl_->dispatch.cmd_draw(submission.command_buffer, request.vertex_count,
                           request.instance_count, request.first_vertex,
                           request.first_instance);
  impl_->dispatch.cmd_end_rendering(submission.command_buffer);
  for (const auto& upload : submission.plan.image_uploads) {
    auto& images = upload.stage_set == 0 ? *submission.vertex_images
                                         : *submission.pixel_images;
    auto& image = images.images[upload.preparation_index];
    if (image.writable && !image.readback_recorded && !image_cache.RecordReadback(
            submission.command_buffer, image, upload.shader_stages,
            upload.shader_access)) {
      return fail_submission(VulkanGraphicsStatus::kReadbackFailed,
                             VulkanGraphicsDiagnosticCode::kReadbackFailed,
                             "shader image readback recording", VK_SUCCESS);
    }
  }
  std::vector<VkBufferMemoryBarrier> host_read_barriers;
  const auto append_host_read_barrier = [&](const VulkanGuestBufferPreparation& buffers) {
    if (buffers.buffer == VK_NULL_HANDLE ||
        std::none_of(buffers.views.begin(), buffers.views.end(),
                     [](const auto& view) { return view.shader_writes; })) return;
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffers.buffer;
    barrier.offset = 0;
    barrier.size = buffers.logical_size;
    host_read_barriers.push_back(barrier);
  };
  append_host_read_barrier(*submission.vertex_buffers);
  append_host_read_barrier(*submission.pixel_buffers);
  if (!host_read_barriers.empty()) {
    impl_->dispatch.cmd_pipeline_barrier(submission.command_buffer,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
        static_cast<std::uint32_t>(host_read_barriers.size()), host_read_barriers.data(),
        0, nullptr);
  }
  if (!image_cache.RecordReadback(submission.command_buffer, *submission.target,
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
  bool marked = buffer_cache.MarkSubmitted(*submission.vertex_buffers);
  marked = buffer_cache.MarkSubmitted(*submission.pixel_buffers) && marked;
  for (auto& image : submission.vertex_images->images)
    marked = image_cache.MarkSubmitted(image) && marked;
  for (auto& image : submission.pixel_images->images)
    marked = image_cache.MarkSubmitted(image) && marked;
  marked = image_cache.MarkSubmitted(*submission.target) && marked;
  if (!marked) {
    result.status = VulkanGraphicsStatus::kReadbackFailed;
    result.timeline = submission.timeline;
    Add(result, VulkanGraphicsDiagnosticSeverity::kError,
        VulkanGraphicsDiagnosticCode::kReadbackFailed,
        "submitted graphics resource could not be marked GPU-dirty", result.timeline);
    impl_->retained.push_back(std::move(submission));
    impl_->Snapshot(result);
    return result;
  }
  result.timeline = submission.timeline;
  const VkFence fence = submission.fence;
  api = impl_->dispatch.wait_for_fences(device, 1, &fence, VK_TRUE, request.timeout_ns);
  if (api == VK_SUCCESS) {
    const bool vertex_buffers_complete =
        buffer_cache.Complete(*submission.vertex_buffers);
    const bool pixel_buffers_complete = buffer_cache.Complete(*submission.pixel_buffers);
    bool vertex_images_complete = true;
    for (auto& image : submission.vertex_images->images)
      vertex_images_complete = image_cache.Complete(image) && vertex_images_complete;
    bool pixel_images_complete = true;
    for (auto& image : submission.pixel_images->images)
      pixel_images_complete = image_cache.Complete(image) && pixel_images_complete;
    if (!vertex_buffers_complete || !pixel_buffers_complete ||
        !vertex_images_complete || !pixel_images_complete ||
        !image_cache.Complete(*submission.target)) {
      result.status = VulkanGraphicsStatus::kReadbackFailed;
      Add(result, VulkanGraphicsDiagnosticSeverity::kError,
          VulkanGraphicsDiagnosticCode::kReadbackFailed,
          "signalled graphics work could not publish every guest readback", result.timeline);
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
