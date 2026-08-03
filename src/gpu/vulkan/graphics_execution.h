// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 renderer/pipeline/{pipelineCache,renderDraw}.*
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior reference: SharpEmu VulkanVideoPresenter.cs at
// 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gpu/shader/recompiler/ShaderRecompiler.h"
#include "gpu/vulkan/graphics_bindings.h"

namespace kajps5::gpu::vulkan {

enum class VulkanGraphicsTopology : std::uint8_t {
  kTriangleList,
  kTriangleStrip,
  kLineList,
  kLineStrip,
  kPointList,
};
enum class VulkanGraphicsCullMode : std::uint8_t { kNone, kFront, kBack };
enum class VulkanGraphicsFrontFace : std::uint8_t {
  kCounterClockwise,
  kClockwise,
};

struct VulkanGraphicsBlendState {
  bool enabled = false;
  VkBlendFactor source_color = VK_BLEND_FACTOR_ONE;
  VkBlendFactor destination_color = VK_BLEND_FACTOR_ZERO;
  VkBlendOp color_op = VK_BLEND_OP_ADD;
  VkBlendFactor source_alpha = VK_BLEND_FACTOR_ONE;
  VkBlendFactor destination_alpha = VK_BLEND_FACTOR_ZERO;
  VkBlendOp alpha_op = VK_BLEND_OP_ADD;
  VkColorComponentFlags write_mask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
};

struct VulkanGraphicsViewportState {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float min_depth = 0.0f;
  float max_depth = 1.0f;
  VkRect2D scissor{};
};

struct VulkanTranslatedDrawRequest {
  const shader::recompiler::CompileResult* vertex = nullptr;
  const shader::recompiler::CompileResult* pixel = nullptr;
  GuestImageLayoutInput color_target{};
  VulkanGraphicsTopology topology = VulkanGraphicsTopology::kTriangleList;
  VulkanGraphicsCullMode cull_mode = VulkanGraphicsCullMode::kNone;
  VulkanGraphicsFrontFace front_face = VulkanGraphicsFrontFace::kCounterClockwise;
  VulkanGraphicsBlendState blend{};
  VulkanGraphicsViewportState viewport{};
  std::uint32_t vertex_count = 0;
  std::uint32_t instance_count = 1;
  std::uint32_t first_vertex = 0;
  std::uint32_t first_instance = 0;
  std::uint64_t timeout_ns = 1'000'000'000ULL;
};

enum class VulkanGraphicsStatus : std::uint8_t {
  kOk,
  kInvalidArgument,
  kUnsupported,
  kContextUnavailable,
  kDeviceFunctionUnavailable,
  kResourceLimit,
  kReadbackFailed,
  kFenceWaitTimedOut,
  kFenceWaitFailed,
  kFenceStatusFailed,
  kDeviceLost,
};
enum class VulkanGraphicsDiagnosticCode : std::uint8_t {
  kInputRejected,
  kUnsupportedState,
  kFormatUnsupported,
  kDeviceFunctionUnavailable,
  kPipelineCreationFailed,
  kSubmissionReclaimed,
  kFenceWaitTimedOut,
  kReadbackFailed,
  kDeviceLost,
  kResourceLimit,
};
enum class VulkanGraphicsDiagnosticSeverity : std::uint8_t { kInfo, kWarning, kError };
struct VulkanGraphicsDiagnostic {
  VulkanGraphicsDiagnosticSeverity severity{};
  VulkanGraphicsDiagnosticCode code{};
  std::uint64_t timeline = 0;
  std::int32_t api_result = VK_SUCCESS;
  std::string message;
};
struct VulkanGraphicsResult {
  VulkanGraphicsStatus status = VulkanGraphicsStatus::kOk;
  std::uint64_t timeline = 0;
  std::uint64_t completed_timeline = 0;
  std::size_t retained_submission_count = 0;
  std::size_t reclaimed_submission_count = 0;
  std::size_t lost_dirty_resource_count = 0;
  std::vector<VulkanGraphicsDiagnostic> diagnostics;
  [[nodiscard]] explicit operator bool() const noexcept {
    return status == VulkanGraphicsStatus::kOk;
  }
};
struct VulkanGraphicsExecutionCreateResult {
  VulkanGraphicsResult initialization;
  std::unique_ptr<class VulkanGraphicsExecution> execution;
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(execution);
  }
};

class VulkanDeviceContext;
class VulkanGraphicsExecution final {
 public:
  [[nodiscard]] static VulkanGraphicsExecutionCreateResult Create(
      VulkanDeviceContext& context);
  ~VulkanGraphicsExecution();
  VulkanGraphicsExecution(const VulkanGraphicsExecution&) = delete;
  VulkanGraphicsExecution& operator=(const VulkanGraphicsExecution&) = delete;
  [[nodiscard]] VulkanGraphicsResult Submit(
      const VulkanTranslatedDrawRequest& request,
      VulkanGuestBufferCache& buffer_cache,
      VulkanGuestBufferPreparation vertex_buffers,
      VulkanGuestBufferPreparation pixel_buffers,
      VulkanGuestImageCache& image_cache,
      VulkanGuestImageSetPreparation vertex_images,
      VulkanGuestImageSetPreparation pixel_images,
      VulkanGuestImagePreparation target, VulkanGraphicsBindingPlan plan);
  [[nodiscard]] VulkanGraphicsResult PollCompleted();

 private:
  struct Impl;
  explicit VulkanGraphicsExecution(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kajps5::gpu::vulkan
