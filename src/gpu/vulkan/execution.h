// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/{commandScheduler,masterSemaphore,render}.*
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "gpu/vulkan/device.h"

namespace kajps5::gpu::vulkan {

inline constexpr std::uint64_t kDefaultVulkanComputeFenceWaitNanoseconds =
    1'000'000'000ULL;
inline constexpr std::size_t kMaximumVulkanComputeRetainedSubmissions = 8;

enum class VulkanComputeStatus {
  kOk,
  kInvalidArgument,
  kContextUnavailable,
  kExecutionAlreadyOwned,
  kDeviceFunctionUnavailable,
  kCommandPoolCreationFailed,
  kCommandBufferAllocationFailed,
  kShaderModuleCreationFailed,
  kPipelineLayoutCreationFailed,
  kComputePipelineCreationFailed,
  kFenceCreationFailed,
  kCommandBufferBeginFailed,
  kCommandBufferEndFailed,
  kQueueSubmitFailed,
  kFenceWaitTimedOut,
  kFenceWaitFailed,
  kFenceStatusFailed,
  kDeviceLost,
  kResourceLimit,
};

enum class VulkanComputeDiagnosticCode {
  kInputRejected,
  kExecutionAlreadyOwned,
  kDeviceFunctionUnavailable,
  kCommandPoolCreationFailed,
  kCommandBufferAllocationFailed,
  kShaderModuleCreationFailed,
  kPipelineLayoutCreationFailed,
  kComputePipelineCreationFailed,
  kFenceCreationFailed,
  kCommandBufferBeginFailed,
  kCommandBufferEndFailed,
  kQueueSubmitFailed,
  kSubmissionCompleted,
  kFenceWaitTimedOut,
  kFenceWaitFailed,
  kFenceStatusFailed,
  kSubmissionReclaimed,
  kDeviceLost,
  kResourceLimit,
  kContextUnavailable,
};

struct VulkanComputeDiagnostic {
  VulkanDiagnosticSeverity severity = VulkanDiagnosticSeverity::kInfo;
  VulkanComputeDiagnosticCode code =
      VulkanComputeDiagnosticCode::kSubmissionCompleted;
  std::uint64_t timeline = 0;
  std::int32_t api_result = VK_SUCCESS;
  std::string message;
};

// A completed submission reports its own timeline in timeline. A timed-out
// submission reports that same nonzero timeline but remains retained until a
// later PollCompleted observes its fence signalled.
struct VulkanComputeResult {
  VulkanComputeStatus status = VulkanComputeStatus::kOk;
  std::uint64_t timeline = 0;
  std::uint64_t completed_timeline = 0;
  std::size_t retained_submission_count = 0;
  std::size_t reclaimed_submission_count = 0;
  std::vector<VulkanComputeDiagnostic> diagnostics;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == VulkanComputeStatus::kOk;
  }
};

class VulkanComputeExecution;

struct VulkanComputeExecutionCreateResult {
  VulkanComputeResult initialization;
  std::unique_ptr<VulkanComputeExecution> execution;

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(initialization) && execution != nullptr;
  }
};

// One optional, runtime-owned compute executor layered on an already-created
// VulkanDeviceContext. It never compiles or translates guest shaders: callers
// provide validated SPIR-V words produced by the existing shader recompiler.
// Every dispatch owns a fresh primary command buffer, fence, shader module,
// empty pipeline layout, and compute pipeline until it is known complete.
// The supplied VulkanDeviceContext must outlive this executor.
class VulkanComputeExecution final {
 public:
  [[nodiscard]] static VulkanComputeExecutionCreateResult Create(
      VulkanDeviceContext& context);

  ~VulkanComputeExecution();

  VulkanComputeExecution(const VulkanComputeExecution&) = delete;
  VulkanComputeExecution& operator=(const VulkanComputeExecution&) = delete;
  VulkanComputeExecution(VulkanComputeExecution&&) = delete;
  VulkanComputeExecution& operator=(VulkanComputeExecution&&) = delete;

  // timeout_ns must be finite (UINT64_MAX is rejected). Zero is a valid poll
  // timeout. No buffers, descriptors, graphics pipeline, surface, or
  // presentation resources are created by this operation.
  [[nodiscard]] VulkanComputeResult Submit(
      std::span<const std::uint32_t> spirv_words,
      std::uint32_t group_count_x,
      std::uint32_t group_count_y,
      std::uint32_t group_count_z,
      std::uint64_t timeout_ns =
          kDefaultVulkanComputeFenceWaitNanoseconds);

  // Nonblocking fence polling for retained timed-out work. Reclamation never
  // resets or frees a command resource before its individual fence signals.
  [[nodiscard]] VulkanComputeResult PollCompleted();

 private:
  struct Impl;

  explicit VulkanComputeExecution(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* VulkanComputeStatusName(
    VulkanComputeStatus status) noexcept;
[[nodiscard]] const char* VulkanComputeDiagnosticCodeName(
    VulkanComputeDiagnosticCode code) noexcept;

}  // namespace kajps5::gpu::vulkan
