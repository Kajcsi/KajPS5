// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/cache/bufferCache.* at
// fb5ecec455cf6c67154134429485ffccbfc34203. Behavior reference: SharpEmu guest
// write tracking at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gpu/resource_coherence.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"
#include "gpu/vulkan/device.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::gpu::vulkan {

// This is deliberately a guest/coherence cache, not a second guest address
// space or a second write observer. Vulkan ownership remains with GpuRuntime.
enum class VulkanGuestBufferStatus {
  kOk,
  kUnsupportedTopology,
  kInvalidSpecialization,
  kInvalidDescriptor,
  kRangeOverflow,
  kZeroFootprint,
  kGuestMemoryFault,
  kGuestMemoryProtection,
  kDeviceResourceFailure,
  kResourceLimit,
};

struct VulkanGuestBufferDiagnostic {
  VulkanGuestBufferStatus status = VulkanGuestBufferStatus::kOk;
  std::string message;
};

struct VulkanGuestBufferView {
  GpuResourceId resource = 0;
  std::uint64_t guest_address = 0;
  std::uint64_t size = 0;
  std::uint32_t descriptor_index = 0;
  std::uint32_t packed_offset_dword = 0;
  VkDeviceSize descriptor_offset = 0;
  VkDeviceSize descriptor_range = 0;
  // Byte position in the shared mapped backing where this view's guest base
  // resides. This is descriptor_offset + packed_offset_dword * 4.
  VkDeviceSize data_offset = 0;
  bool shader_reads = false;
  bool shader_writes = false;
  bool gpu_dirty = false;
  std::vector<std::byte> uploaded_bytes;
};

struct VulkanGuestBufferPreparation {
  VulkanGuestBufferStatus status = VulkanGuestBufferStatus::kOk;
  // Exact bytes passed to vkCmdPushConstants. User-data dwords occupy the
  // leading slots selected by BindingLayout::user_data_registers; buffer
  // offsets are packed four six-bit dword values per following word.
  std::vector<std::uint32_t> shader_data_dwords;
  std::uint64_t descriptor_binding_base = 0;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void *mapped = nullptr;
  VkDeviceSize allocation_size = 0;
  VkDeviceSize logical_size = 0;
  bool host_coherent = false;
  bool backing_reusable = false;
  std::vector<VulkanGuestBufferView> views;
  std::vector<VulkanGuestBufferDiagnostic> diagnostics;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == VulkanGuestBufferStatus::kOk;
  }
};

// Validates and uploads the exact guest ranges described by an immutable
// CompileResult. The resulting vectors are intentionally backend-neutral: the
// executor may only bind them after this returns success. This makes every
// rejection below happen before Vulkan side effects.
class VulkanGuestBufferCache final {
public:
  VulkanGuestBufferCache(VulkanDeviceContext &context,
                         memory::GuestMemory &memory,
                         GpuResourceCoherence &coherence) noexcept;
  // Test-only pre-Vulkan validator construction. It cannot prepare a legal
  // lease because no device is available.
  VulkanGuestBufferCache(memory::GuestMemory &memory,
                         GpuResourceCoherence &coherence) noexcept;
  ~VulkanGuestBufferCache();

  VulkanGuestBufferCache(const VulkanGuestBufferCache &) = delete;
  VulkanGuestBufferCache &operator=(const VulkanGuestBufferCache &) = delete;

  [[nodiscard]] VulkanGuestBufferPreparation
  Prepare(const shader::recompiler::CompileResult &compile);
  // Releases coherence registrations for a preparation that never transferred
  // into a submitted Vulkan lease. Safe to call after a failed preparation.
  void Discard(VulkanGuestBufferPreparation &preparation) noexcept;
  [[nodiscard]] bool
  MarkSubmitted(VulkanGuestBufferPreparation &preparation) noexcept;
  // Runs once after the associated fence signals. A false return leaves every
  // GPU-dirty record pending; callers must retain the lease for diagnostics.
  [[nodiscard]] bool
  Complete(VulkanGuestBufferPreparation &preparation) noexcept;
  [[nodiscard]] std::size_t lost_dirty_resource_count() const noexcept {
    return lost_dirty_count_;
  }
  [[nodiscard]] std::size_t idle_backing_count() const noexcept;

private:
  VulkanDeviceContext *context_ = nullptr;
  memory::GuestMemory &memory_;
  GpuResourceCoherence &coherence_;
  struct IdleBacking {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceSize allocation_size = 0;
    VkDeviceSize logical_size = 0;
    bool host_coherent = false;
  };
  static constexpr std::size_t kMaximumIdleBackings = 8;
  std::array<std::optional<IdleBacking>, kMaximumIdleBackings> idle_backings_{};
  static constexpr std::size_t kMaximumLostDirtyResources = 8 * 32;
  std::array<GpuResourceId, kMaximumLostDirtyResources> lost_dirty_resources_{};
  std::size_t lost_dirty_count_ = 0;
};

[[nodiscard]] const char *
VulkanGuestBufferStatusName(VulkanGuestBufferStatus status) noexcept;

} // namespace kajps5::gpu::vulkan
