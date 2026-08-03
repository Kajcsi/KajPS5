// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 descriptor preparation model.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "gpu/vulkan/buffer_cache.h"
#include "gpu/vulkan/image_cache.h"

namespace kajps5::gpu::vulkan {

enum class VulkanGraphicsBindingStatus : std::uint8_t {
  kOk,
  kInvalidSpecialization,
  kUnsupportedTopology,
  kMalformedPreparation,
  kDeviceResourceLimit,
  kAliasConflict,
  kAllocationFailure,
};

struct VulkanGraphicsBindingDiagnostic {
  VulkanGraphicsBindingStatus status = VulkanGraphicsBindingStatus::kOk;
  std::string message;
};

// Non-owning input. The caller retains every preparation until the executor
// consumes the completed plan; the plan copies all command-recording data.
struct VulkanGraphicsStageBindingInput {
  const shader::recompiler::CompileResult* compile = nullptr;
  const VulkanGuestBufferPreparation* buffers = nullptr;
  const VulkanGuestImageSetPreparation* images = nullptr;
};

struct VulkanGraphicsDescriptorBindingPlan {
  VkDescriptorSetLayoutBinding layout{};
  // Exactly one of these vectors is populated for a binding. Their contents
  // are owned by the plan, rather than pointing at caller preparations.
  std::vector<VkDescriptorBufferInfo> buffer_infos;
  std::vector<VkDescriptorImageInfo> image_infos;
};

struct VulkanGraphicsDescriptorSetPlan {
  std::uint32_t set = 0;
  std::vector<VulkanGraphicsDescriptorBindingPlan> bindings;
};

struct VulkanGraphicsPushConstantPlan {
  VkPushConstantRange range{};
  std::vector<std::uint32_t> data_dwords;
};

struct VulkanGraphicsImageUploadPlan {
  std::uint32_t stage_set = 0;
  std::uint32_t preparation_index = 0;
  GpuResourceId resource = 0;
  std::uint64_t guest_address = 0;
  std::uint64_t guest_size = 0;
  VkImage image = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkPipelineStageFlags shader_stages = 0;
  VkAccessFlags shader_access = 0;
};

// A fully materialized, Vulkan-call-free description for the graphics
// executor. set_layouts always contains set 0; it additionally contains set 1
// when the pixel shader uses descriptors, preserving Vulkan set numbering.
struct VulkanGraphicsBindingPlan {
  std::vector<VulkanGraphicsDescriptorSetPlan> set_layouts;
  std::vector<VkDescriptorPoolSize> pool_sizes;
  std::vector<VulkanGraphicsPushConstantPlan> push_constants;
  std::vector<VulkanGraphicsImageUploadPlan> image_uploads;
  std::vector<VulkanGraphicsBindingDiagnostic> diagnostics;

  [[nodiscard]] explicit operator bool() const noexcept {
    return diagnostics.empty();
  }
};

// allocation_record_limit is a bounded test seam. Production callers use the
// default and receive the same fail-closed bad_alloc handling.
[[nodiscard]] VulkanGraphicsBindingStatus BuildVulkanGraphicsBindingPlan(
    const VulkanDeviceCandidate& properties,
    const VulkanGraphicsStageBindingInput& vertex,
    const VulkanGraphicsStageBindingInput& pixel,
    VulkanGraphicsBindingPlan& output,
    std::size_t allocation_record_limit = std::numeric_limits<std::size_t>::max()) noexcept;

[[nodiscard]] const char* VulkanGraphicsBindingStatusName(
    VulkanGraphicsBindingStatus status) noexcept;

} // namespace kajps5::gpu::vulkan
