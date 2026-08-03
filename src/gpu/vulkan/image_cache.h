// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 image/cache model at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior reference: SharpEmu guest-image sizing and alias tests at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gpu/image_layout.h"
#include "gpu/resource_coherence.h"
#include "gpu/vulkan/device.h"

namespace kajps5::memory { class GuestMemory; }

namespace kajps5::gpu::vulkan {

enum class VulkanImageStorageClass : std::uint8_t { kR8, kR8G8, kR8G8B8A8, kR16, kR16G16, kR16G16B16A16, kR32, kR32G32, kR32G32B32A32, kBc1, kBc3, kBc4, kBc5, kBc6, kBc7 };
struct VulkanImageFormat { VkFormat format = VK_FORMAT_UNDEFINED; VulkanImageStorageClass storage_class{}; std::optional<VkFormat> sibling_format; };
[[nodiscard]] std::optional<VulkanImageFormat> MapGuestImageFormat(std::uint32_t format) noexcept;

enum class VulkanGuestImageStatus : std::uint8_t { kOk, kInvalidLayout, kUnsupportedFormat, kUnsupportedTopology, kGuestMemoryProtection, kGuestMemoryFault, kDeviceResourceFailure, kResourceLimit };
struct VulkanGuestImageDiagnostic { VulkanGuestImageStatus status = VulkanGuestImageStatus::kOk; std::string message; };

struct VulkanGuestImageRequest {
  GuestImageLayoutInput input;
  VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  bool writable = false;
  bool request_sibling_view = false;
};

// Vulkan handles are leases only. They neither own GuestMemory nor mark a GPU
// write until a future command-recording unit actually submits work.
struct VulkanGuestImagePreparation {
  VulkanGuestImageStatus status = VulkanGuestImageStatus::kInvalidLayout;
  GuestImageLayout layout{};
  VulkanImageFormat format{};
  GpuResourceId resource = 0;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkImageView sibling_view = VK_NULL_HANDLE;
  VkBuffer staging_buffer = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  void* staging_mapped = nullptr;
  VkDeviceSize staging_allocation_size = 0;
  bool staging_host_coherent = false;
  bool writable = false;
  std::vector<VkBufferImageCopy> copy_regions;
  std::vector<VulkanGuestImageDiagnostic> diagnostics;
  [[nodiscard]] explicit operator bool() const noexcept { return status == VulkanGuestImageStatus::kOk; }
};

class VulkanGuestImageCache final {
 public:
  VulkanGuestImageCache(VulkanDeviceContext& context, memory::GuestMemory& memory, GpuResourceCoherence& coherence) noexcept;
  ~VulkanGuestImageCache() = default;
  VulkanGuestImageCache(const VulkanGuestImageCache&) = delete;
  VulkanGuestImageCache& operator=(const VulkanGuestImageCache&) = delete;
  [[nodiscard]] VulkanGuestImagePreparation Prepare(const VulkanGuestImageRequest& request);
  void Discard(VulkanGuestImagePreparation& preparation) noexcept;
 private:
  VulkanDeviceContext& context_; memory::GuestMemory& memory_; GpuResourceCoherence& coherence_;
};
[[nodiscard]] const char* VulkanGuestImageStatusName(VulkanGuestImageStatus status) noexcept;
} // namespace kajps5::gpu::vulkan
