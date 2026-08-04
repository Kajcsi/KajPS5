// Copyright (C) 2026 KajPS5 contributors
// Narrow Win32 surface boundary adapted from KytyPS5 vulkanWindow at
// fb5ecec455cf6c67154134429485ffccbfc34203. SPDX-License-Identifier: GPL-2.0-only
#pragma once

#if defined(_WIN32)
#include "gpu/vulkan/device.h"

namespace kajps5::platform::windows {
class VulkanWindow final {
 public:
  [[nodiscard]] static std::unique_ptr<VulkanWindow> CreateHidden(
      std::uint32_t width = 640, std::uint32_t height = 480);
  [[nodiscard]] static std::unique_ptr<VulkanWindow> CreateVisible(
      std::uint32_t width = 640, std::uint32_t height = 480);
  ~VulkanWindow();
  VulkanWindow(const VulkanWindow&) = delete;
  VulkanWindow& operator=(const VulkanWindow&) = delete;
  [[nodiscard]] gpu::vulkan::VulkanSurfaceFactory surface_factory() const;
  void PumpMessages() noexcept;
  void Resize(std::uint32_t width, std::uint32_t height) noexcept;
  [[nodiscard]] VkExtent2D client_extent() const noexcept;
  [[nodiscard]] bool minimized() const noexcept;
  [[nodiscard]] bool closed() const noexcept;
 public:  // Implementation state is only defined by the Windows translation unit.
  struct Impl;
 private:
  explicit VulkanWindow(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};
}  // namespace kajps5::platform::windows
#endif
