// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/{commandScheduler,masterSemaphore,render}.*
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/execution.h"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace kajps5::gpu::vulkan {
namespace {

struct ComputeDispatch {
  PFN_vkCreateCommandPool create_command_pool = nullptr;
  PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
  PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
  PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
  PFN_vkEndCommandBuffer end_command_buffer = nullptr;
  PFN_vkCreateShaderModule create_shader_module = nullptr;
  PFN_vkDestroyShaderModule destroy_shader_module = nullptr;
  PFN_vkCreatePipelineLayout create_pipeline_layout = nullptr;
  PFN_vkDestroyPipelineLayout destroy_pipeline_layout = nullptr;
  PFN_vkCreateComputePipelines create_compute_pipelines = nullptr;
  PFN_vkDestroyPipeline destroy_pipeline = nullptr;
  PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
  PFN_vkCmdDispatch cmd_dispatch = nullptr;
  PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
  PFN_vkCreateFence create_fence = nullptr;
  PFN_vkDestroyFence destroy_fence = nullptr;
  PFN_vkWaitForFences wait_for_fences = nullptr;
  PFN_vkGetFenceStatus get_fence_status = nullptr;
  PFN_vkQueueSubmit queue_submit = nullptr;
  PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout = nullptr;
  PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout = nullptr;
  PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
  PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
  PFN_vkAllocateDescriptorSets allocate_descriptor_sets = nullptr;
  PFN_vkUpdateDescriptorSets update_descriptor_sets = nullptr;
  PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets = nullptr;
  PFN_vkCmdPushConstants cmd_push_constants = nullptr;
};

struct Submission {
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkShaderModule shader_module = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VulkanGuestBufferCache *guest_cache = nullptr;
  std::optional<VulkanGuestBufferPreparation> guest_preparation;
  VulkanGuestImageCache *image_cache = nullptr;
  std::optional<VulkanGuestImageSetPreparation> image_preparation;
  std::uint64_t timeline = 0;
};

void AddDiagnostic(VulkanComputeResult& result,
                   VulkanDiagnosticSeverity severity,
                   VulkanComputeDiagnosticCode code,
                   std::string message,
                   std::uint64_t timeline = 0,
                   VkResult api_result = VK_SUCCESS) {
  result.diagnostics.push_back(
      {severity, code, timeline, static_cast<std::int32_t>(api_result),
       std::move(message)});
}

bool IsDeviceLost(VkResult result) noexcept {
  return result == VK_ERROR_DEVICE_LOST;
}

void DestroySubmission(const ComputeDispatch& dispatch,
                       VkDevice device,
                       Submission& submission) noexcept {
  if (submission.fence != VK_NULL_HANDLE) {
    dispatch.destroy_fence(device, submission.fence, nullptr);
    submission.fence = VK_NULL_HANDLE;
  }
  if (submission.guest_preparation.has_value() &&
      submission.guest_cache != nullptr) {
    submission.guest_cache->Discard(*submission.guest_preparation);
    submission.guest_preparation.reset();
    submission.guest_cache = nullptr;
  }
  if (submission.image_preparation.has_value() &&
      submission.image_cache != nullptr) {
    submission.image_cache->Discard(*submission.image_preparation);
    submission.image_preparation.reset();
    submission.image_cache = nullptr;
  }
  if (submission.descriptor_pool != VK_NULL_HANDLE &&
      dispatch.destroy_descriptor_pool != nullptr) {
    dispatch.destroy_descriptor_pool(device, submission.descriptor_pool,
                                     nullptr);
    submission.descriptor_pool = VK_NULL_HANDLE;
  }
  if (submission.descriptor_set_layout != VK_NULL_HANDLE &&
      dispatch.destroy_descriptor_set_layout != nullptr) {
    dispatch.destroy_descriptor_set_layout(
        device, submission.descriptor_set_layout, nullptr);
    submission.descriptor_set_layout = VK_NULL_HANDLE;
  }
  if (submission.pipeline != VK_NULL_HANDLE) {
    dispatch.destroy_pipeline(device, submission.pipeline, nullptr);
    submission.pipeline = VK_NULL_HANDLE;
  }
  if (submission.pipeline_layout != VK_NULL_HANDLE) {
    dispatch.destroy_pipeline_layout(device, submission.pipeline_layout, nullptr);
    submission.pipeline_layout = VK_NULL_HANDLE;
  }
  if (submission.shader_module != VK_NULL_HANDLE) {
    dispatch.destroy_shader_module(device, submission.shader_module, nullptr);
    submission.shader_module = VK_NULL_HANDLE;
  }
  if (submission.command_pool != VK_NULL_HANDLE) {
    // The command buffer was allocated from this private pool. Destroying the
    // pool releases it only after its fence is known complete.
    dispatch.destroy_command_pool(device, submission.command_pool, nullptr);
    submission.command_pool = VK_NULL_HANDLE;
    submission.command_buffer = VK_NULL_HANDLE;
  }
}

// Owns only handles returned by successful Vulkan calls. Keeping this guard
// allocation-free makes every pre-submit construction path safe if a
// diagnostic allocation throws or a driver writes a non-null output on error.
class SubmissionTransaction final {
 public:
  SubmissionTransaction(const ComputeDispatch& dispatch, VkDevice device) noexcept
      : dispatch_(dispatch), device_(device) {}

  ~SubmissionTransaction() { DestroySubmission(dispatch_, device_, submission_); }

  SubmissionTransaction(const SubmissionTransaction&) = delete;
  SubmissionTransaction& operator=(const SubmissionTransaction&) = delete;

  [[nodiscard]] Submission& submission() noexcept { return submission_; }

  [[nodiscard]] Submission Release() noexcept {
    Submission released = std::move(submission_);
    submission_ = {};
    return released;
  }

 private:
  const ComputeDispatch& dispatch_;
  VkDevice device_ = VK_NULL_HANDLE;
  Submission submission_;
};

bool LoadComputeDispatch(VulkanDeviceContext& context,
                         ComputeDispatch& dispatch,
                         VulkanComputeResult& result) {
  const auto resolve = [&](const char* name) {
    return context.ResolveDeviceFunction(name);
  };
  dispatch.create_command_pool = reinterpret_cast<PFN_vkCreateCommandPool>(
      resolve("vkCreateCommandPool"));
  dispatch.destroy_command_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(
      resolve("vkDestroyCommandPool"));
  dispatch.allocate_command_buffers =
      reinterpret_cast<PFN_vkAllocateCommandBuffers>(
          resolve("vkAllocateCommandBuffers"));
  dispatch.begin_command_buffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(
      resolve("vkBeginCommandBuffer"));
  dispatch.end_command_buffer = reinterpret_cast<PFN_vkEndCommandBuffer>(
      resolve("vkEndCommandBuffer"));
  dispatch.create_shader_module = reinterpret_cast<PFN_vkCreateShaderModule>(
      resolve("vkCreateShaderModule"));
  dispatch.destroy_shader_module = reinterpret_cast<PFN_vkDestroyShaderModule>(
      resolve("vkDestroyShaderModule"));
  dispatch.create_pipeline_layout =
      reinterpret_cast<PFN_vkCreatePipelineLayout>(
          resolve("vkCreatePipelineLayout"));
  dispatch.destroy_pipeline_layout =
      reinterpret_cast<PFN_vkDestroyPipelineLayout>(
          resolve("vkDestroyPipelineLayout"));
  dispatch.create_compute_pipelines =
      reinterpret_cast<PFN_vkCreateComputePipelines>(
          resolve("vkCreateComputePipelines"));
  dispatch.destroy_pipeline = reinterpret_cast<PFN_vkDestroyPipeline>(
      resolve("vkDestroyPipeline"));
  dispatch.cmd_bind_pipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(
      resolve("vkCmdBindPipeline"));
  dispatch.cmd_dispatch =
      reinterpret_cast<PFN_vkCmdDispatch>(resolve("vkCmdDispatch"));
  dispatch.cmd_pipeline_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
      resolve("vkCmdPipelineBarrier"));
  dispatch.create_fence =
      reinterpret_cast<PFN_vkCreateFence>(resolve("vkCreateFence"));
  dispatch.destroy_fence =
      reinterpret_cast<PFN_vkDestroyFence>(resolve("vkDestroyFence"));
  dispatch.wait_for_fences =
      reinterpret_cast<PFN_vkWaitForFences>(resolve("vkWaitForFences"));
  dispatch.get_fence_status =
      reinterpret_cast<PFN_vkGetFenceStatus>(resolve("vkGetFenceStatus"));
  dispatch.queue_submit =
      reinterpret_cast<PFN_vkQueueSubmit>(resolve("vkQueueSubmit"));
  dispatch.create_descriptor_set_layout =
      reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
          resolve("vkCreateDescriptorSetLayout"));
  dispatch.destroy_descriptor_set_layout =
      reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
          resolve("vkDestroyDescriptorSetLayout"));
  dispatch.create_descriptor_pool =
      reinterpret_cast<PFN_vkCreateDescriptorPool>(
          resolve("vkCreateDescriptorPool"));
  dispatch.destroy_descriptor_pool =
      reinterpret_cast<PFN_vkDestroyDescriptorPool>(
          resolve("vkDestroyDescriptorPool"));
  dispatch.allocate_descriptor_sets =
      reinterpret_cast<PFN_vkAllocateDescriptorSets>(
          resolve("vkAllocateDescriptorSets"));
  dispatch.update_descriptor_sets =
      reinterpret_cast<PFN_vkUpdateDescriptorSets>(
          resolve("vkUpdateDescriptorSets"));
  dispatch.cmd_bind_descriptor_sets =
      reinterpret_cast<PFN_vkCmdBindDescriptorSets>(
          resolve("vkCmdBindDescriptorSets"));
  dispatch.cmd_push_constants =
      reinterpret_cast<PFN_vkCmdPushConstants>(resolve("vkCmdPushConstants"));

  std::string missing;
  const auto require = [&](bool available, const char* name) {
    if (available) {
      return;
    }
    if (!missing.empty()) {
      missing += ", ";
    }
    missing += name;
  };
  require(dispatch.create_command_pool != nullptr, "vkCreateCommandPool");
  require(dispatch.destroy_command_pool != nullptr, "vkDestroyCommandPool");
  require(dispatch.allocate_command_buffers != nullptr,
          "vkAllocateCommandBuffers");
  require(dispatch.begin_command_buffer != nullptr, "vkBeginCommandBuffer");
  require(dispatch.end_command_buffer != nullptr, "vkEndCommandBuffer");
  require(dispatch.create_shader_module != nullptr, "vkCreateShaderModule");
  require(dispatch.destroy_shader_module != nullptr, "vkDestroyShaderModule");
  require(dispatch.create_pipeline_layout != nullptr, "vkCreatePipelineLayout");
  require(dispatch.destroy_pipeline_layout != nullptr,
          "vkDestroyPipelineLayout");
  require(dispatch.create_compute_pipelines != nullptr,
          "vkCreateComputePipelines");
  require(dispatch.destroy_pipeline != nullptr, "vkDestroyPipeline");
  require(dispatch.cmd_bind_pipeline != nullptr, "vkCmdBindPipeline");
  require(dispatch.cmd_dispatch != nullptr, "vkCmdDispatch");
  require(dispatch.create_fence != nullptr, "vkCreateFence");
  require(dispatch.destroy_fence != nullptr, "vkDestroyFence");
  require(dispatch.wait_for_fences != nullptr, "vkWaitForFences");
  require(dispatch.get_fence_status != nullptr, "vkGetFenceStatus");
  require(dispatch.queue_submit != nullptr, "vkQueueSubmit");
  if (missing.empty()) {
    return true;
  }

  result.status = VulkanComputeStatus::kDeviceFunctionUnavailable;
  AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                VulkanComputeDiagnosticCode::kDeviceFunctionUnavailable,
                "Vulkan compute execution is missing required device entry "
                "points: " +
                    missing);
  return false;
}

}  // namespace

struct VulkanComputeExecution::Impl {
  explicit Impl(VulkanDeviceContext& device_context) : context(device_context) {}

  ~Impl() {
    bool has_retained_submission = false;
    for (const std::optional<Submission>& retained_submission : retained) {
      has_retained_submission = has_retained_submission ||
                               retained_submission.has_value();
    }
    if (has_retained_submission) {
      const VkResult wait_idle_result = context.WaitIdle();
      if (IsDeviceLost(wait_idle_result)) {
        device_lost = true;
        context.MarkDeviceLost();
      } else if (wait_idle_result != VK_SUCCESS) {
        // vkDeviceWaitIdle did not establish that submitted work is idle. A
        // destructor cannot return that recoverable failure, and destroying
        // the children here would violate Vulkan's in-flight lifetime rules.
        std::terminate();
      }
    }
    const VkDevice device = context.device();
    for (std::optional<Submission>& retained_submission : retained) {
      if (!retained_submission.has_value()) {
        continue;
      }
      // The executor dies before its required context. Destroy every child
      // even after a timeout or device loss, so the context never destroys its
      // VkDevice while this executor still owns a child handle.
      DestroySubmission(dispatch, device, *retained_submission);
      retained_submission.reset();
    }
  }

  void MarkDeviceLost() noexcept {
    device_lost = true;
    context.MarkDeviceLost();
  }

  VulkanDeviceContext& context;
  ComputeDispatch dispatch;
  std::array<std::optional<Submission>,
             kMaximumVulkanComputeRetainedSubmissions>
      retained;
  std::mutex mutex;
  std::uint64_t next_timeline = 1;
  std::uint64_t completed_timeline = 0;
  bool device_lost = false;
  std::size_t lost_dirty_resource_count = 0;
  bool owns_context_execution_slot = false;
};

namespace {

template <typename ExecutionImpl>
std::size_t RetainedSubmissionCount(const ExecutionImpl& impl) {
  std::size_t count = 0;
  for (const std::optional<Submission>& retained_submission : impl.retained) {
    count += retained_submission.has_value() ? 1U : 0U;
  }
  return count;
}

template <typename ExecutionImpl>
std::optional<Submission>* FindFreeRetainedSlot(ExecutionImpl& impl) {
  for (std::optional<Submission>& retained_submission : impl.retained) {
    if (!retained_submission.has_value()) {
      return &retained_submission;
    }
  }
  return nullptr;
}

template <typename ExecutionImpl>
void DestroyRetainedSubmissions(ExecutionImpl& impl) noexcept {
  const VkDevice device = impl.context.device();
  for (std::optional<Submission>& retained_submission : impl.retained) {
    if (!retained_submission.has_value()) {
      continue;
    }
    DestroySubmission(impl.dispatch, device, *retained_submission);
    retained_submission.reset();
  }
}

template <typename ExecutionImpl>
void DestroyLostRetainedSubmissions(ExecutionImpl &impl) noexcept {
  VulkanGuestBufferCache *cache = nullptr;
  VulkanGuestImageCache *image_cache = nullptr;
  for (std::optional<Submission> &retained_submission : impl.retained) {
    if (!retained_submission.has_value())
      continue;
    if (retained_submission->guest_cache != nullptr)
      cache = retained_submission->guest_cache;
    if (retained_submission->image_cache != nullptr)
      image_cache = retained_submission->image_cache;
    DestroySubmission(impl.dispatch, impl.context.device(),
                      *retained_submission);
    retained_submission.reset();
  }
  if (cache != nullptr) {
    impl.lost_dirty_resource_count = std::max(
        impl.lost_dirty_resource_count, cache->lost_dirty_resource_count());
  }
  if (image_cache != nullptr) {
    impl.lost_dirty_resource_count += image_cache->lost_dirty_resource_count();
  }
}

template <typename ExecutionImpl>
void SnapshotExecutionState(const ExecutionImpl& impl,
                            VulkanComputeResult& result) {
  result.completed_timeline = impl.completed_timeline;
  result.retained_submission_count = RetainedSubmissionCount(impl);
  result.lost_dirty_resource_count = impl.lost_dirty_resource_count;
}

template <typename ExecutionImpl>
bool ReclaimCompletedLocked(ExecutionImpl& impl,
                            VulkanComputeResult& result) {
  const VkDevice device = impl.context.device();
  for (std::optional<Submission>& retained_submission : impl.retained) {
    if (!retained_submission.has_value()) {
      continue;
    }

    const VkResult status = impl.dispatch.get_fence_status(
        device, retained_submission->fence);
    if (status == VK_NOT_READY) {
      continue;
    }
    if (status == VK_SUCCESS) {
      if (retained_submission->guest_preparation.has_value() &&
          retained_submission->guest_cache != nullptr &&
          !retained_submission->guest_cache->Complete(
              *retained_submission->guest_preparation)) {
        result.status = VulkanComputeStatus::kReadbackFailed;
        AddDiagnostic(
            result, VulkanDiagnosticSeverity::kError,
            VulkanComputeDiagnosticCode::kReadbackFailed,
            "Vulkan guest-buffer completion could not publish readback",
            retained_submission->timeline);
        SnapshotExecutionState(impl, result);
        return false;
      }
      if (retained_submission->image_preparation.has_value() &&
          retained_submission->image_cache != nullptr) {
        for (VulkanGuestImagePreparation& image :
             retained_submission->image_preparation->images) {
          if (!retained_submission->image_cache->Complete(image)) {
            result.status = VulkanComputeStatus::kReadbackFailed;
            AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                          VulkanComputeDiagnosticCode::kReadbackFailed,
                          "Vulkan guest-image completion could not publish readback",
                          retained_submission->timeline);
            SnapshotExecutionState(impl, result);
            return false;
          }
        }
      }
      impl.completed_timeline =
          std::max(impl.completed_timeline, retained_submission->timeline);
      AddDiagnostic(result, VulkanDiagnosticSeverity::kInfo,
                    VulkanComputeDiagnosticCode::kSubmissionReclaimed,
                    "reclaimed completed Vulkan compute submission",
                    retained_submission->timeline);
      DestroySubmission(impl.dispatch, device, *retained_submission);
      retained_submission.reset();
      ++result.reclaimed_submission_count;
      continue;
    }

    if (IsDeviceLost(status)) {
      impl.MarkDeviceLost();
      const std::uint64_t lost_timeline = retained_submission->timeline;
      DestroyLostRetainedSubmissions(impl);
      result.status = VulkanComputeStatus::kDeviceLost;
      AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                    VulkanComputeDiagnosticCode::kDeviceLost,
                    "vkGetFenceStatus reported Vulkan device loss while "
          "polling retained compute work (retained lost-dirty resources: " +
              std::to_string(impl.lost_dirty_resource_count) + ")",
                    lost_timeline, status);
    } else {
      result.status = VulkanComputeStatus::kFenceStatusFailed;
      AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                    VulkanComputeDiagnosticCode::kFenceStatusFailed,
                    "vkGetFenceStatus failed while polling retained compute "
                    "work",
                    retained_submission->timeline, status);
    }
    SnapshotExecutionState(impl, result);
    return false;
  }

  SnapshotExecutionState(impl, result);
  return true;
}

template <typename ExecutionImpl>
void AddDeviceLostDiagnostic(ExecutionImpl& impl,
                             VulkanComputeResult& result,
                             std::string message,
                             std::uint64_t timeline,
                             VkResult api_result) {
  impl.MarkDeviceLost();
  DestroyLostRetainedSubmissions(impl);
  result.status = VulkanComputeStatus::kDeviceLost;
  AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                VulkanComputeDiagnosticCode::kDeviceLost, std::move(message) + " (retained lost-dirty resources: " +
                    std::to_string(impl.lost_dirty_resource_count) + ")",
                timeline, api_result);
}

}  // namespace

VulkanComputeExecution::VulkanComputeExecution(std::unique_ptr<Impl> impl)
    noexcept
    : impl_(std::move(impl)) {}

VulkanComputeExecution::~VulkanComputeExecution() {
  if (impl_ == nullptr) {
    return;
  }
  VulkanDeviceContext* const context = &impl_->context;
  const bool owns_context_execution_slot = impl_->owns_context_execution_slot;
  impl_.reset();
  if (owns_context_execution_slot) {
    context->ReleaseComputeExecutionOwner();
  }
}

VulkanComputeExecutionCreateResult VulkanComputeExecution::Create(
    VulkanDeviceContext& context) {
  VulkanComputeExecutionCreateResult result;
  if (context.IsDeviceLost()) {
    result.initialization.status = VulkanComputeStatus::kDeviceLost;
    AddDiagnostic(result.initialization, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kDeviceLost,
                  "Vulkan compute execution is unavailable after device loss");
    return result;
  }
  auto impl = std::make_unique<Impl>(context);
  if (!LoadComputeDispatch(context, impl->dispatch, result.initialization)) {
    return result;
  }

  auto execution = std::unique_ptr<VulkanComputeExecution>(
      new VulkanComputeExecution(std::move(impl)));
  bool context_is_device_lost = false;
  if (!context.TryAcquireComputeExecutionOwner(context_is_device_lost)) {
    if (context_is_device_lost) {
      result.initialization.status = VulkanComputeStatus::kDeviceLost;
      AddDiagnostic(result.initialization, VulkanDiagnosticSeverity::kError,
                    VulkanComputeDiagnosticCode::kDeviceLost,
                    "Vulkan compute execution is unavailable after device loss");
    } else {
      result.initialization.status = VulkanComputeStatus::kExecutionAlreadyOwned;
      AddDiagnostic(
          result.initialization, VulkanDiagnosticSeverity::kError,
          VulkanComputeDiagnosticCode::kExecutionAlreadyOwned,
          "VulkanDeviceContext already has a compute execution owner");
    }
    return result;
  }
  execution->impl_->owns_context_execution_slot = true;
  result.initialization.status = VulkanComputeStatus::kOk;
  result.execution = std::move(execution);
  return result;
}

VulkanComputeResult VulkanComputeExecution::PollCompleted() {
  VulkanComputeResult result;
  std::lock_guard lock(impl_->mutex);
  if (impl_->device_lost || impl_->context.IsDeviceLost()) {
    result.status = VulkanComputeStatus::kDeviceLost;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kDeviceLost,
                  "Vulkan compute execution is unavailable after device loss");
    SnapshotExecutionState(*impl_, result);
    return result;
  }
  (void)ReclaimCompletedLocked(*impl_, result);
  return result;
}

VulkanComputeResult VulkanComputeExecution::Submit(
    std::span<const std::uint32_t> spirv_words,
    std::uint32_t group_count_x,
    std::uint32_t group_count_y,
    std::uint32_t group_count_z,
    std::uint64_t timeout_ns) {
  VulkanComputeResult result;
  if (spirv_words.empty() || group_count_x == 0 || group_count_y == 0 ||
      group_count_z == 0 || timeout_ns == std::numeric_limits<std::uint64_t>::max() ||
      spirv_words.size() >
          std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
    result.status = VulkanComputeStatus::kInvalidArgument;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kInputRejected,
                  "Vulkan compute submission requires SPIR-V words, nonzero "
                  "group counts, and a finite fence timeout");
    return result;
  }

  std::lock_guard lock(impl_->mutex);
  if (impl_->device_lost || impl_->context.IsDeviceLost()) {
    result.status = VulkanComputeStatus::kDeviceLost;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kDeviceLost,
                  "Vulkan compute execution is unavailable after device loss");
    SnapshotExecutionState(*impl_, result);
    return result;
  }
  const std::array<std::uint32_t, 3>& max_work_group_count =
      impl_->context.properties().max_compute_work_group_count;
  if (group_count_x > max_work_group_count[0] ||
      group_count_y > max_work_group_count[1] ||
      group_count_z > max_work_group_count[2]) {
    result.status = VulkanComputeStatus::kInvalidArgument;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kInputRejected,
                  "Vulkan compute dispatch group count exceeds the selected "
                  "device limit");
    SnapshotExecutionState(*impl_, result);
    return result;
  }
  if (!ReclaimCompletedLocked(*impl_, result)) {
    return result;
  }
  if (RetainedSubmissionCount(*impl_) >=
      kMaximumVulkanComputeRetainedSubmissions) {
    result.status = VulkanComputeStatus::kResourceLimit;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kResourceLimit,
                  "Vulkan compute retained timed-out work reached its fixed "
                  "resource limit");
    SnapshotExecutionState(*impl_, result);
    return result;
  }

  std::optional<Submission>* retained_slot = FindFreeRetainedSlot(*impl_);
  if (retained_slot == nullptr) {
    result.status = VulkanComputeStatus::kResourceLimit;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kResourceLimit,
                  "Vulkan compute found no free retained-work slot");
    SnapshotExecutionState(*impl_, result);
    return result;
  }

  const VkDevice device = impl_->context.device();
  SubmissionTransaction transaction(impl_->dispatch, device);
  Submission& submission = transaction.submission();
  const auto fail_before_submit = [&](VulkanComputeStatus status,
                                      VulkanComputeDiagnosticCode code,
                                      const char* operation,
                                      VkResult api_result) {
    if (IsDeviceLost(api_result)) {
      AddDeviceLostDiagnostic(*impl_, result,
                              std::string(operation) + " reported device loss",
                              0, api_result);
    } else {
      result.status = status;
      AddDiagnostic(result, VulkanDiagnosticSeverity::kError, code,
                    std::string(operation) + " failed", 0, api_result);
    }
    SnapshotExecutionState(*impl_, result);
    return result;
  };

  VkCommandPoolCreateInfo command_pool_info{};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_info.queueFamilyIndex = impl_->context.queue_family_index();
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkResult api_result = impl_->dispatch.create_command_pool(
      device, &command_pool_info, nullptr, &command_pool);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(VulkanComputeStatus::kCommandPoolCreationFailed,
                              VulkanComputeDiagnosticCode::kCommandPoolCreationFailed,
                              "vkCreateCommandPool", api_result);
  }
  submission.command_pool = command_pool;

  VkCommandBufferAllocateInfo command_buffer_info{};
  command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_info.commandPool = submission.command_pool;
  command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_info.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  api_result = impl_->dispatch.allocate_command_buffers(
      device, &command_buffer_info, &command_buffer);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(
        VulkanComputeStatus::kCommandBufferAllocationFailed,
        VulkanComputeDiagnosticCode::kCommandBufferAllocationFailed,
        "vkAllocateCommandBuffers", api_result);
  }
  submission.command_buffer = command_buffer;

  VkShaderModuleCreateInfo shader_module_info{};
  shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_info.codeSize = spirv_words.size() * sizeof(std::uint32_t);
  shader_module_info.pCode = spirv_words.data();
  VkShaderModule shader_module = VK_NULL_HANDLE;
  api_result = impl_->dispatch.create_shader_module(
      device, &shader_module_info, nullptr, &shader_module);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(VulkanComputeStatus::kShaderModuleCreationFailed,
                              VulkanComputeDiagnosticCode::kShaderModuleCreationFailed,
                              "vkCreateShaderModule", api_result);
  }
  submission.shader_module = shader_module;

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  api_result = impl_->dispatch.create_pipeline_layout(
      device, &pipeline_layout_info, nullptr, &pipeline_layout);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(
        VulkanComputeStatus::kPipelineLayoutCreationFailed,
        VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed,
        "vkCreatePipelineLayout", api_result);
  }
  submission.pipeline_layout = pipeline_layout;

  VkPipelineShaderStageCreateInfo shader_stage{};
  shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shader_stage.module = submission.shader_module;
  shader_stage.pName = "main";
  VkComputePipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_info.stage = shader_stage;
  pipeline_info.layout = submission.pipeline_layout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  api_result = impl_->dispatch.create_compute_pipelines(
      device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(
        VulkanComputeStatus::kComputePipelineCreationFailed,
        VulkanComputeDiagnosticCode::kComputePipelineCreationFailed,
        "vkCreateComputePipelines", api_result);
  }
  submission.pipeline = pipeline;

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  api_result = impl_->dispatch.create_fence(device, &fence_info, nullptr,
                                            &fence);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(VulkanComputeStatus::kFenceCreationFailed,
                              VulkanComputeDiagnosticCode::kFenceCreationFailed,
                              "vkCreateFence", api_result);
  }
  submission.fence = fence;

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  api_result = impl_->dispatch.begin_command_buffer(submission.command_buffer,
                                                     &begin_info);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(VulkanComputeStatus::kCommandBufferBeginFailed,
                              VulkanComputeDiagnosticCode::kCommandBufferBeginFailed,
                              "vkBeginCommandBuffer", api_result);
  }
  impl_->dispatch.cmd_bind_pipeline(submission.command_buffer,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    submission.pipeline);
  impl_->dispatch.cmd_dispatch(submission.command_buffer, group_count_x,
                               group_count_y, group_count_z);
  api_result = impl_->dispatch.end_command_buffer(submission.command_buffer);
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(VulkanComputeStatus::kCommandBufferEndFailed,
                              VulkanComputeDiagnosticCode::kCommandBufferEndFailed,
                              "vkEndCommandBuffer", api_result);
  }

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &submission.command_buffer;
  submission.timeline = impl_->next_timeline++;
  {
    std::lock_guard queue_lock(impl_->context.queue_mutex());
    api_result = impl_->dispatch.queue_submit(impl_->context.queue(), 1,
                                              &submit_info, submission.fence);
  }
  if (api_result != VK_SUCCESS) {
    return fail_before_submit(VulkanComputeStatus::kQueueSubmitFailed,
                              VulkanComputeDiagnosticCode::kQueueSubmitFailed,
                              "vkQueueSubmit", api_result);
  }

  // A successful submission is in flight. Move it into the fixed retained
  // storage before waiting or constructing any diagnostic that may allocate.
  retained_slot->emplace(transaction.Release());
  Submission& submitted = **retained_slot;
  result.timeline = submitted.timeline;
  const VkFence submitted_fence = submitted.fence;
  api_result = impl_->dispatch.wait_for_fences(device, 1, &submitted_fence,
                                               VK_TRUE, timeout_ns);
  if (api_result == VK_SUCCESS) {
    impl_->completed_timeline =
        std::max(impl_->completed_timeline, submitted.timeline);
    AddDiagnostic(result, VulkanDiagnosticSeverity::kInfo,
                  VulkanComputeDiagnosticCode::kSubmissionCompleted,
                  "Vulkan compute submission completed", submitted.timeline);
    DestroySubmission(impl_->dispatch, device, submitted);
    retained_slot->reset();
    SnapshotExecutionState(*impl_, result);
    return result;
  }

  if (api_result == VK_TIMEOUT) {
    result.status = VulkanComputeStatus::kFenceWaitTimedOut;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kWarning,
                  VulkanComputeDiagnosticCode::kFenceWaitTimedOut,
                  "Vulkan compute fence wait timed out; retaining in-flight "
                  "resources for later polling",
                  result.timeline, api_result);
  } else if (IsDeviceLost(api_result)) {
    AddDeviceLostDiagnostic(*impl_, result,
                            "vkWaitForFences reported device loss for Vulkan "
                            "compute work",
                            result.timeline, api_result);
  } else {
    result.status = VulkanComputeStatus::kFenceWaitFailed;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kFenceWaitFailed,
                  "vkWaitForFences failed for Vulkan compute work; retaining "
                  "resources until a later status poll",
                  result.timeline, api_result);
  }
  SnapshotExecutionState(*impl_, result);
  return result;
}

VulkanComputeResult VulkanComputeExecution::SubmitTranslated(
    const shader::recompiler::CompileResult &compile,
    VulkanGuestBufferCache &cache, VulkanGuestBufferPreparation preparation,
    std::uint32_t group_count_x, std::uint32_t group_count_y,
    std::uint32_t group_count_z, std::uint64_t timeout_ns) {
  return SubmitTranslatedPrepared(compile, cache, std::move(preparation),
                                  nullptr, {}, group_count_x, group_count_y,
                                  group_count_z, timeout_ns);
}

VulkanComputeResult VulkanComputeExecution::SubmitTranslated(
    const shader::recompiler::CompileResult &compile,
    VulkanGuestBufferCache &buffer_cache,
    VulkanGuestBufferPreparation buffer_preparation,
    VulkanGuestImageCache &image_cache,
    VulkanGuestImageSetPreparation image_preparation,
    std::uint32_t group_count_x, std::uint32_t group_count_y,
    std::uint32_t group_count_z, std::uint64_t timeout_ns) {
  return SubmitTranslatedPrepared(compile, buffer_cache,
                                  std::move(buffer_preparation), &image_cache,
                                  std::move(image_preparation), group_count_x,
                                  group_count_y, group_count_z, timeout_ns);
}

VulkanComputeResult VulkanComputeExecution::SubmitTranslatedPrepared(
    const shader::recompiler::CompileResult &compile,
    VulkanGuestBufferCache &cache, VulkanGuestBufferPreparation preparation,
    VulkanGuestImageCache *image_cache,
    VulkanGuestImageSetPreparation image_preparation,
    std::uint32_t group_count_x, std::uint32_t group_count_y,
    std::uint32_t group_count_z, std::uint64_t timeout_ns) {
  VulkanComputeResult result;
  const auto &layout = compile.program.bindings;
  const auto &descriptors = layout.descriptors;
  using DescriptorKind = shader::recompiler::IR::DescriptorBindingKind;
  std::size_t buffer_count = 0;
  std::size_t sampled_count = 0;
  std::size_t storage_count = 0;
  std::size_t sampler_count = 0;
  bool descriptor_shape = true;
  std::vector<std::uint32_t> seen_bindings;
  for (const auto& group : descriptors) {
    if (std::find(seen_bindings.begin(), seen_bindings.end(), group.binding) !=
        seen_bindings.end()) descriptor_shape = false;
    seen_bindings.push_back(group.binding);
    if (group.kind == DescriptorKind::Buffers) {
      ++buffer_count;
      descriptor_shape = descriptor_shape &&
          group.resources.size() == preparation.views.size();
    } else if (group.kind == DescriptorKind::Samplers) {
      sampler_count += group.resources.size();
    } else if (group.kind >= DescriptorKind::Sampled1D &&
               group.kind <= DescriptorKind::StorageUint3D) {
      const bool storage = group.kind >= DescriptorKind::Storage1D &&
                           group.kind <= DescriptorKind::StorageUint3D;
      (storage ? storage_count : sampled_count) += group.resources.size();
    } else {
      descriptor_shape = false;
    }
  }
  if (buffer_count > 1 || (buffer_count == 0 && !preparation.views.empty()) ||
      (image_cache == nullptr && (sampled_count != 0 || storage_count != 0 ||
                                  sampler_count != 0))) {
    descriptor_shape = false;
  }
  const bool image_descriptors_requested =
      sampled_count != 0 || storage_count != 0 || sampler_count != 0;
  const bool prepared_image_leases_missing =
      image_cache != nullptr && image_descriptors_requested && !image_preparation;
  const bool prepared_image_descriptor_count_mismatch =
      image_cache != nullptr && image_preparation &&
      (image_preparation.image_descriptors.size() != sampled_count + storage_count ||
       image_preparation.sampler_descriptors.size() != sampler_count);
  const bool image_cache_missing =
      image_cache == nullptr && image_descriptors_requested;
  if (image_cache != nullptr) {
    descriptor_shape = descriptor_shape && !prepared_image_leases_missing &&
        !prepared_image_descriptor_count_mismatch;
  }
  const std::uint64_t push_end =
      static_cast<std::uint64_t>(layout.push_constant_offset) +
      static_cast<std::uint64_t>(layout.push_constant_size);
  const VkDeviceSize storage_alignment = std::max<VkDeviceSize>(
      impl_->context.properties().min_storage_buffer_offset_alignment, 1);
  bool view_shape = true;
  for (const VulkanGuestBufferView &view : preparation.views) {
    view_shape =
        view_shape && view.descriptor_offset % storage_alignment == 0 &&
        view.data_offset ==
            view.descriptor_offset +
                static_cast<VkDeviceSize>(view.packed_offset_dword) * 4 &&
        view.descriptor_range >=
            static_cast<VkDeviceSize>(view.packed_offset_dword) * 4 +
                view.size &&
        view.descriptor_range <=
            impl_->context.properties().max_storage_buffer_range;
  }
  const bool descriptor_limit_exceeded =
      preparation.views.size() >
          impl_->context.properties().max_per_stage_descriptor_storage_buffers ||
      preparation.views.size() >
          impl_->context.properties().max_descriptor_set_storage_buffers ||
      sampled_count >
          impl_->context.properties().max_per_stage_descriptor_sampled_images ||
      sampled_count >
          impl_->context.properties().max_descriptor_set_sampled_images ||
      storage_count >
          impl_->context.properties().max_per_stage_descriptor_storage_images ||
      storage_count >
          impl_->context.properties().max_descriptor_set_storage_images ||
      sampler_count >
          impl_->context.properties().max_per_stage_descriptor_samplers ||
      sampler_count >
          impl_->context.properties().max_descriptor_set_samplers ||
      preparation.views.size() + sampled_count + storage_count + sampler_count >
          impl_->context.properties().max_per_stage_resources;
  if (!preparation || compile.spirv.empty() || group_count_x == 0 ||
      group_count_y == 0 || group_count_z == 0 ||
      timeout_ns == std::numeric_limits<std::uint64_t>::max() ||
      layout.descriptor_set != 0 || image_cache_missing || !descriptor_shape || !view_shape ||
      descriptor_limit_exceeded ||
      (layout.push_constant_offset & 3U) != 0 ||
      (layout.push_constant_size & 3U) != 0 ||
      push_end > impl_->context.properties().max_push_constants_size ||
      layout.push_constant_size !=
          preparation.shader_data_dwords.size() * sizeof(std::uint32_t)) {
    result.status = descriptor_limit_exceeded ? VulkanComputeStatus::kResourceLimit
                                              : VulkanComputeStatus::kInvalidArgument;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  descriptor_limit_exceeded
                      ? VulkanComputeDiagnosticCode::kResourceLimit
                      : VulkanComputeDiagnosticCode::kInputRejected,
                  descriptor_limit_exceeded
                      ? "translated descriptor resources exceed selected Vulkan device limits"
                      : prepared_image_leases_missing
                          ? "translated image or sampler descriptors are missing prepared image leases"
                          : prepared_image_descriptor_count_mismatch
                              ? "prepared image or sampler descriptor counts do not match the translated layout"
                              : image_cache_missing
                                  ? "translated image or sampler descriptors require prepared image leases"
                                  : "translated compute layout or prepared guest buffers are invalid");
    if (image_cache != nullptr) image_cache->Discard(image_preparation);
    cache.Discard(preparation);
    return result;
  }
  std::lock_guard lock(impl_->mutex);
  if (impl_->device_lost || impl_->context.IsDeviceLost()) {
    result.status = VulkanComputeStatus::kDeviceLost;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kDeviceLost,
                  "translated Vulkan compute is unavailable after device loss");
    if (image_cache != nullptr) image_cache->Discard(image_preparation);
    cache.Discard(preparation);
    return result;
  }
  const auto max_groups =
      impl_->context.properties().max_compute_work_group_count;
  if (group_count_x > max_groups[0] || group_count_y > max_groups[1] ||
      group_count_z > max_groups[2] ||
      !ReclaimCompletedLocked(*impl_, result) ||
      RetainedSubmissionCount(*impl_) >=
          kMaximumVulkanComputeRetainedSubmissions) {
    if (result.status == VulkanComputeStatus::kOk)
      result.status = VulkanComputeStatus::kResourceLimit;
    if (image_cache != nullptr) image_cache->Discard(image_preparation);
    cache.Discard(preparation);
    return result;
  }
  auto *slot = FindFreeRetainedSlot(*impl_);
  if (slot == nullptr) {
    result.status = VulkanComputeStatus::kResourceLimit;
    if (image_cache != nullptr) image_cache->Discard(image_preparation);
    cache.Discard(preparation);
    return result;
  }
  const auto required =
      impl_->dispatch.create_descriptor_set_layout != nullptr &&
      impl_->dispatch.destroy_descriptor_set_layout != nullptr &&
      impl_->dispatch.create_descriptor_pool != nullptr &&
      impl_->dispatch.destroy_descriptor_pool != nullptr &&
      impl_->dispatch.allocate_descriptor_sets != nullptr &&
      impl_->dispatch.update_descriptor_sets != nullptr &&
      impl_->dispatch.cmd_bind_descriptor_sets != nullptr &&
      impl_->dispatch.cmd_push_constants != nullptr &&
      impl_->dispatch.cmd_pipeline_barrier != nullptr;
  if (!required) {
    result.status = VulkanComputeStatus::kDeviceFunctionUnavailable;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kDeviceFunctionUnavailable,
                  "translated Vulkan compute is missing descriptor or "
                  "push-constant entry points");
    if (image_cache != nullptr) image_cache->Discard(image_preparation);
    cache.Discard(preparation);
    return result;
  }
  const VkDevice device = impl_->context.device();
  SubmissionTransaction transaction(impl_->dispatch, device);
  Submission &submission = transaction.submission();
  submission.guest_cache = &cache;
  submission.guest_preparation.emplace(std::move(preparation));
  submission.image_cache = image_cache;
  if (image_cache != nullptr)
    submission.image_preparation.emplace(std::move(image_preparation));
  const auto fail = [&](VulkanComputeStatus status,
                        VulkanComputeDiagnosticCode code, const char *name,
                        VkResult value) {
    if (IsDeviceLost(value)) {
      AddDeviceLostDiagnostic(
          *impl_, result,
          std::string(name) + " reported device loss for translated Vulkan compute",
          0, value);
      SnapshotExecutionState(*impl_, result);
      return result;
    }
    result.status = status;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  code,
                  std::string(name) + " failed", 0, value);
    return result;
  };
  const auto reject_prepared_descriptor = [&](const char* message) {
    result.status = VulkanComputeStatus::kInvalidArgument;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kInputRejected, message);
    return result;
  };
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  std::vector<VkDescriptorPoolSize> pool_sizes;
  bindings.reserve(descriptors.size());
  const auto add_pool = [&](VkDescriptorType type, std::uint32_t count) {
    if (count == 0) return;
    for (auto& size : pool_sizes) {
      if (size.type == type) { size.descriptorCount += count; return; }
    }
    pool_sizes.push_back({type, count});
  };
  for (const auto& group : descriptors) {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    if (group.kind == DescriptorKind::Buffers) type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    else if (group.kind == DescriptorKind::Samplers) type = VK_DESCRIPTOR_TYPE_SAMPLER;
    else if (group.kind >= DescriptorKind::Storage1D &&
             group.kind <= DescriptorKind::StorageUint3D) type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    else if (group.kind >= DescriptorKind::Sampled1D &&
             group.kind <= DescriptorKind::SampledUint3D) type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM || group.resources.empty()) {
      result.status = VulkanComputeStatus::kInvalidArgument;
      AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                    VulkanComputeDiagnosticCode::kInputRejected,
                    "translated descriptor group is empty or unsupported");
      return result;
    }
    bindings.push_back({group.binding, type,
                        static_cast<std::uint32_t>(group.resources.size()),
                        VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    add_pool(type, static_cast<std::uint32_t>(group.resources.size()));
  }
  VkDescriptorSetLayoutCreateInfo set_layout_info{};
  set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  set_layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
  set_layout_info.pBindings = bindings.empty() ? nullptr : bindings.data();
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkResult api = impl_->dispatch.create_descriptor_set_layout(
      device, &set_layout_info, nullptr, &descriptor_set_layout);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kPipelineLayoutCreationFailed,
                VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed,
                "vkCreateDescriptorSetLayout", api);
  submission.descriptor_set_layout = descriptor_set_layout;
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
  pool_info.pPoolSizes = pool_sizes.empty() ? nullptr : pool_sizes.data();
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  api = impl_->dispatch.create_descriptor_pool(device, &pool_info, nullptr,
                                               &descriptor_pool);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kPipelineLayoutCreationFailed,
                VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed,
                "vkCreateDescriptorPool", api);
  submission.descriptor_pool = descriptor_pool;
  VkDescriptorSetAllocateInfo set_info{};
  set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_info.descriptorPool = submission.descriptor_pool;
  set_info.descriptorSetCount = 1;
  set_info.pSetLayouts = &submission.descriptor_set_layout;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  api = impl_->dispatch.allocate_descriptor_sets(device, &set_info,
                                                 &descriptor_set);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kPipelineLayoutCreationFailed,
                VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed,
                "vkAllocateDescriptorSets", api);
  submission.descriptor_set = descriptor_set;
  std::vector<VkDescriptorBufferInfo> buffer_infos;
  std::vector<VkDescriptorImageInfo> image_infos;
  std::vector<VkWriteDescriptorSet> writes;
  buffer_infos.reserve(submission.guest_preparation->views.size());
  image_infos.reserve(sampled_count + storage_count + sampler_count);
  writes.reserve(bindings.size());
  for (const auto& binding : bindings) {
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = submission.descriptor_set;
    write.dstBinding = binding.binding;
    write.descriptorCount = binding.descriptorCount;
    write.descriptorType = binding.descriptorType;
    if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
      const auto group = std::find_if(descriptors.begin(), descriptors.end(),
          [&](const auto& candidate) { return candidate.binding == binding.binding; });
      const std::size_t first = buffer_infos.size();
      for (std::size_t i = 0; i < group->resources.size(); ++i) {
        const auto& view = submission.guest_preparation->views[i];
        buffer_infos.push_back({submission.guest_preparation->buffer,
                                view.descriptor_offset, view.descriptor_range});
      }
      write.pBufferInfo = buffer_infos.data() + first;
    } else {
      const std::size_t first = image_infos.size();
      if (submission.image_preparation == std::nullopt)
        return reject_prepared_descriptor(
            "translated image or sampler binding is missing prepared image leases");
      if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
        for (const auto& descriptor : submission.image_preparation->sampler_descriptors) {
          if (descriptor.binding != binding.binding) continue;
          if (descriptor.array_index != image_infos.size() - first ||
              descriptor.sampler == VK_NULL_HANDLE) {
            result.status = VulkanComputeStatus::kInvalidArgument;
            AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                          VulkanComputeDiagnosticCode::kInputRejected,
                          "prepared sampler descriptor does not match its binding");
            return result;
          }
          image_infos.push_back({descriptor.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
        }
      } else {
        for (const auto& descriptor : submission.image_preparation->image_descriptors) {
          if (descriptor.binding != binding.binding) continue;
          if (descriptor.array_index != image_infos.size() - first ||
              descriptor.descriptor_type != binding.descriptorType ||
              descriptor.view == VK_NULL_HANDLE) {
            result.status = VulkanComputeStatus::kInvalidArgument;
            AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                          VulkanComputeDiagnosticCode::kInputRejected,
                          "prepared image descriptor does not match its binding");
            return result;
          }
          VkImageLayout resolved = descriptor.layout;
          if (descriptor.preparation_index >=
              submission.image_preparation->images.size())
            return reject_prepared_descriptor(
                "prepared image descriptor references an invalid image lease");
          for (const auto& other : submission.image_preparation->image_descriptors) {
            if (other.preparation_index == descriptor.preparation_index &&
                other.descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
              resolved = VK_IMAGE_LAYOUT_GENERAL;
              break;
            }
          }
          image_infos.push_back({VK_NULL_HANDLE, descriptor.view, resolved});
        }
      }
      if (image_infos.size() - first != binding.descriptorCount)
        return reject_prepared_descriptor(
            "prepared image or sampler descriptors are missing or extra for a binding");
      write.pImageInfo = image_infos.data() + first;
    }
    writes.push_back(write);
  }
  if (!writes.empty())
    impl_->dispatch.update_descriptor_sets(device,
                                           static_cast<std::uint32_t>(writes.size()),
                                           writes.data(), 0, nullptr);
  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push.offset = layout.push_constant_offset;
  push.size = layout.push_constant_size;
  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &submission.descriptor_set_layout;
  pipeline_layout_info.pushConstantRangeCount = push.size == 0 ? 0 : 1;
  pipeline_layout_info.pPushConstantRanges = push.size == 0 ? nullptr : &push;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  api = impl_->dispatch.create_pipeline_layout(device, &pipeline_layout_info,
                                               nullptr, &pipeline_layout);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kPipelineLayoutCreationFailed,
                VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed,
                "vkCreatePipelineLayout", api);
  submission.pipeline_layout = pipeline_layout;
  VkShaderModuleCreateInfo module_info{};
  module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  module_info.codeSize = compile.spirv.size() * sizeof(std::uint32_t);
  module_info.pCode = compile.spirv.data();
  VkShaderModule shader_module = VK_NULL_HANDLE;
  api = impl_->dispatch.create_shader_module(device, &module_info, nullptr,
                                             &shader_module);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kShaderModuleCreationFailed,
                VulkanComputeDiagnosticCode::kShaderModuleCreationFailed,
                "vkCreateShaderModule", api);
  submission.shader_module = shader_module;
  VkPipelineShaderStageCreateInfo stage{};
  stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = submission.shader_module;
  stage.pName = "main";
  VkComputePipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_info.stage = stage;
  pipeline_info.layout = submission.pipeline_layout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  api = impl_->dispatch.create_compute_pipelines(
      device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kComputePipelineCreationFailed,
                VulkanComputeDiagnosticCode::kComputePipelineCreationFailed,
                "vkCreateComputePipelines", api);
  submission.pipeline = pipeline;
  VkCommandPoolCreateInfo cp{};
  cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cp.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  cp.queueFamilyIndex = impl_->context.queue_family_index();
  VkCommandPool command_pool = VK_NULL_HANDLE;
  api =
      impl_->dispatch.create_command_pool(device, &cp, nullptr, &command_pool);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kCommandPoolCreationFailed,
                VulkanComputeDiagnosticCode::kCommandPoolCreationFailed,
                "vkCreateCommandPool", api);
  submission.command_pool = command_pool;
  VkCommandBufferAllocateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cb.commandPool = submission.command_pool;
  cb.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cb.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  api = impl_->dispatch.allocate_command_buffers(device, &cb, &command_buffer);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kCommandBufferAllocationFailed,
                VulkanComputeDiagnosticCode::kCommandBufferAllocationFailed,
                "vkAllocateCommandBuffers", api);
  submission.command_buffer = command_buffer;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  api = impl_->dispatch.create_fence(device, &fence_info, nullptr, &fence);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kFenceCreationFailed,
                VulkanComputeDiagnosticCode::kFenceCreationFailed,
                "vkCreateFence", api);
  submission.fence = fence;
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  api = impl_->dispatch.begin_command_buffer(submission.command_buffer, &begin);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kCommandBufferBeginFailed,
                VulkanComputeDiagnosticCode::kCommandBufferBeginFailed,
                "vkBeginCommandBuffer", api);
  impl_->dispatch.cmd_bind_pipeline(submission.command_buffer,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    submission.pipeline);
  impl_->dispatch.cmd_bind_descriptor_sets(
      submission.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
      submission.pipeline_layout, 0, 1, &submission.descriptor_set, 0, nullptr);
  if (!submission.guest_preparation->shader_data_dwords.empty()) {
    impl_->dispatch.cmd_push_constants(
        submission.command_buffer, submission.pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT, layout.push_constant_offset,
        static_cast<std::uint32_t>(
            submission.guest_preparation->shader_data_dwords.size() * 4),
        submission.guest_preparation->shader_data_dwords.data());
  }
  if (submission.image_preparation.has_value()) {
    for (std::size_t index = 0;
         index < submission.image_preparation->images.size(); ++index) {
      VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      VkAccessFlags access = 0;
      for (const auto& descriptor : submission.image_preparation->image_descriptors) {
        if (descriptor.preparation_index != index) continue;
        if (descriptor.descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
          image_layout = VK_IMAGE_LAYOUT_GENERAL;
        if (descriptor.shader_reads) access |= VK_ACCESS_SHADER_READ_BIT;
        if (descriptor.shader_writes) access |= VK_ACCESS_SHADER_WRITE_BIT;
      }
      if (access == 0) access = VK_ACCESS_SHADER_READ_BIT;
      if (!submission.image_cache->RecordUpload(
              submission.command_buffer,
              submission.image_preparation->images[index], image_layout,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, access)) {
        result.status = VulkanComputeStatus::kReadbackFailed;
        AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                      VulkanComputeDiagnosticCode::kReadbackFailed,
                      "Vulkan guest-image upload recording failed");
        return result;
      }
    }
  }
  impl_->dispatch.cmd_dispatch(submission.command_buffer, group_count_x,
                               group_count_y, group_count_z);
  if (submission.image_preparation.has_value()) {
    for (auto& image : submission.image_preparation->images) {
      if (image.writable && !submission.image_cache->RecordReadback(
          submission.command_buffer, image, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT)) {
        result.status = VulkanComputeStatus::kReadbackFailed;
        AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                      VulkanComputeDiagnosticCode::kReadbackFailed,
                      "Vulkan guest-image readback recording failed");
        return result;
      }
    }
  }
  const bool shader_writes = std::any_of(
      submission.guest_preparation->views.begin(),
      submission.guest_preparation->views.end(),
      [](const VulkanGuestBufferView &view) { return view.shader_writes; });
  if (shader_writes) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    impl_->dispatch.cmd_pipeline_barrier(
        submission.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
  }
  api = impl_->dispatch.end_command_buffer(submission.command_buffer);
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kCommandBufferEndFailed,
                VulkanComputeDiagnosticCode::kCommandBufferEndFailed,
                "vkEndCommandBuffer", api);
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &submission.command_buffer;
  submission.timeline = impl_->next_timeline++;
  {
    std::lock_guard queue_lock(impl_->context.queue_mutex());
    api = impl_->dispatch.queue_submit(impl_->context.queue(), 1, &submit,
                                       submission.fence);
  }
  if (api != VK_SUCCESS)
    return fail(VulkanComputeStatus::kQueueSubmitFailed,
                VulkanComputeDiagnosticCode::kQueueSubmitFailed,
                "vkQueueSubmit", api);
  slot->emplace(transaction.Release());
  Submission &submitted = **slot;
  result.timeline = submitted.timeline;
  if (!submitted.guest_cache->MarkSubmitted(*submitted.guest_preparation)) {
    result.status = VulkanComputeStatus::kReadbackFailed;
    AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                  VulkanComputeDiagnosticCode::kReadbackFailed,
                  "submitted Vulkan guest buffer could not be marked GPU-dirty",
                  result.timeline);
    SnapshotExecutionState(*impl_, result);
    return result;
  }
  if (submitted.image_preparation.has_value()) {
    for (auto& image : submitted.image_preparation->images) {
      if (!submitted.image_cache->MarkSubmitted(image)) {
        result.status = VulkanComputeStatus::kReadbackFailed;
        AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                      VulkanComputeDiagnosticCode::kReadbackFailed,
                      "submitted Vulkan guest image could not be marked GPU-dirty",
                      result.timeline);
        SnapshotExecutionState(*impl_, result);
        return result;
      }
    }
  }
  const VkFence submitted_fence = submitted.fence;
  api = impl_->dispatch.wait_for_fences(device, 1, &submitted_fence, VK_TRUE,
                                        timeout_ns);
  if (api == VK_SUCCESS) {
    if (!submitted.guest_cache->Complete(*submitted.guest_preparation)) {
      result.status = VulkanComputeStatus::kReadbackFailed;
      AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                    VulkanComputeDiagnosticCode::kReadbackFailed,
                    "signalled Vulkan guest-buffer submission retained after "
                    "readback failure",
                    submitted.timeline);
      SnapshotExecutionState(*impl_, result);
      return result;
    }
    if (submitted.image_preparation.has_value()) {
      for (auto& image : submitted.image_preparation->images) {
        if (!submitted.image_cache->Complete(image)) {
          result.status = VulkanComputeStatus::kReadbackFailed;
          AddDiagnostic(result, VulkanDiagnosticSeverity::kError,
                        VulkanComputeDiagnosticCode::kReadbackFailed,
                        "signalled Vulkan guest-image submission retained after readback failure",
                        submitted.timeline);
          SnapshotExecutionState(*impl_, result);
          return result;
        }
      }
    }
    impl_->completed_timeline =
        std::max(impl_->completed_timeline, submitted.timeline);
    DestroySubmission(impl_->dispatch, device, submitted);
    slot->reset();
    SnapshotExecutionState(*impl_, result);
    return result;
  }
  if (api == VK_TIMEOUT) {
    result.status = VulkanComputeStatus::kFenceWaitTimedOut;
    AddDiagnostic(
        result, VulkanDiagnosticSeverity::kWarning,
        VulkanComputeDiagnosticCode::kFenceWaitTimedOut,
        "translated Vulkan compute timed out; retaining guest-buffer lease",
        result.timeline, api);
  } else if (IsDeviceLost(api)) {
    AddDeviceLostDiagnostic(
        *impl_, result,
        "vkWaitForFences reported device loss for translated Vulkan compute",
        result.timeline, api);
  } else {
    result.status = VulkanComputeStatus::kFenceWaitFailed;
  }
  SnapshotExecutionState(*impl_, result);
  return result;
}

const char* VulkanComputeStatusName(VulkanComputeStatus status) noexcept {
  switch (status) {
    case VulkanComputeStatus::kOk:
      return "ok";
    case VulkanComputeStatus::kInvalidArgument:
      return "invalid_argument";
    case VulkanComputeStatus::kContextUnavailable:
      return "context_unavailable";
    case VulkanComputeStatus::kExecutionAlreadyOwned:
      return "execution_already_owned";
    case VulkanComputeStatus::kDeviceFunctionUnavailable:
      return "device_function_unavailable";
    case VulkanComputeStatus::kCommandPoolCreationFailed:
      return "command_pool_creation_failed";
    case VulkanComputeStatus::kCommandBufferAllocationFailed:
      return "command_buffer_allocation_failed";
    case VulkanComputeStatus::kShaderModuleCreationFailed:
      return "shader_module_creation_failed";
    case VulkanComputeStatus::kPipelineLayoutCreationFailed:
      return "pipeline_layout_creation_failed";
    case VulkanComputeStatus::kComputePipelineCreationFailed:
      return "compute_pipeline_creation_failed";
    case VulkanComputeStatus::kFenceCreationFailed:
      return "fence_creation_failed";
    case VulkanComputeStatus::kCommandBufferBeginFailed:
      return "command_buffer_begin_failed";
    case VulkanComputeStatus::kCommandBufferEndFailed:
      return "command_buffer_end_failed";
    case VulkanComputeStatus::kQueueSubmitFailed:
      return "queue_submit_failed";
    case VulkanComputeStatus::kFenceWaitTimedOut:
      return "fence_wait_timed_out";
    case VulkanComputeStatus::kFenceWaitFailed:
      return "fence_wait_failed";
    case VulkanComputeStatus::kFenceStatusFailed:
      return "fence_status_failed";
  case VulkanComputeStatus::kReadbackFailed:
    return "readback_failed";
    case VulkanComputeStatus::kDeviceLost:
      return "device_lost";
    case VulkanComputeStatus::kResourceLimit:
      return "resource_limit";
  }
  return "unknown";
}

const char* VulkanComputeDiagnosticCodeName(
    VulkanComputeDiagnosticCode code) noexcept {
  switch (code) {
    case VulkanComputeDiagnosticCode::kInputRejected:
      return "input_rejected";
    case VulkanComputeDiagnosticCode::kExecutionAlreadyOwned:
      return "execution_already_owned";
    case VulkanComputeDiagnosticCode::kDeviceFunctionUnavailable:
      return "device_function_unavailable";
    case VulkanComputeDiagnosticCode::kCommandPoolCreationFailed:
      return "command_pool_creation_failed";
    case VulkanComputeDiagnosticCode::kCommandBufferAllocationFailed:
      return "command_buffer_allocation_failed";
    case VulkanComputeDiagnosticCode::kShaderModuleCreationFailed:
      return "shader_module_creation_failed";
    case VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed:
      return "pipeline_layout_creation_failed";
    case VulkanComputeDiagnosticCode::kComputePipelineCreationFailed:
      return "compute_pipeline_creation_failed";
    case VulkanComputeDiagnosticCode::kFenceCreationFailed:
      return "fence_creation_failed";
    case VulkanComputeDiagnosticCode::kCommandBufferBeginFailed:
      return "command_buffer_begin_failed";
    case VulkanComputeDiagnosticCode::kCommandBufferEndFailed:
      return "command_buffer_end_failed";
    case VulkanComputeDiagnosticCode::kQueueSubmitFailed:
      return "queue_submit_failed";
    case VulkanComputeDiagnosticCode::kSubmissionCompleted:
      return "submission_completed";
    case VulkanComputeDiagnosticCode::kFenceWaitTimedOut:
      return "fence_wait_timed_out";
    case VulkanComputeDiagnosticCode::kFenceWaitFailed:
      return "fence_wait_failed";
    case VulkanComputeDiagnosticCode::kFenceStatusFailed:
      return "fence_status_failed";
    case VulkanComputeDiagnosticCode::kReadbackFailed:
    return "readback_failed";
  case VulkanComputeDiagnosticCode::kSubmissionReclaimed:
      return "submission_reclaimed";
    case VulkanComputeDiagnosticCode::kDeviceLost:
      return "device_lost";
    case VulkanComputeDiagnosticCode::kResourceLimit:
      return "resource_limit";
    case VulkanComputeDiagnosticCode::kContextUnavailable:
      return "context_unavailable";
  }
  return "unknown";
}

}  // namespace kajps5::gpu::vulkan
