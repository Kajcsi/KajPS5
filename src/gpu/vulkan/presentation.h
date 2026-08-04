// Copyright (C) 2026 KajPS5 contributors
// Architecture adapted from KytyPS5 presentation/window swapchain flow at
// fb5ecec455cf6c67154134429485ffccbfc34203.  Behavioral constraints follow
// SharpEmu VulkanVideoPresenter tests at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gpu/image_layout.h"
#include "gpu/vulkan/image_cache.h"

namespace kajps5::gpu::vulkan {

enum class VulkanPresentationStatus : std::uint8_t {
  kOk, kContextUnavailable, kInvalidFrame, kSurfaceUnavailable, kMinimized,
  kAcquireTimeout, kRetainedWorkPending, kRecreateRequired, kDeviceLost,
  kDeviceFailure,
};

struct VulkanPresentationDiagnostic {
  VulkanPresentationStatus status = VulkanPresentationStatus::kOk;
  std::int32_t api_result = VK_SUCCESS;
  std::string message;
};
struct VulkanPresentationResult {
  VulkanPresentationStatus status = VulkanPresentationStatus::kOk;
  std::uint64_t timeline = 0;
  std::vector<VulkanPresentationDiagnostic> diagnostics;
  [[nodiscard]] explicit operator bool() const noexcept {
    return status == VulkanPresentationStatus::kOk;
  }
};

struct VulkanPresentationChoice {
  VkSurfaceFormatKHR format{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D extent{};
  std::uint32_t image_count = 0;
};

struct VulkanPresentationSwapchainPlan {
  VulkanPresentationChoice choice{};
  VkImageUsageFlags image_usage = 0;
  VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  bool retry_when_minimized = false;
};

// Narrow per-instance test seam.  It substitutes only external Vulkan/cache
// effects; Present/Poll retain ownership, timeout, and recreation state.
struct VulkanPresentationTestHooks {
  std::function<VulkanGuestImagePreparation(const VulkanGuestImageRequest&)> prepare;
  std::function<void(VulkanGuestImagePreparation&)> discard;
  std::function<bool(VulkanGuestImagePreparation&)> complete;
  std::function<bool(VulkanGuestImagePreparation&)> mark_submitted;
  std::function<VkResult(std::uint64_t, std::uint32_t*)> acquire;
  std::function<VkResult()> submit;
  std::function<VkResult()> present;
  std::function<VkResult()> fence_status;
  std::function<bool(VkSwapchainKHR, VkSwapchainKHR&, std::vector<VkImage>&)> recreate;
};

[[nodiscard]] VulkanPresentationChoice ChooseVulkanPresentationChoice(
    const VkSurfaceCapabilitiesKHR& capabilities,
    const std::vector<VkSurfaceFormatKHR>& formats,
    const std::vector<VkPresentModeKHR>& modes, VkExtent2D requested) noexcept;
[[nodiscard]] VulkanPresentationSwapchainPlan BuildVulkanPresentationSwapchainPlan(
    const VkSurfaceCapabilitiesKHR& capabilities,
    const std::vector<VkSurfaceFormatKHR>& formats,
    const std::vector<VkPresentModeKHR>& modes, VkExtent2D requested) noexcept;

class VulkanPresentation final {
 public:
  [[nodiscard]] static std::unique_ptr<VulkanPresentation> Create(
      VulkanDeviceContext& context, VulkanGuestImageCache& image_cache,
      VulkanPresentationResult& result);
  [[nodiscard]] static std::unique_ptr<VulkanPresentation> CreateForTesting(
      VulkanPresentationTestHooks hooks, VulkanPresentationResult& result);
  ~VulkanPresentation();
  VulkanPresentation(const VulkanPresentation&) = delete;
  VulkanPresentation& operator=(const VulkanPresentation&) = delete;
  [[nodiscard]] VulkanPresentationResult Present(
      const GuestImageLayoutInput& input, std::uint64_t timeout_ns,
      std::optional<VulkanImageFormat> format_override = std::nullopt);
  [[nodiscard]] VulkanPresentationResult Poll();
  [[nodiscard]] VulkanPresentationResult RequestResize(VkExtent2D extent);
  [[nodiscard]] VkSurfaceFormatKHR surface_format() const noexcept;
  [[nodiscard]] VkPresentModeKHR present_mode() const noexcept;
 public:  // Internal state is defined in the implementation unit.
  struct Impl;
 private:
  explicit VulkanPresentation(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* VulkanPresentationStatusName(
    VulkanPresentationStatus status) noexcept;
}  // namespace kajps5::gpu::vulkan
