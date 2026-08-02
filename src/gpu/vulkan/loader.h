// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/presentation/window/vulkanWindow.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <optional>
#include <string>

#include "gpu/vulkan/vulkan_types.h"

namespace kajps5::gpu::vulkan {

// A move-only, per-context loader. It deliberately has no static Vulkan
// dispatch table, so a future host can own more than one independent context.
class VulkanLoader final {
 public:
  VulkanLoader() = default;
  ~VulkanLoader();

  VulkanLoader(const VulkanLoader&) = delete;
  VulkanLoader& operator=(const VulkanLoader&) = delete;
  VulkanLoader(VulkanLoader&& other) noexcept;
  VulkanLoader& operator=(VulkanLoader&& other) noexcept;

  // Opens the platform loader without requiring a Vulkan SDK/import library.
  // On failure, diagnostic contains the platform loader error when available.
  [[nodiscard]] static std::optional<VulkanLoader> Open(
      std::string& diagnostic);

  // Adopts an externally supplied resolver without taking a library-handle
  // ownership. This is the injected discovery seam used by focused tests and
  // can also support an embedding host's loader policy.
  [[nodiscard]] static VulkanLoader FromGetInstanceProcAddr(
      PFN_vkGetInstanceProcAddr get_instance_proc_addr) noexcept;

  [[nodiscard]] bool valid() const noexcept {
    return get_instance_proc_addr_ != nullptr;
  }
  [[nodiscard]] PFN_vkGetInstanceProcAddr get_instance_proc_addr() const
      noexcept {
    return get_instance_proc_addr_;
  }

 private:
  VulkanLoader(void* library_handle,
               PFN_vkGetInstanceProcAddr get_instance_proc_addr) noexcept;

  void Reset() noexcept;

  void* library_handle_ = nullptr;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr_ = nullptr;
};

}  // namespace kajps5::gpu::vulkan
