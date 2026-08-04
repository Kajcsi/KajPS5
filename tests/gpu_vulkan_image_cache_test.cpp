// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/format.h"
#include "gpu/image_layout.h"
#include "gpu/runtime.h"
#include "gpu/tile_layout.h"
#include "gpu/vulkan/buffer_cache.h"
#include "gpu/vulkan/image_cache.h"
#include "gpu/vulkan/loader.h"

namespace {
namespace P = kajps5::gpu::Prospero;
namespace vk = kajps5::gpu::vulkan;
namespace ir = kajps5::gpu::shader::recompiler::IR;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "vulkan_image_cache_test: " << message << '\n';
    std::exit(1);
  }
}

template <typename T> T Handle(std::uintptr_t value) {
  return reinterpret_cast<T>(value);
}

enum class FailPoint { kNone, kCreateImage, kAllocateImage, kBindImage,
                       kCreateFirstView, kCreateSecondView, kCreateBuffer,
                       kAllocateStaging, kBindStaging, kMap, kCreateSampler,
                       kCreateAuxiliaryView };

struct FakeOperation {
  std::string_view kind;
  std::uintptr_t handle = 0;
};

// Copy only scalar values. Vulkan owns every pointed-to create-info payload
// only for the duration of the call, so retaining the original structs would
// leave this fake with dangling pNext pointers.
struct FakeImageCreateInfo {
  VkImageCreateFlags flags = 0;
  VkImageType image_type = VK_IMAGE_TYPE_MAX_ENUM;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent3D extent{};
  std::uint32_t mip_levels = 0;
  std::uint32_t array_layers = 0;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
  VkImageTiling tiling = VK_IMAGE_TILING_MAX_ENUM;
  VkImageUsageFlags usage = 0;
  VkSharingMode sharing_mode = VK_SHARING_MODE_MAX_ENUM;
  VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct FakeImageViewCreateInfo {
  VkImageCreateFlags flags = 0;
  VkImage image = VK_NULL_HANDLE;
  VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkComponentMapping components{};
  VkImageAspectFlags aspect_mask = 0;
  std::uint32_t base_mip_level = 0;
  std::uint32_t level_count = 0;
  std::uint32_t base_array_layer = 0;
  std::uint32_t layer_count = 0;
  VkImageView created_view = VK_NULL_HANDLE;
};

struct FakeBarrier {
  VkPipelineStageFlags src_stage = 0;
  VkPipelineStageFlags dst_stage = 0;
  std::vector<VkImageMemoryBarrier> images;
  std::vector<VkBufferMemoryBarrier> buffers;
};

enum class FakeCommandOperation {
  kImageBarrier,
  kBufferToImageCopy,
  kImageToBufferCopy,
  kBufferBarrier,
};

struct FakeState {
  FailPoint fail = FailPoint::kNone;
  bool coherent = false;
  bool commands_available = true;
  bool invalidate_available = true;
  std::uint32_t max_per_stage_descriptor_storage_buffers = 16;
  std::uint32_t max_descriptor_set_storage_buffers = 16;
  std::uint32_t max_per_stage_descriptor_samplers = 16;
  std::uint32_t max_descriptor_set_samplers = 16;
  std::uint32_t max_per_stage_descriptor_sampled_images = 16;
  std::uint32_t max_descriptor_set_sampled_images = 16;
  std::uint32_t max_per_stage_descriptor_storage_images = 16;
  std::uint32_t max_descriptor_set_storage_images = 16;
  std::uint32_t max_per_stage_resources = 64;
  std::uintptr_t next_handle = 0x1000;
  std::uint32_t image_allocations = 0;
  std::uint32_t buffer_binds = 0;
  std::uint32_t views = 0;
  std::uint32_t flushes = 0;
  std::uint32_t invalidates = 0;
  VkImageType image_type = VK_IMAGE_TYPE_MAX_ENUM;
  VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
  VkFormat image_format = VK_FORMAT_UNDEFINED;
  VkImageCreateFlags image_flags = 0;
  std::uint32_t image_layers = 0;
  VkImageUsageFlags image_usage = 0;
  VkBufferUsageFlags buffer_usage = 0;
  VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  std::vector<std::byte> mapped_bytes;
  std::vector<FakeBarrier> barriers;
  std::vector<VkBufferImageCopy> upload_copies;
  std::vector<VkBufferImageCopy> readback_copies;
  std::vector<FakeCommandOperation> command_operations;
  std::vector<FakeImageCreateInfo> image_create_infos;
  std::vector<FakeImageViewCreateInfo> image_view_create_infos;
  std::vector<FakeOperation> issued;
  std::vector<FakeOperation> poisoned;
  std::vector<FakeOperation> teardown;
  VkFormatFeatureFlags format_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  VkResult graphics_wait_result = VK_SUCCESS;
  VkResult graphics_queue_submit_result = VK_SUCCESS;
  std::uint32_t graphics_shader_modules = 0;
  std::uint32_t graphics_pipeline_layouts = 0;
  std::uint32_t graphics_pipelines = 0;
  std::uint32_t graphics_descriptor_layouts = 0;
  std::uint32_t graphics_descriptor_pools = 0;
  std::uint32_t graphics_descriptor_binds = 0;
  std::uint32_t graphics_pushes = 0;
  std::vector<VkDescriptorSetLayoutBinding> graphics_descriptor_bindings;
  std::vector<VkWriteDescriptorSet> graphics_descriptor_writes;
  std::vector<std::uint32_t> graphics_push_words;
  std::uint32_t graphics_command_pools = 0;
  std::uint32_t graphics_fences = 0;
  VkGraphicsPipelineCreateInfo graphics_pipeline_info{};
  VkPrimitiveTopology graphics_topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
  VkCullModeFlags graphics_cull_mode = 0;
  VkFrontFace graphics_front_face = VK_FRONT_FACE_MAX_ENUM;
  VkPipelineRenderingCreateInfo graphics_rendering_info{};
  VkFormat graphics_color_format = VK_FORMAT_UNDEFINED;
  VkPipelineDepthStencilStateCreateInfo graphics_depth_stencil{};
  VkPipelineShaderStageCreateInfo graphics_stages[2]{};
  VkPipelineColorBlendAttachmentState graphics_blend{};
  std::vector<std::string_view> graphics_commands;
  VkViewport graphics_viewport{};
  VkRect2D graphics_scissor{};
  VkRenderingAttachmentInfo graphics_attachment{};
  VkRenderingAttachmentInfo graphics_depth_attachment{};
  std::array<std::uint32_t, 4> graphics_draw{};
} g;

template <typename T> std::uintptr_t HandleId(T handle) {
  return reinterpret_cast<std::uintptr_t>(handle);
}

void Issue(std::string_view kind, std::uintptr_t handle) {
  Check(handle != 0, "fake issued a null resource handle");
  g.issued.push_back({kind, handle});
}

void Poison(std::string_view kind, std::uintptr_t handle) {
  Check(handle != 0, "fake poison value is null");
  g.poisoned.push_back({kind, handle});
}

void TearDown(std::string_view kind, std::uintptr_t handle) {
  Check(handle != 0, "implementation attempted to destroy a null fake handle");
  g.teardown.push_back({kind, handle});
}

void Reset(FailPoint fail = FailPoint::kNone, bool coherent = false) {
  g = {};
  g.fail = fail;
  g.coherent = coherent;
  g.next_handle = 0x1000;
}

VkInstance Instance() { return Handle<VkInstance>(0x11); }
VkPhysicalDevice PhysicalDevice() { return Handle<VkPhysicalDevice>(0x22); }
VkDevice Device() { return Handle<VkDevice>(0x33); }
VkQueue Queue() { return Handle<VkQueue>(0x44); }

VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerateInstanceVersion(std::uint32_t* version) {
  *version = VK_API_VERSION_1_3;
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateInstance(const VkInstanceCreateInfo*,
    const VkAllocationCallbacks*, VkInstance* instance) { *instance = Instance(); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL FakeDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeEnumeratePhysicalDevices(VkInstance, std::uint32_t* count,
    VkPhysicalDevice* devices) {
  if (devices == nullptr) { *count = 1; return VK_SUCCESS; }
  devices[0] = PhysicalDevice(); *count = 1; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceProperties(VkPhysicalDevice,
    VkPhysicalDeviceProperties* properties) {
  *properties = {};
  properties->apiVersion = VK_API_VERSION_1_3;
  properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  properties->limits.maxImageDimension1D = 4096;
  properties->limits.maxImageDimension2D = 4096;
  properties->limits.maxImageDimension3D = 256;
  properties->limits.maxImageArrayLayers = 256;
  properties->limits.minStorageBufferOffsetAlignment = 1;
  properties->limits.maxStorageBufferRange = 4096;
  properties->limits.maxPerStageDescriptorStorageBuffers = g.max_per_stage_descriptor_storage_buffers;
  properties->limits.maxDescriptorSetStorageBuffers = g.max_descriptor_set_storage_buffers;
  properties->limits.maxPerStageDescriptorSamplers = g.max_per_stage_descriptor_samplers;
  properties->limits.maxDescriptorSetSamplers = g.max_descriptor_set_samplers;
  properties->limits.maxPerStageDescriptorSampledImages = g.max_per_stage_descriptor_sampled_images;
  properties->limits.maxDescriptorSetSampledImages = g.max_descriptor_set_sampled_images;
  properties->limits.maxPerStageDescriptorStorageImages = g.max_per_stage_descriptor_storage_images;
  properties->limits.maxDescriptorSetStorageImages = g.max_descriptor_set_storage_images;
  properties->limits.maxPerStageResources = g.max_per_stage_resources;
  properties->limits.maxPushConstantsSize = 128;
  properties->limits.maxBoundDescriptorSets = 4;
  properties->limits.nonCoherentAtomSize = 64;
  properties->limits.maxComputeWorkGroupCount[0] = 16;
  properties->limits.maxComputeWorkGroupCount[1] = 16;
  properties->limits.maxComputeWorkGroupCount[2] = 16;
  std::strncpy(properties->deviceName, "injected image cache", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceProperties2(VkPhysicalDevice,
    VkPhysicalDeviceProperties2* properties) { properties->properties = {}; }
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
  *properties = {};
  properties->optimalTilingFeatures = g.format_features;
}
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceMemoryProperties(VkPhysicalDevice,
    VkPhysicalDeviceMemoryProperties* properties) {
  *properties = {};
  properties->memoryTypeCount = 1;
  properties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      (g.coherent ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0);
  properties->memoryHeaps[0].size = 1024 * 1024;
}
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceFeatures2(VkPhysicalDevice,
    VkPhysicalDeviceFeatures2* features) {
  features->features.sampleRateShading = VK_TRUE;
  features->features.fragmentStoresAndAtomics = VK_TRUE;
  features->features.samplerAnisotropy = VK_TRUE;
  features->features.robustBufferAccess = VK_TRUE;
  features->features.depthBounds = VK_TRUE;
  features->features.shaderImageGatherExtended = VK_TRUE;
  features->features.shaderStorageImageReadWithoutFormat = VK_TRUE;
  features->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
  features->features.independentBlend = VK_TRUE;
  features->features.tessellationShader = VK_TRUE;
  features->features.textureCompressionBC = VK_TRUE;
  for (auto* next = static_cast<VkBaseOutStructure*>(features->pNext); next != nullptr; next = next->pNext) {
    if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES) {
      auto* f = reinterpret_cast<VkPhysicalDeviceVulkan13Features*>(next);
      f->dynamicRendering = VK_TRUE; f->synchronization2 = VK_TRUE; f->robustImageAccess = VK_TRUE;
    } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
      auto* f = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(next);
      f->timelineSemaphore = VK_TRUE; f->samplerMirrorClampToEdge = VK_TRUE;
    }
  }
}
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice,
    std::uint32_t* count, VkQueueFamilyProperties* properties) {
  if (properties == nullptr) { *count = 1; return; }
  properties[0] = {}; properties[0].queueCount = 1;
  properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; *count = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerateDeviceExtensionProperties(VkPhysicalDevice,
    const char*, std::uint32_t* count, VkExtensionProperties*) { *count = 0; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*,
    const VkAllocationCallbacks*, VkDevice* device) { *device = Device(); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL FakeDestroyDevice(VkDevice, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL FakeGetDeviceQueue(VkDevice, std::uint32_t, std::uint32_t, VkQueue* queue) { *queue = Queue(); }
VKAPI_ATTR VkResult VKAPI_CALL FakeDeviceWaitIdle(VkDevice) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateImage(VkDevice, const VkImageCreateInfo* info,
    const VkAllocationCallbacks*, VkImage* image) {
  g.image_create_infos.push_back({info->flags, info->imageType, info->format, info->extent,
      info->mipLevels, info->arrayLayers, info->samples, info->tiling, info->usage,
      info->sharingMode, info->initialLayout});
  if (g.fail == FailPoint::kCreateImage) { *image = Handle<VkImage>(0xdead); Poison("image", HandleId(*image)); return VK_ERROR_OUT_OF_HOST_MEMORY; }
  g.image_type = info->imageType; g.image_format = info->format; g.image_flags = info->flags;
  g.image_usage = info->usage;
  g.image_layers = info->arrayLayers; *image = Handle<VkImage>(g.next_handle++); Issue("image", HandleId(*image)); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyImage(VkDevice, VkImage image, const VkAllocationCallbacks*) { TearDown("image", HandleId(image)); }
VKAPI_ATTR void VKAPI_CALL FakeGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* requirements) {
  *requirements = {}; requirements->size = 256; requirements->alignment = 64; requirements->memoryTypeBits = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) {
  return g.fail == FailPoint::kBindImage ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateImageView(VkDevice, const VkImageViewCreateInfo* info,
    const VkAllocationCallbacks*, VkImageView* view) {
  g.image_view_create_infos.push_back({info->flags, info->image, info->viewType, info->format,
      info->components, info->subresourceRange.aspectMask,
      info->subresourceRange.baseMipLevel, info->subresourceRange.levelCount,
      info->subresourceRange.baseArrayLayer, info->subresourceRange.layerCount});
  ++g.views; g.view_type = info->viewType;
  if ((g.fail == FailPoint::kCreateFirstView && g.views == 1) ||
      (g.fail == FailPoint::kCreateSecondView && g.views == 2) ||
      (g.fail == FailPoint::kCreateAuxiliaryView && g.views == 3)) { *view = Handle<VkImageView>(0xdead); Poison("view", HandleId(*view)); return VK_ERROR_OUT_OF_HOST_MEMORY; }
  *view = Handle<VkImageView>(g.next_handle++);
  g.image_view_create_infos.back().created_view = *view;
  Issue("view", HandleId(*view)); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks*) { TearDown("view", HandleId(view)); }
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateBuffer(VkDevice, const VkBufferCreateInfo* info,
    const VkAllocationCallbacks*, VkBuffer* buffer) {
  g.buffer_usage = info->usage;
  if (g.fail == FailPoint::kCreateBuffer) { *buffer = Handle<VkBuffer>(0xdead); Poison("buffer", HandleId(*buffer)); return VK_ERROR_OUT_OF_HOST_MEMORY; }
  *buffer = Handle<VkBuffer>(g.next_handle++); Issue("buffer", HandleId(*buffer)); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) { TearDown("buffer", HandleId(buffer)); }
VKAPI_ATTR void VKAPI_CALL FakeGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements* requirements) {
  *requirements = {}; requirements->size = 256; requirements->alignment = 64; requirements->memoryTypeBits = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeAllocateMemory(VkDevice, const VkMemoryAllocateInfo*,
    const VkAllocationCallbacks*, VkDeviceMemory* memory) {
  ++g.image_allocations;
  if ((g.fail == FailPoint::kAllocateImage && g.image_allocations == 1) ||
      (g.fail == FailPoint::kAllocateStaging && g.image_allocations == 2)) { *memory = Handle<VkDeviceMemory>(0xdead); Poison("memory", HandleId(*memory)); return VK_ERROR_OUT_OF_HOST_MEMORY; }
  *memory = Handle<VkDeviceMemory>(g.next_handle++); Issue("memory", HandleId(*memory)); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) { TearDown("memory", HandleId(memory)); }
VKAPI_ATTR VkResult VKAPI_CALL FakeBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) {
  ++g.buffer_binds; return g.fail == FailPoint::kBindStaging ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize size,
    VkMemoryMapFlags, void** mapped) {
  if (g.fail == FailPoint::kMap) { *mapped = reinterpret_cast<void*>(0xdead); return VK_ERROR_MEMORY_MAP_FAILED; }
  g.mapped_bytes.resize(static_cast<std::size_t>(size)); *mapped = g.mapped_bytes.data(); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeUnmapMemory(VkDevice, VkDeviceMemory memory) { TearDown("unmap", HandleId(memory)); }
VKAPI_ATTR VkResult VKAPI_CALL FakeFlushMappedMemoryRanges(VkDevice, std::uint32_t, const VkMappedMemoryRange*) { ++g.flushes; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL FakeInvalidateMappedMemoryRanges(VkDevice, std::uint32_t, const VkMappedMemoryRange*) { ++g.invalidates; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateSampler(VkDevice, const VkSamplerCreateInfo* info,
    const VkAllocationCallbacks*, VkSampler* sampler) {
  if (g.fail == FailPoint::kCreateSampler) {
    *sampler = Handle<VkSampler>(0xdead); Poison("sampler", HandleId(*sampler));
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  g.sampler_info = *info;
  *sampler = Handle<VkSampler>(g.next_handle++); Issue("sampler", HandleId(*sampler));
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroySampler(VkDevice, VkSampler sampler,
    const VkAllocationCallbacks*) { TearDown("sampler", HandleId(sampler)); }
VKAPI_ATTR void VKAPI_CALL FakeCmdPipelineBarrier(VkCommandBuffer,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    VkDependencyFlags, std::uint32_t, const VkMemoryBarrier*,
    std::uint32_t buffer_count, const VkBufferMemoryBarrier* buffers,
    std::uint32_t image_count, const VkImageMemoryBarrier* images) {
  FakeBarrier barrier{src_stage, dst_stage};
  if (image_count != 0) barrier.images.assign(images, images + image_count);
  if (buffer_count != 0) barrier.buffers.assign(buffers, buffers + buffer_count);
  if (image_count != 0) {
    g.command_operations.push_back(FakeCommandOperation::kImageBarrier);
  } else if (buffer_count != 0) {
    g.command_operations.push_back(FakeCommandOperation::kBufferBarrier);
  }
  g.barriers.push_back(std::move(barrier));
}
VKAPI_ATTR void VKAPI_CALL FakeCmdCopyBufferToImage(VkCommandBuffer, VkBuffer,
    VkImage, VkImageLayout, std::uint32_t count, const VkBufferImageCopy* regions) {
  g.upload_copies.assign(regions, regions + count);
  g.command_operations.push_back(FakeCommandOperation::kBufferToImageCopy);
}
VKAPI_ATTR void VKAPI_CALL FakeCmdCopyImageToBuffer(VkCommandBuffer, VkImage,
    VkImageLayout, VkBuffer, std::uint32_t count, const VkBufferImageCopy* regions) {
  g.readback_copies.assign(regions, regions + count);
  g.command_operations.push_back(FakeCommandOperation::kImageToBufferCopy);
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateCommandPool(
    VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*,
    VkCommandPool* pool) {
  *pool = Handle<VkCommandPool>(g.next_handle++);
  ++g.graphics_command_pools;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyCommandPool(
    VkDevice, VkCommandPool, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeAllocateCommandBuffers(
    VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* command) {
  *command = Handle<VkCommandBuffer>(g.next_handle++);
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeBeginCommandBuffer(
    VkCommandBuffer, const VkCommandBufferBeginInfo*) {
  g.graphics_commands.push_back("begin-command");
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeEndCommandBuffer(VkCommandBuffer) {
  g.graphics_commands.push_back("end-command");
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateFence(
    VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* fence) {
  *fence = Handle<VkFence>(g.next_handle++);
  ++g.graphics_fences;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyFence(VkDevice, VkFence,
                                             const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeWaitForFences(VkDevice, std::uint32_t,
    const VkFence*, VkBool32, std::uint64_t) { return g.graphics_wait_result; }
VKAPI_ATTR VkResult VKAPI_CALL FakeGetFenceStatus(VkDevice, VkFence) {
  return g.graphics_wait_result == VK_TIMEOUT ? VK_SUCCESS : g.graphics_wait_result;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeQueueSubmit(VkQueue, std::uint32_t,
    const VkSubmitInfo*, VkFence) {
  g.graphics_commands.push_back("submit");
  return g.graphics_queue_submit_result;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateShaderModule(VkDevice,
    const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* module) {
  *module = Handle<VkShaderModule>(g.next_handle++);
  ++g.graphics_shader_modules;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyShaderModule(
    VkDevice, VkShaderModule, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDescriptorSetLayout(
    VkDevice, const VkDescriptorSetLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkDescriptorSetLayout* layout) {
  g.graphics_descriptor_bindings.clear();
  if (info->bindingCount != 0) g.graphics_descriptor_bindings.assign(
      info->pBindings, info->pBindings + info->bindingCount);
  *layout = Handle<VkDescriptorSetLayout>(g.next_handle++);
  ++g.graphics_descriptor_layouts;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyDescriptorSetLayout(
    VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDescriptorPool(
    VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*,
    VkDescriptorPool* pool) {
  *pool = Handle<VkDescriptorPool>(g.next_handle++);
  ++g.graphics_descriptor_pools;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyDescriptorPool(
    VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeAllocateDescriptorSets(
    VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
  for (std::uint32_t index = 0; index < info->descriptorSetCount; ++index)
    sets[index] = Handle<VkDescriptorSet>(g.next_handle++);
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeUpdateDescriptorSets(
    VkDevice, std::uint32_t count, const VkWriteDescriptorSet* writes,
    std::uint32_t, const VkCopyDescriptorSet*) {
  g.graphics_descriptor_writes.assign(writes, writes + count);
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreatePipelineLayout(VkDevice,
    const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* layout) {
  *layout = Handle<VkPipelineLayout>(g.next_handle++);
  ++g.graphics_pipeline_layouts;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyPipelineLayout(
    VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateGraphicsPipelines(VkDevice, VkPipelineCache,
    std::uint32_t, const VkGraphicsPipelineCreateInfo* info,
    const VkAllocationCallbacks*, VkPipeline* pipeline) {
  g.graphics_pipeline_info = *info;
  g.graphics_topology = info->pInputAssemblyState->topology;
  g.graphics_cull_mode = info->pRasterizationState->cullMode;
  g.graphics_front_face = info->pRasterizationState->frontFace;
  g.graphics_rendering_info = *static_cast<const VkPipelineRenderingCreateInfo*>(info->pNext);
  g.graphics_color_format = g.graphics_rendering_info.pColorAttachmentFormats[0];
  if (info->pDepthStencilState != nullptr) {
    g.graphics_depth_stencil = *info->pDepthStencilState;
  }
  g.graphics_stages[0] = info->pStages[0];
  g.graphics_stages[1] = info->pStages[1];
  g.graphics_blend = info->pColorBlendState->pAttachments[0];
  *pipeline = Handle<VkPipeline>(g.next_handle++);
  ++g.graphics_pipelines;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyPipeline(
    VkDevice, VkPipeline, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL FakeCmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint,
                                                 VkPipeline) { g.graphics_commands.push_back("bind"); }
VKAPI_ATTR void VKAPI_CALL FakeCmdBindDescriptorSets(
    VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, std::uint32_t,
    std::uint32_t, const VkDescriptorSet*, std::uint32_t, const std::uint32_t*) {
  ++g.graphics_descriptor_binds;
  g.graphics_commands.push_back("bind-descriptors");
}
VKAPI_ATTR void VKAPI_CALL FakeCmdPushConstants(
    VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, std::uint32_t,
    std::uint32_t size, const void* values) {
  g.graphics_push_words.assign(static_cast<const std::uint32_t*>(values),
      static_cast<const std::uint32_t*>(values) + size / sizeof(std::uint32_t));
  ++g.graphics_pushes;
  g.graphics_commands.push_back("push");
}
VKAPI_ATTR void VKAPI_CALL FakeCmdSetViewport(VkCommandBuffer, std::uint32_t,
    std::uint32_t, const VkViewport* viewport) { g.graphics_viewport = *viewport; g.graphics_commands.push_back("viewport"); }
VKAPI_ATTR void VKAPI_CALL FakeCmdSetScissor(VkCommandBuffer, std::uint32_t,
    std::uint32_t, const VkRect2D* scissor) { g.graphics_scissor = *scissor; g.graphics_commands.push_back("scissor"); }
VKAPI_ATTR void VKAPI_CALL FakeCmdBeginRendering(
    VkCommandBuffer, const VkRenderingInfo* info) {
  g.graphics_attachment = info->pColorAttachments[0];
  if (info->pDepthAttachment != nullptr) {
    g.graphics_depth_attachment = *info->pDepthAttachment;
  }
  g.graphics_commands.push_back("begin-rendering");
}
VKAPI_ATTR void VKAPI_CALL FakeCmdEndRendering(VkCommandBuffer) { g.graphics_commands.push_back("end-rendering"); }
VKAPI_ATTR void VKAPI_CALL FakeCmdDraw(VkCommandBuffer, std::uint32_t vertices,
    std::uint32_t instances, std::uint32_t first_vertex, std::uint32_t first_instance) {
  g.graphics_draw = {vertices, instances, first_vertex, first_instance};
  g.graphics_commands.push_back("draw");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetDeviceProcAddr(VkDevice, const char* name) {
  if (std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDevice);
  if (std::strcmp(name, "vkGetDeviceQueue") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetDeviceQueue);
  if (std::strcmp(name, "vkDeviceWaitIdle") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDeviceWaitIdle);
  if (std::strcmp(name, "vkCreateImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateImage);
  if (std::strcmp(name, "vkDestroyImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyImage);
  if (std::strcmp(name, "vkGetImageMemoryRequirements") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetImageMemoryRequirements);
  if (std::strcmp(name, "vkBindImageMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeBindImageMemory);
  if (std::strcmp(name, "vkCreateImageView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateImageView);
  if (std::strcmp(name, "vkDestroyImageView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyImageView);
  if (std::strcmp(name, "vkCreateBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateBuffer);
  if (std::strcmp(name, "vkDestroyBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyBuffer);
  if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetBufferMemoryRequirements);
  if (std::strcmp(name, "vkAllocateMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeAllocateMemory);
  if (std::strcmp(name, "vkFreeMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeFreeMemory);
  if (std::strcmp(name, "vkBindBufferMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeBindBufferMemory);
  if (std::strcmp(name, "vkMapMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeMapMemory);
  if (std::strcmp(name, "vkUnmapMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeUnmapMemory);
  if (std::strcmp(name, "vkFlushMappedMemoryRanges") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeFlushMappedMemoryRanges);
  if (g.invalidate_available && std::strcmp(name, "vkInvalidateMappedMemoryRanges") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeInvalidateMappedMemoryRanges);
  if (std::strcmp(name, "vkCreateSampler") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateSampler);
  if (std::strcmp(name, "vkDestroySampler") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroySampler);
  if (g.commands_available && std::strcmp(name, "vkCmdPipelineBarrier") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdPipelineBarrier);
  if (g.commands_available && std::strcmp(name, "vkCmdCopyBufferToImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdCopyBufferToImage);
  if (g.commands_available && std::strcmp(name, "vkCmdCopyImageToBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdCopyImageToBuffer);
  if (std::strcmp(name, "vkCreateCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateCommandPool);
  if (std::strcmp(name, "vkDestroyCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyCommandPool);
  if (std::strcmp(name, "vkAllocateCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeAllocateCommandBuffers);
  if (std::strcmp(name, "vkBeginCommandBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeBeginCommandBuffer);
  if (std::strcmp(name, "vkEndCommandBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeEndCommandBuffer);
  if (std::strcmp(name, "vkCreateFence") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateFence);
  if (std::strcmp(name, "vkDestroyFence") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyFence);
  if (std::strcmp(name, "vkWaitForFences") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeWaitForFences);
  if (std::strcmp(name, "vkGetFenceStatus") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetFenceStatus);
  if (std::strcmp(name, "vkQueueSubmit") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeQueueSubmit);
  if (std::strcmp(name, "vkCreateShaderModule") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateShaderModule);
  if (std::strcmp(name, "vkDestroyShaderModule") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyShaderModule);
  if (std::strcmp(name, "vkCreateDescriptorSetLayout") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateDescriptorSetLayout);
  if (std::strcmp(name, "vkDestroyDescriptorSetLayout") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDescriptorSetLayout);
  if (std::strcmp(name, "vkCreateDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateDescriptorPool);
  if (std::strcmp(name, "vkDestroyDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDescriptorPool);
  if (std::strcmp(name, "vkAllocateDescriptorSets") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeAllocateDescriptorSets);
  if (std::strcmp(name, "vkUpdateDescriptorSets") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeUpdateDescriptorSets);
  if (std::strcmp(name, "vkCreatePipelineLayout") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreatePipelineLayout);
  if (std::strcmp(name, "vkDestroyPipelineLayout") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyPipelineLayout);
  if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateGraphicsPipelines);
  if (std::strcmp(name, "vkDestroyPipeline") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyPipeline);
  if (std::strcmp(name, "vkCmdBindPipeline") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdBindPipeline);
  if (std::strcmp(name, "vkCmdBindDescriptorSets") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdBindDescriptorSets);
  if (std::strcmp(name, "vkCmdPushConstants") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdPushConstants);
  if (std::strcmp(name, "vkCmdSetViewport") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdSetViewport);
  if (std::strcmp(name, "vkCmdSetScissor") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdSetScissor);
  if (std::strcmp(name, "vkCmdBeginRendering") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdBeginRendering);
  if (std::strcmp(name, "vkCmdEndRendering") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdEndRendering);
  if (std::strcmp(name, "vkCmdDraw") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdDraw);
  return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetInstanceProcAddr(VkInstance, const char* name) {
  if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumerateInstanceVersion);
  if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateInstance);
  if (std::strcmp(name, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyInstance);
  if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumeratePhysicalDevices);
  if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceProperties);
  if (std::strcmp(name, "vkGetPhysicalDeviceProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceProperties2);
  if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceFormatProperties);
  if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceMemoryProperties);
  if (std::strcmp(name, "vkGetPhysicalDeviceFeatures2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceFeatures2);
  if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceQueueFamilyProperties);
  if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumerateDeviceExtensionProperties);
  if (std::strcmp(name, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateDevice);
  if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetDeviceProcAddr);
  if (std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDevice);
  return nullptr;
}

std::unique_ptr<vk::VulkanDeviceContext> Context() {
  auto result = vk::VulkanDeviceContext::Create(
      vk::VulkanLoader::FromGetInstanceProcAddr(FakeGetInstanceProcAddr));
  Check(static_cast<bool>(result), "injected Vulkan context failed to create");
  return std::move(result.context);
}

vk::VulkanGuestImageRequest Request(P::ImageType type = P::ImageType::kColor2D) {
  vk::VulkanGuestImageRequest request;
  request.input.guest_address = 0x1000;
  request.input.format = P::GpuEnumValue(P::BufferFormat::k8_8_8_8UNorm);
  request.input.width = 2; request.input.height = 2; request.input.depth = 1;
  request.input.image_type = type;
  request.request_sibling_view = true;
  return request;
}

vk::VulkanGuestImageRequest RenderTargetRequest() {
  vk::VulkanGuestImageRequest request;
  request.input.guest_address = 0x10000;
  request.input.format = P::GpuEnumValue(P::BufferFormat::k8_8_8_8UNorm);
  request.input.width = 17;
  request.input.height = 9;
  request.input.depth = 1;
  request.input.image_type = P::ImageType::kColor2D;
  request.input.tile_mode = P::TileMode::kRenderTarget;
  request.request_sibling_view = false;
  return request;
}

std::byte RenderTargetActiveByte(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t byte) {
  return static_cast<std::byte>((x * 17u + y * 43u + byte * 71u) & 0xffu);
}

bool FillReferenceRenderTarget64KB(const kajps5::gpu::GuestImageLayout& layout,
                                   std::span<const std::byte> expected_linear,
                                   std::span<std::byte> tiled) {
  const auto bytes_per_element = P::NumBytesPerElement(layout.view_format);
  const auto& mip = layout.mips[0];
  kajps5::gpu::TileBlockLayout block{};
  if (!kajps5::gpu::TileGetBlockLayout(
          kajps5::gpu::TileBlockFamily::RenderTarget64KB, bytes_per_element, block) ||
      expected_linear.size() != layout.total_bytes || tiled.size() != layout.guest_storage_bytes ||
      mip.row_bytes % bytes_per_element != 0) {
    return false;
  }

  std::fill(tiled.begin(), tiled.end(), std::byte{0x5d});
  const std::uint64_t pitch = mip.row_bytes / bytes_per_element;
  const std::uint64_t columns = pitch / block.block_width +
      (pitch % block.block_width == 0 ? 0 : 1);
  for (std::uint32_t y = 0; y < mip.height; ++y) {
    for (std::uint32_t x = 0; x < mip.width; ++x) {
      const std::uint32_t block_x = x / block.block_width;
      const std::uint32_t block_y = y / block.block_height;
      const std::uint32_t local_x = x % block.block_width;
      const std::uint32_t local_y = y % block.block_height;
      std::uint32_t local_offset = 0;
      std::uint32_t block_xor = 0;
      if (!kajps5::gpu::TileGetBlockOffset(block, local_x, local_y, 0, local_offset) ||
          !kajps5::gpu::TileGetBlockXor(block, block_x, block_y, 0, block_xor)) {
        return false;
      }
      const std::uint64_t tiled_offset =
          (static_cast<std::uint64_t>(block_y) * columns + block_x) * block.block_size +
          (local_offset ^ block_xor);
      const std::uint64_t linear_offset =
          static_cast<std::uint64_t>(y) * mip.row_bytes + x * bytes_per_element;
      if (tiled_offset + bytes_per_element > tiled.size() ||
          linear_offset + bytes_per_element > expected_linear.size()) {
        return false;
      }
      for (std::uint32_t byte = 0; byte < bytes_per_element; ++byte) {
        tiled[tiled_offset + byte] = expected_linear[linear_offset + byte];
      }
    }
  }
  return true;
}

kajps5::gpu::shader::recompiler::CompileResult SampledImageCompile(
    bool storage = false, bool srgb = false) {
  kajps5::gpu::shader::recompiler::CompileResult compile;
  auto& program = compile.program;
  program.stage = kajps5::gpu::ShaderType::Compute;
  program.resource_tracking_complete = true;
  program.shader_info_complete = true;
  program.binding_layout_complete = true;
  program.bindings.descriptor_set = 0;
  program.info.images.push_back({.source = 0, .kind = storage ?
      ir::ResourceKind::StorageImage : ir::ResourceKind::Image,
      .dimension = kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2D,
      .storage_swizzle = storage ? 0u : ir::StorageImageIdentitySwizzle,
      .read = true, .written = storage});
  program.info.samplers.push_back({.source = 0});
  program.bindings.descriptors.push_back({storage ? ir::DescriptorBindingKind::Storage2D :
      ir::DescriptorBindingKind::Sampled2D, 2, {0}});
  program.bindings.descriptors.push_back({ir::DescriptorBindingKind::Samplers, 3, {0}});
  ir::DescriptorValue image;
  image.dword_count = 8;
  image.dwords[0] = 0x10;
  image.dwords[1] = (srgb ? P::GpuEnumValue(P::BufferFormat::k8_8_8_8Srgb) :
      P::GpuEnumValue(P::BufferFormat::k8_8_8_8UNorm)) << 20u;
  image.dwords[2] = 1u | (1u << 14u);
  image.dwords[3] = static_cast<std::uint32_t>(P::ImageType::kColor2D) << 28u;
  compile.resources.images.push_back(image);
  ir::DescriptorValue sampler;
  sampler.dword_count = 4;
  compile.resources.samplers.push_back(sampler);
  return compile;
}

void SetTextureFormat(ir::DescriptorValue& descriptor, P::BufferFormat format) {
  descriptor.dwords[1] = (descriptor.dwords[1] & ~(0x1ffu << 20u)) |
      (P::GpuEnumValue(format) << 20u);
}

kajps5::gpu::shader::recompiler::CompileResult FormatAliasCompile(
    P::BufferFormat first_format, bool first_storage,
    P::BufferFormat second_format, bool second_storage) {
  auto compile = SampledImageCompile();
  compile.program.info.samplers.clear();
  compile.resources.samplers.clear();
  compile.program.bindings.descriptors.clear();
  SetTextureFormat(compile.resources.images[0], first_format);
  compile.program.info.images[0].kind = first_storage ? ir::ResourceKind::StorageImage :
      ir::ResourceKind::Image;
  compile.program.info.images[0].storage_swizzle = first_storage ? 0u :
      ir::StorageImageIdentitySwizzle;
  compile.program.info.images[0].written = first_storage;
  auto second = compile.resources.images[0];
  SetTextureFormat(second, second_format);
  compile.resources.images.push_back(second);
  auto second_info = compile.program.info.images[0];
  second_info.kind = second_storage ? ir::ResourceKind::StorageImage :
      ir::ResourceKind::Image;
  second_info.storage_swizzle = second_storage ? 0u : ir::StorageImageIdentitySwizzle;
  second_info.written = second_storage;
  compile.program.info.images.push_back(second_info);
  compile.program.bindings.descriptors.push_back({first_storage ?
      ir::DescriptorBindingKind::Storage2D : ir::DescriptorBindingKind::Sampled2D, 2, {0}});
  compile.program.bindings.descriptors.push_back({second_storage ?
      ir::DescriptorBindingKind::Storage2D : ir::DescriptorBindingKind::Sampled2D, 4, {1}});
  return compile;
}

VkFormat CapturedViewFormat(VkImageView view) {
  const auto found = std::find_if(g.image_view_create_infos.begin(),
      g.image_view_create_infos.end(), [=](const FakeImageViewCreateInfo& info) {
        return info.created_view == view;
      });
  Check(found != g.image_view_create_infos.end(),
        "translated descriptor referenced an uncaptured image view");
  return found->format;
}

void CheckExactCleanup(std::initializer_list<std::string_view> expected) {
  Check(g.teardown.size() == expected.size(), "cleanup operation count differs");
  std::size_t index = 0;
  for (const std::string_view kind : expected) {
    if (g.teardown[index].kind != kind) {
      std::cerr << "vulkan_image_cache_test: cleanup expected " << kind
                << " but saw " << g.teardown[index].kind << " at failure "
                << static_cast<int>(g.fail) << '\n';
      std::exit(1);
    }
    ++index;
  }
  for (const FakeOperation& issued : g.issued) {
    const auto destroyed = std::count_if(g.teardown.begin(), g.teardown.end(),
        [&](const FakeOperation& operation) {
          return operation.kind == issued.kind && operation.handle == issued.handle;
        });
    Check(destroyed == 1, "an acquired or poisoned fake handle was not cleaned exactly once");
  }
  for (const FakeOperation& operation : g.teardown) {
    if (operation.kind == "unmap") continue;
    const auto issued = std::count_if(g.issued.begin(), g.issued.end(),
        [&](const FakeOperation& candidate) {
          return candidate.kind == operation.kind && candidate.handle == operation.handle;
        });
    Check(issued == 1, "cleanup used a handle that was never issued by the fake");
  }
  for (const FakeOperation& poison : g.poisoned) {
    const auto destroyed = std::count_if(g.teardown.begin(), g.teardown.end(),
        [&](const FakeOperation& operation) {
          return operation.kind == poison.kind && operation.handle == poison.handle;
        });
    Check(destroyed == 0, "cleanup adopted a poison value from a failed Vulkan call");
  }
}

void TestReadOnlyRenderTargetDetilePreparation() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  constexpr auto protection = Protection::kRead | Protection::kWrite |
      Protection::kGpuRead | Protection::kGpuWrite;

  const auto request = RenderTargetRequest();
  const auto layout = kajps5::gpu::CalculateGuestImageLayout(request.input);
  Check(layout.ok() && layout.needs_detile && layout.guest_storage_bytes == 64 * 1024 &&
            layout.total_bytes < layout.guest_storage_bytes,
        "render-target fixture did not calculate a padded tiled layout");
  const auto bytes_per_element = P::NumBytesPerElement(layout.view_format);
  std::vector<std::byte> expected_linear(layout.total_bytes, std::byte{0});
  for (std::uint32_t y = 0; y < layout.mips[0].height; ++y) {
    for (std::uint32_t x = 0; x < layout.mips[0].width; ++x) {
      const std::uint64_t offset = static_cast<std::uint64_t>(y) * layout.mips[0].row_bytes +
          x * bytes_per_element;
      for (std::uint32_t byte = 0; byte < bytes_per_element; ++byte) {
        expected_linear[offset + byte] = RenderTargetActiveByte(x, y, byte);
      }
    }
  }
  std::vector<std::byte> tiled(layout.guest_storage_bytes);
  Check(FillReferenceRenderTarget64KB(layout, expected_linear, tiled),
        "reference RenderTarget64KB tiler rejected the fixture");

  Reset();
  auto context = Context();
  kajps5::memory::GuestMemory memory(request.input.guest_address,
      static_cast<std::size_t>(layout.guest_storage_bytes), protection);
  Check(memory.Initialize(request.input.guest_address, tiled),
        "tiled guest allocation initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto prepared = cache.Prepare(request);
  Check(prepared && prepared.layout.needs_detile &&
            prepared.layout.guest_storage_bytes == 64 * 1024 &&
            prepared.layout.total_bytes < prepared.layout.guest_storage_bytes &&
            prepared.uploaded_bytes == expected_linear,
        "read-only tiled image did not prepare the expected linear upload");
  Check(g.mapped_bytes.size() >= expected_linear.size() &&
            std::equal(expected_linear.begin(), expected_linear.end(), g.mapped_bytes.begin()),
        "tiled image staging prefix differs from the expected linear bytes");
  Check(prepared.copy_regions.size() == 1 &&
            prepared.copy_regions[0].imageExtent.width == 17 &&
            prepared.copy_regions[0].imageExtent.height == 9 &&
            prepared.copy_regions[0].bufferRowLength ==
                layout.mips[0].row_bytes / bytes_per_element,
        "tiled image copy region does not retain the planned linear pitch");
  const auto resource = prepared.resource;
  const auto uploaded = runtime.resource_coherence().Query(resource);
  Check(uploaded && uploaded->mapped && !uploaded->mapping_changed &&
            !uploaded->needs_cpu_upload,
        "tiled image resource was not fully mapped and upload acknowledged");

  const std::array<std::byte, 1> tail = {std::byte{0xe2}};
  Check(memory.Write(request.input.guest_address + layout.guest_storage_bytes - 1, tail),
        "padded tiled allocation tail mutation failed");
  const auto tail_dirty = runtime.resource_coherence().Query(resource);
  Check(tail_dirty && tail_dirty->needs_cpu_upload,
        "padded tiled allocation tail mutation did not invalidate the image upload");
  cache.Discard(prepared);
  Check(!runtime.resource_coherence().Query(resource),
        "discard did not unregister the tiled image resource");

  const std::size_t issued_before_writable = g.issued.size();
  auto writable = request;
  writable.writable = true;
  const auto rejected_writable = cache.Prepare(writable);
  Check(!rejected_writable &&
            rejected_writable.status == vk::VulkanGuestImageStatus::kUnsupportedTopology &&
            rejected_writable.resource == 0 && g.issued.size() == issued_before_writable &&
            !runtime.resource_coherence().Query(resource + 1),
        "tiled writable image published coherence or Vulkan resources");

  Reset();
  auto short_context = Context();
  kajps5::memory::GuestMemory short_memory(request.input.guest_address,
      static_cast<std::size_t>(layout.guest_storage_bytes), Protection::kNone);
  Check(short_memory.Map(request.input.guest_address, layout.total_bytes, protection),
        "short tiled fixture could not map the linear staging range");
  kajps5::gpu::GpuRuntime short_runtime(short_memory);
  vk::VulkanGuestImageCache short_cache(*short_context, short_memory,
                                        short_runtime.resource_coherence());
  const auto short_result = short_cache.Prepare(request);
  Check(!short_result &&
            short_result.status == vk::VulkanGuestImageStatus::kGuestMemoryProtection &&
            short_result.resource == 0 && g.issued.empty() &&
            !short_runtime.resource_coherence().Query(1),
        "short tiled guest mapping reached coherence or Vulkan preparation");
}

void TestPreparationAndTopology() {
  Reset();
  auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x1000, 0x1000, Protection::kRead | Protection::kWrite | Protection::kGpuRead | Protection::kGpuWrite);
  const std::array<std::byte, 16> bytes = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  Check(memory.Initialize(0x1000, bytes), "test guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto prepared = cache.Prepare(Request());
  Check(prepared && prepared.image && prepared.image_memory && prepared.view && prepared.sibling_view &&
            prepared.staging_buffer && prepared.staging_memory && prepared.staging_mapped,
        "successful preparation did not retain all resource leases");
  Check(g.image_type == VK_IMAGE_TYPE_2D && g.view_type == VK_IMAGE_VIEW_TYPE_2D &&
            (g.image_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) != 0 && g.image_layers == 1,
        "2D mutable sibling topology was not requested");
  Check(g.flushes == 1 && g.mapped_bytes.size() >= bytes.size() &&
            std::memcmp(g.mapped_bytes.data(), bytes.data(), bytes.size()) == 0,
        "noncoherent staging upload or flush differs");
  Check(prepared.copy_regions.size() == 1 && prepared.copy_regions[0].imageExtent.width == 2,
        "copy region was not published");
  cache.Discard(prepared);
  Check(!prepared.image && !prepared.staging_buffer && prepared.resource == 0,
        "discard retained a Vulkan or coherence handle");
  CheckExactCleanup({"unmap", "view", "view", "buffer", "image", "memory", "memory"});
  const std::size_t teardown_after_first_discard = g.teardown.size();
  cache.Discard(prepared);
  Check(g.teardown.size() == teardown_after_first_discard,
        "second discard performed additional teardown");

  const struct { P::ImageType type; VkImageType image; VkImageViewType view; } cases[] = {
      {P::ImageType::kColor1D, VK_IMAGE_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D},
      {P::ImageType::kColor1DArray, VK_IMAGE_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D_ARRAY},
      {P::ImageType::kColor2D, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D},
      {P::ImageType::kColor2DArray, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY},
      {P::ImageType::kColor3D, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D},
      {P::ImageType::kCube, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE},
  };
  for (const auto& test : cases) {
    Reset();
    auto request = Request(test.type);
    if (test.type == P::ImageType::kColor1D || test.type == P::ImageType::kColor1DArray)
      request.input.height = 1;
    if (test.type == P::ImageType::kColor1DArray) request.input.layers = 2;
    if (test.type == P::ImageType::kColor3D) request.input.depth = 2;
    if (test.type == P::ImageType::kCube) { request.input.width = 2; request.input.height = 2; request.input.layers = 1; }
    if (test.type == P::ImageType::kColor2DArray) request.input.layers = 2;
    auto next = cache.Prepare(request);
    Check(static_cast<bool>(next), "topology preparation failed");
    Check(g.image_type == test.image && g.view_type == test.view,
          "image type did not map to expected Vulkan topology");
    Check(g.image_create_infos.size() == 1 && g.image_view_create_infos.size() == 2 &&
              g.image_create_infos[0].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED,
          "fake image create-info capture did not retain scalar topology records");
    cache.Discard(next);
  }

  Reset();
  auto ranged = Request(P::ImageType::kColor2DArray);
  ranged.input.layers = 2;
  ranged.input.mip_count = 2;
  ranged.view_base_mip_level = 1;
  ranged.view_level_count = 1;
  ranged.view_base_array_layer = 1;
  ranged.view_layer_count = 1;
  auto subrange = cache.Prepare(ranged);
  Check(subrange && g.image_create_infos[0].mip_levels == 2 &&
            g.image_create_infos[0].array_layers == 2 &&
            g.image_view_create_infos[0].base_mip_level == 1 &&
            g.image_view_create_infos[0].level_count == 1 &&
            g.image_view_create_infos[0].base_array_layer == 1 &&
            g.image_view_create_infos[0].layer_count == 1,
        "nonzero mip or array view subrange was not retained in the Vulkan view");
  cache.Discard(subrange);

  Reset();
  auto cube_array = Request(P::ImageType::kCube);
  cube_array.input.layers = 2;
  cube_array.view_type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  auto cubes = cache.Prepare(cube_array);
  Check(cubes && (g.image_create_infos[0].flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0 &&
            g.image_view_create_infos[0].view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY &&
            g.image_view_create_infos[0].layer_count == 12,
        "cube-array topology did not request cube-compatible 2D-array storage");
  cache.Discard(cubes);

  Reset();
  auto bad_cube = Request(P::ImageType::kCube);
  bad_cube.view_base_array_layer = 1;
  Check(!cache.Prepare(bad_cube) && g.image_create_infos.empty(),
        "misaligned cube view reached Vulkan image creation");
}

void TestCoherentAndRollback() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  const FailPoint failures[] = {FailPoint::kCreateImage, FailPoint::kAllocateImage,
      FailPoint::kBindImage, FailPoint::kCreateFirstView, FailPoint::kCreateSecondView,
      FailPoint::kCreateBuffer, FailPoint::kAllocateStaging, FailPoint::kBindStaging, FailPoint::kMap};
  const std::initializer_list<std::string_view> cleanup[] = {
      {},
      {"image"},
      {"image", "memory"},
      {"image", "memory"},
      {"view", "image", "memory"},
      {"view", "view", "image", "memory"},
      {"buffer", "view", "view", "image", "memory"},
      {"buffer", "view", "view", "image", "memory", "memory"},
      {"buffer", "view", "view", "image", "memory", "memory"},
  };
  for (std::size_t index = 0; index < std::size(failures); ++index) {
    const auto failure = failures[index];
    Reset(failure); auto context = Context();
    kajps5::memory::GuestMemory memory(0x1000, 0x1000, Protection::kRead | Protection::kWrite | Protection::kGpuRead | Protection::kGpuWrite);
    Check(memory.InitializeFill(0x1000, 16, std::byte{0x7a}), "rollback guest initialization failed");
    kajps5::gpu::GpuRuntime runtime(memory);
    vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
    auto result = cache.Prepare(Request());
    Check(!result && !result.image && !result.image_memory && !result.view && !result.sibling_view &&
              !result.staging_buffer && !result.staging_memory && result.resource == 0,
          "failed Vulkan preparation published a poisoned partial handle");
    if (g.teardown.size() != cleanup[index].size()) {
      std::cerr << "vulkan_image_cache_test: failure index " << index
                << " cleanup count mismatch\n";
      std::exit(1);
    }
    CheckExactCleanup(cleanup[index]);
  }
  Reset(FailPoint::kNone, true); auto context = Context();
  kajps5::memory::GuestMemory memory(0x1000, 0x1000, Protection::kRead | Protection::kWrite | Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0x11}), "coherent guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto result = cache.Prepare(Request());
  Check(result && result.staging_host_coherent && g.flushes == 0,
        "coherent staging allocation unexpectedly flushed");
  cache.Discard(result);
}

void TestTransferLeaseAndCoherence() {
  Reset();
  auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  const std::array<std::byte, 16> initial = {
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
      std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12},
      std::byte{13}, std::byte{14}, std::byte{15}, std::byte{16}};
  Check(memory.Initialize(0x1000, initial), "transfer guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto request = Request();
  request.writable = true;
  auto prepared = cache.Prepare(request);
  Check(prepared && (g.image_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 &&
            (g.buffer_usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0 &&
            (g.buffer_usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0,
        "writable image did not request bidirectional transfer usage");
  const auto command_buffer = Handle<VkCommandBuffer>(0x55);
  Check(!cache.RecordReadback(command_buffer, prepared,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT) &&
            !prepared.readback_recorded && g.barriers.empty() &&
            g.readback_copies.empty() && g.command_operations.empty(),
        "readback before upload changed the image lease");
  Check(cache.RecordUpload(command_buffer, prepared,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT),
        "upload lease did not record");
  Check(prepared.current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            prepared.upload_recorded && g.barriers.size() == 2 &&
            g.upload_copies.size() == prepared.copy_regions.size(),
        "upload did not emit its two transitions and all copies");
  Check(g.barriers[0].src_stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT &&
            g.barriers[0].dst_stage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
            g.barriers[0].images.size() == 1 &&
            g.barriers[0].images[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            g.barriers[0].images[0].newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            g.barriers[1].images[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        "upload layout barriers differ from the required transfer path");
  const std::array upload_operations = {
      FakeCommandOperation::kImageBarrier,
      FakeCommandOperation::kBufferToImageCopy,
      FakeCommandOperation::kImageBarrier,
  };
  Check(g.command_operations.size() == upload_operations.size() &&
            std::equal(g.command_operations.begin(), g.command_operations.end(),
                       upload_operations.begin()),
        "upload command order differs from barrier-copy-barrier");
  Check(!cache.RecordUpload(command_buffer, prepared,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT) &&
            g.barriers.size() == 2 &&
            g.command_operations.size() == upload_operations.size() &&
            prepared.current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        "duplicate upload recorded commands or changed lease state");
  Check(cache.RecordReadback(command_buffer, prepared,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_SHADER_WRITE_BIT),
        "readback lease did not record");
  Check(prepared.readback_recorded &&
            prepared.current_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
            g.barriers.size() == 4 &&
            g.readback_copies.size() == prepared.copy_regions.size() &&
            g.readback_copies[0].bufferOffset == prepared.copy_regions[0].bufferOffset,
        "readback did not emit its transition, copy, and host barrier");
  Check(g.barriers[2].images[0].oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            g.barriers[2].images[0].newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
            g.barriers[3].src_stage == VK_PIPELINE_STAGE_TRANSFER_BIT &&
            g.barriers[3].dst_stage == VK_PIPELINE_STAGE_HOST_BIT &&
            g.barriers[3].buffers.size() == 1 &&
            g.barriers[3].buffers[0].dstAccessMask == VK_ACCESS_HOST_READ_BIT,
        "readback synchronization differs from the required host-visible path");
  const std::array transfer_operations = {
      FakeCommandOperation::kImageBarrier,
      FakeCommandOperation::kBufferToImageCopy,
      FakeCommandOperation::kImageBarrier,
      FakeCommandOperation::kImageBarrier,
      FakeCommandOperation::kImageToBufferCopy,
      FakeCommandOperation::kBufferBarrier,
  };
  Check(g.command_operations.size() == transfer_operations.size() &&
            std::equal(g.command_operations.begin(), g.command_operations.end(),
                       transfer_operations.begin()),
        "upload/readback commands interleaved out of transfer order");
  for (const FakeBarrier& barrier : g.barriers) {
    for (const VkImageMemoryBarrier& image : barrier.images) {
      Check(image.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
                image.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED,
            "image barrier attempted queue-family ownership transfer");
    }
    for (const VkBufferMemoryBarrier& buffer : barrier.buffers) {
      Check(buffer.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
                buffer.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED,
            "buffer barrier attempted queue-family ownership transfer");
    }
  }
  Check(!cache.RecordReadback(command_buffer, prepared,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT) &&
            g.barriers.size() == 4 &&
            g.command_operations.size() == transfer_operations.size() &&
            prepared.current_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        "duplicate readback recorded commands or changed lease state");
  Check(cache.MarkSubmitted(prepared) && prepared.gpu_dirty,
        "submitted writable readback did not mark coherence dirty");
  Check(!cache.MarkSubmitted(prepared) && prepared.gpu_dirty,
        "duplicate submission changed GPU-dirty state");
  const std::array<std::byte, 1> cpu_only = {std::byte{0xe1}};
  Check(memory.Write(0x1008, cpu_only), "CPU-only guest mutation failed");
  g.mapped_bytes[3] = std::byte{0x9a};
  g.invalidate_available = false;
  std::array<std::byte, 16> before_completion{};
  Check(!cache.Complete(prepared) && prepared.gpu_dirty && g.invalidates == 0 &&
            memory.Read(0x1000, before_completion) &&
            before_completion[3] == initial[3] &&
            before_completion[8] == cpu_only[0],
        "missing noncoherent invalidate changed a dirty image lease");
  g.invalidate_available = true;
  Check(cache.Complete(prepared) && !prepared.gpu_dirty && g.invalidates == 1,
        "noncoherent readback completion did not invalidate and resolve dirty state");
  std::array<std::byte, 16> resolved{};
  Check(memory.Read(0x1000, resolved) && resolved[3] == std::byte{0x9a} &&
            resolved[8] == std::byte{0xe1},
        "readback did not preserve exact newer CPU-only bytes");
  const auto state = runtime.resource_coherence().Query(prepared.resource);
  Check(state && !state->gpu_write_pending,
        "coherence GPU-write pending flag was not cleared after completion");
  Check(!cache.Complete(prepared) && !prepared.gpu_dirty && g.invalidates == 1,
        "duplicate completion invalidated or changed a completed lease");
  cache.Discard(prepared);
}

void TestCoherentCompletion() {
  Reset(FailPoint::kNone, true);
  auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0x31}),
        "coherent completion guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto request = Request();
  request.writable = true;
  auto prepared = cache.Prepare(request);
  const auto command_buffer = Handle<VkCommandBuffer>(0x55);
  Check(prepared.staging_host_coherent &&
            cache.RecordUpload(command_buffer, prepared, VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) &&
            cache.RecordReadback(command_buffer, prepared,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_SHADER_WRITE_BIT) &&
            cache.MarkSubmitted(prepared),
        "coherent writable lease could not reach completion");
  g.mapped_bytes[5] = std::byte{0xbd};
  g.invalidate_available = false;
  Check(cache.Complete(prepared) && g.invalidates == 0,
        "coherent completion unexpectedly invalidated mapped staging memory");
  std::array<std::byte, 1> value{};
  Check(memory.Read(0x1005, value) && value[0] == std::byte{0xbd},
        "coherent completion did not publish the GPU byte");
  cache.Discard(prepared);
}

void TestMissingCommandFunctionsAreSideEffectFree() {
  Reset();
  g.commands_available = false;
  auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0x42}),
        "missing-command guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto prepared = cache.Prepare(Request());
  Check(prepared && !cache.RecordUpload(Handle<VkCommandBuffer>(0x55), prepared,
                                        VK_IMAGE_LAYOUT_GENERAL,
                                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_SHADER_READ_BIT) &&
            !prepared.upload_recorded && prepared.current_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
            g.barriers.empty() && g.upload_copies.empty() &&
            g.command_operations.empty(),
        "unresolved command functions changed image lease state");
  cache.Discard(prepared);
}

void TestTranslatedImageSamplerSet() {
  {
  Reset();
  auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0x10}),
        "translated image guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto sampled = cache.PrepareTranslated(SampledImageCompile());
  Check(sampled && sampled.images.size() == 1 && sampled.image_descriptors.size() == 1 &&
            sampled.samplers.size() == 1 && sampled.sampler_descriptors.size() == 1 &&
            sampled.image_descriptors[0].binding == 2 &&
            sampled.image_descriptors[0].descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            sampled.image_descriptors[0].layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            sampled.sampler_descriptors[0].binding == 3 &&
            g.sampler_info.magFilter == VK_FILTER_NEAREST &&
            g.sampler_info.addressModeU == VK_SAMPLER_ADDRESS_MODE_REPEAT,
        "translated sampled image/sampler topology differs");
  cache.Discard(sampled);
  const std::size_t teardown = g.teardown.size();
  cache.Discard(sampled);
  Check(g.teardown.size() == teardown, "translated set discard was not idempotent");
  auto unnormalized_compile = SampledImageCompile();
  unnormalized_compile.resources.samplers[0].dwords[0] = 1u << 15u;
  auto unnormalized = cache.PrepareTranslated(unnormalized_compile);
  Check(unnormalized && g.sampler_info.unnormalizedCoordinates == VK_TRUE &&
            g.sampler_info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
            g.sampler_info.minLod == 0.0f && g.sampler_info.maxLod == 0.0f,
        "ForceUnormCoords did not produce Kyty-compatible Vulkan restrictions");
  cache.Discard(unnormalized);
  }

  {
  Reset(); auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory storage_memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(storage_memory.InitializeFill(0x1000, 16, std::byte{0x10}),
        "storage image guest initialization failed");
  kajps5::gpu::GpuRuntime storage_runtime(storage_memory);
  vk::VulkanGuestImageCache storage_cache(*context, storage_memory,
                                           storage_runtime.resource_coherence());
  auto storage = storage_cache.PrepareTranslated(SampledImageCompile(true, true));
  Check(storage && storage.image_descriptors[0].descriptor_type ==
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
            storage.image_descriptors[0].layout == VK_IMAGE_LAYOUT_GENERAL &&
            storage.image_descriptors[0].view == storage.images[0].sibling_view &&
            storage.image_descriptors[0].shader_writes,
        "sRGB storage image did not select the compatible UNORM sibling view");
  storage_cache.Discard(storage);
  }

  {
  Reset(); auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0x10}),
        "access-union guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto union_compile = SampledImageCompile(true);
  union_compile.program.bindings.descriptors[0].binding = 4;
  union_compile.program.bindings.descriptors.insert(
      union_compile.program.bindings.descriptors.begin(),
      {ir::DescriptorBindingKind::Sampled2D, 2, {0}});
  auto merged = cache.PrepareTranslated(union_compile);
  Check(merged && merged.images.size() == 1 && merged.image_descriptors.size() == 2 &&
            (g.image_usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 &&
            (g.image_usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0 &&
            (g.buffer_usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0 &&
            merged.image_descriptors[0].descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            merged.image_descriptors[1].descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        "sampled/storage access union did not create one writable union lease");
  cache.Discard(merged);
  }
}

void TestTranslatedDescriptorDeviceLimits() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  const auto reject = [](const char* message,
                         const kajps5::gpu::shader::recompiler::CompileResult& compile) {
    auto context = Context();
    kajps5::memory::GuestMemory memory(
        0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                            Protection::kGpuRead | Protection::kGpuWrite);
    Check(memory.InitializeFill(0x1000, 16, std::byte{0x10}),
          "descriptor-limit guest initialization failed");
    kajps5::gpu::GpuRuntime runtime(memory);
    vk::VulkanGuestImageCache cache(*context, memory,
                                    runtime.resource_coherence());
    const auto prepared = cache.PrepareTranslated(compile);
    Check(!prepared && prepared.status == vk::VulkanGuestImageSetStatus::kResourceLimit &&
              g.issued.empty() && !runtime.resource_coherence().Query(1),
          message);
  };

  Reset();
  g.max_per_stage_descriptor_storage_buffers = 0;
  {
    auto context = Context();
    kajps5::memory::GuestMemory memory(
        0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                            Protection::kGpuRead | Protection::kGpuWrite);
    Check(memory.InitializeFill(0x1000, 16, std::byte{0x10}),
          "zero buffer-limit guest initialization failed");
    kajps5::gpu::GpuRuntime runtime(memory);
    vk::VulkanGuestBufferCache cache(*context, memory,
                                     runtime.resource_coherence());
    kajps5::gpu::shader::recompiler::CompileResult compile;
    auto& program = compile.program;
    program.stage = kajps5::gpu::ShaderType::Compute;
    program.resource_tracking_complete = program.shader_info_complete =
        program.binding_layout_complete = true;
    program.bindings.descriptor_set = 0;
    program.bindings.push_constant_size = 4;
    program.bindings.buffer_offset_count = 1;
    program.bindings.descriptors.push_back(
        {ir::DescriptorBindingKind::Buffers, 3, {0}});
    program.info.buffers.push_back({.source = 0, .max_byte_extent = 16,
                                    .packed_stride = 4, .descriptor_format = 0,
                                    .read = true});
    ir::DescriptorValue descriptor;
    descriptor.dword_count = 4;
    descriptor.dwords[0] = 0x1000;
    descriptor.dwords[1] = 4U << 16U;
    descriptor.dwords[2] = 4;
    compile.resources.buffers.push_back(descriptor);
    const auto prepared = cache.Prepare(compile);
    Check(!prepared && prepared.status == vk::VulkanGuestBufferStatus::kResourceLimit &&
              g.issued.empty() && !runtime.resource_coherence().Query(1),
          "zero storage-buffer descriptor limit reached unsafe buffer preparation");
  }

  Reset();
  g.max_per_stage_descriptor_sampled_images = 0;
  reject("zero sampled-image limit reached unsafe image preparation",
         SampledImageCompile());

  Reset();
  g.max_descriptor_set_storage_images = 0;
  reject("zero storage-image limit reached unsafe image preparation",
         SampledImageCompile(true));

  Reset();
  g.max_per_stage_descriptor_samplers = 0;
  reject("zero sampler limit reached unsafe sampler preparation",
         SampledImageCompile());
}

void TestTranslatedAliasesAndInvalidInputs() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  Reset();
  auto context = Context();
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 0x1000, std::byte{0x20}),
        "translated alias guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());

  const struct {
    P::ImageType type;
    kajps5::gpu::shader::recompiler::Decoder::ImageDimension dimension;
    ir::DescriptorBindingKind binding;
    VkImageType image_type;
    VkImageViewType view_type;
    std::uint32_t depth_minus_one;
    bool cube;
  } translated_topologies[] = {
      {P::ImageType::kColor1D, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim1D,
       ir::DescriptorBindingKind::Sampled1D, VK_IMAGE_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D, 0, false},
      {P::ImageType::kColor1DArray, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim1DArray,
       ir::DescriptorBindingKind::Sampled1DArray, VK_IMAGE_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D_ARRAY, 1, false},
      {P::ImageType::kColor2D, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2D,
       ir::DescriptorBindingKind::Sampled2D, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, 0, false},
      {P::ImageType::kColor2DArray, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2DArray,
       ir::DescriptorBindingKind::Sampled2DArray, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1, false},
      {P::ImageType::kColor3D, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim3D,
       ir::DescriptorBindingKind::Sampled3D, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D, 1, false},
      {P::ImageType::kCube, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2DArray,
       ir::DescriptorBindingKind::Sampled2DArray, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, 5, true},
      {P::ImageType::kCube, kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2DArray,
       ir::DescriptorBindingKind::Sampled2DArray, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, 11, true},
  };
  for (const auto& topology : translated_topologies) {
    Reset();
    auto compile = SampledImageCompile();
    compile.program.info.images[0].dimension = topology.dimension;
    compile.program.info.images[0].cube = topology.cube;
    compile.program.bindings.descriptors[0].kind = topology.binding;
    compile.resources.images[0].dwords[3] = static_cast<std::uint32_t>(topology.type) << 28u;
    compile.resources.images[0].dwords[4] = topology.depth_minus_one;
    if (topology.cube) compile.resources.images[0].dwords[2] = 1u | (4u << 14u);
    auto set = cache.PrepareTranslated(compile);
    const bool correct_topology = set && g.image_create_infos.size() == 1 &&
        g.image_view_create_infos.size() >= 1 &&
        g.image_create_infos[0].image_type == topology.image_type &&
        g.image_view_create_infos[0].view_type == topology.view_type &&
        (!topology.cube || (g.image_create_infos[0].flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0);
    Check(correct_topology, "translated image descriptor did not preserve the requested Vulkan topology");
    cache.Discard(set);
  }

  Reset();
  auto translated_subrange = SampledImageCompile();
  translated_subrange.program.info.images[0].dimension =
      kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2DArray;
  translated_subrange.program.bindings.descriptors[0].kind = ir::DescriptorBindingKind::Sampled2DArray;
  translated_subrange.resources.images[0].dwords[3] =
      (static_cast<std::uint32_t>(P::ImageType::kColor2DArray) << 28u) |
      (1u << 12u) | (1u << 16u);
  translated_subrange.resources.images[0].dwords[4] = 1u | (1u << 16u);
  translated_subrange.resources.images[0].dwords[5] = 1u << 4u;
  auto subrange_set = cache.PrepareTranslated(translated_subrange);
  Check(subrange_set && g.image_create_infos[0].mip_levels == 2 &&
            g.image_create_infos[0].array_layers == 3 &&
            g.image_view_create_infos[0].base_mip_level == 1 &&
            g.image_view_create_infos[0].level_count == 1 &&
            g.image_view_create_infos[0].base_array_layer == 1 &&
            g.image_view_create_infos[0].layer_count == 2,
        "translated descriptor did not retain its nonzero mip and array subranges");
  cache.Discard(subrange_set);

  Reset();
  auto aliases = SampledImageCompile();
  aliases.program.info.images[0].dimension =
      kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2DArray;
  aliases.program.bindings.descriptors[0].kind = ir::DescriptorBindingKind::Sampled2DArray;
  aliases.resources.images[0].dwords[3] =
      static_cast<std::uint32_t>(P::ImageType::kColor2DArray) << 28u;
  aliases.resources.images[0].dwords[4] = 2;  // Three storage layers, view [0, 3).
  auto narrow = aliases.resources.images[0];
  narrow.dwords[4] = 1u | (1u << 16u);  // Same storage key, view [1, 3).
  aliases.resources.images.push_back(narrow);
  aliases.resources.images.push_back(narrow);
  aliases.program.info.images.push_back(aliases.program.info.images[0]);
  aliases.program.info.images.push_back(aliases.program.info.images[0]);
  aliases.program.bindings.descriptors[0].resources = {0, 1, 2};
  auto prepared = cache.PrepareTranslated(aliases);
  const bool aliases_ok = prepared && prepared.images.size() == 1 && prepared.image_descriptors.size() == 3 &&
            prepared.image_descriptors[0].preparation_index == 0 &&
            prepared.image_descriptors[1].preparation_index == 0 &&
            prepared.image_descriptors[1].view != prepared.image_descriptors[0].view &&
            prepared.image_descriptors[2].view == prepared.image_descriptors[1].view &&
            prepared.auxiliary_views.size() == 1 && g.image_create_infos.size() == 1 &&
            g.image_view_create_infos.size() == 3 &&
            g.image_view_create_infos.back().base_array_layer == 1 &&
            g.image_view_create_infos.back().layer_count == 2;
  Check(aliases_ok, "exact storage aliases did not share one image and deduplicate their auxiliary view");
  cache.Discard(prepared);
  CheckExactCleanup({"view", "sampler", "unmap", "view", "view", "buffer", "image", "memory", "memory"});
  const std::size_t teardown = g.teardown.size();
  cache.Discard(prepared);
  Check(g.teardown.size() == teardown, "alias set discard was not inert after its first call");

  Reset(FailPoint::kCreateAuxiliaryView);
  auto auxiliary_failure = cache.PrepareTranslated(aliases);
  Check(!auxiliary_failure, "poisoned auxiliary-view creation unexpectedly succeeded");
  CheckExactCleanup({"unmap", "view", "view", "buffer", "image", "memory", "memory"});

  Reset();
  auto overlap = SampledImageCompile();
  overlap.resources.images.push_back(overlap.resources.images[0]);
  overlap.resources.images[1].dwords[2] = 3u | (1u << 14u);  // Wider overlapping storage.
  overlap.program.info.images.push_back(overlap.program.info.images[0]);
  overlap.program.bindings.descriptors[0].resources = {0, 1};
  Check(!cache.PrepareTranslated(overlap) && g.issued.empty() && g.teardown.empty(),
        "incompatible overlapping storage reached Vulkan work");

  const auto CheckInvalid = [&](auto mutate, std::string_view message) {
    Reset();
    auto invalid = SampledImageCompile();
    mutate(invalid);
    const auto result = cache.PrepareTranslated(invalid);
    Check(!result && g.issued.empty() && g.teardown.empty(), message);
  };
  CheckInvalid([](auto& c) { c.program.stage = kajps5::gpu::ShaderType::Pixel; },
               "wrong shader stage reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.binding_layout_complete = false; },
               "incomplete specialization reached Vulkan work");
  CheckInvalid([](auto& c) { c.resources.images[0].dwords[3] =
                    static_cast<std::uint32_t>(P::ImageType::kColor3D) << 28u; },
               "changed specialization reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.bindings.descriptors[1].binding = 2; },
               "duplicate descriptor binding reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.bindings.descriptors[0].resources = {4}; },
               "invalid dense image index reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.bindings.descriptors[0].kind = ir::DescriptorBindingKind::Gds; },
               "unsupported descriptor group reached Vulkan work");
  CheckInvalid([](auto& c) { c.resources.images[0].dwords[0] = 0; c.resources.images[0].dwords[1] &= ~0xffu; },
               "null image address reached Vulkan work");
  CheckInvalid([](auto& c) { c.resources.images[0].dwords[3] |= 1u << 20u; },
               "tiled image descriptor reached Vulkan work");
  CheckInvalid([](auto& c) { c.resources.images[0].dwords[3] =
                    static_cast<std::uint32_t>(P::ImageType::kColor2DMsaa) << 28u; },
               "MSAA descriptor reached Vulkan work");
  CheckInvalid([](auto& c) { c.resources.images[0].dwords[3] |= 2u << 12u; },
               "malformed mip view range reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.bindings.descriptors[0].kind = ir::DescriptorBindingKind::Sampled3D; },
               "wrong image binding dimension reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.info.images[0].kind = ir::ResourceKind::ImageUint; },
               "wrong image binding numeric class reached Vulkan work");
  CheckInvalid([](auto& c) { c.program.info.images[0].cube = true; },
               "invalid cube specialization reached Vulkan work");
  CheckInvalid([](auto& c) {
    for (std::uint32_t index = 1; index <= ir::ShaderInfo::MaxImages; ++index) {
      c.program.info.images.push_back(c.program.info.images[0]);
      c.resources.images.push_back(c.resources.images[0]);
    }
  }, "fixed ShaderInfo image bound reached Vulkan work");

  Reset();
  auto compressed = SampledImageCompile(true, true);
  compressed.resources.images[0].dwords[1] =
      P::GpuEnumValue(P::BufferFormat::kBc1Srgb) << 20u;
  Check(!cache.PrepareTranslated(compressed) && g.issued.empty() && g.teardown.empty(),
        "compressed storage image reached Vulkan work");
}

void TestTranslatedSamplerMatrixAndRollback() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  Reset();
  auto context = Context();
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 0x400, std::byte{0x30}),
        "sampler matrix guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());

  const VkSamplerAddressMode addresses[] = {
      VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
  };
  for (std::uint32_t value = 0; value < std::size(addresses); ++value) {
    Reset();
    auto compile = SampledImageCompile();
    compile.resources.samplers[0].dwords[0] = value | (value << 3u) | (value << 6u);
    auto set = cache.PrepareTranslated(compile);
    Check(set && g.sampler_info.addressModeU == addresses[value] &&
              g.sampler_info.addressModeV == addresses[value] &&
              g.sampler_info.addressModeW == addresses[value],
        "sampler clamp enum did not map to its Vulkan address mode");
    cache.Discard(set);
  }
  const VkFilter filters[] = {VK_FILTER_NEAREST, VK_FILTER_LINEAR,
                              VK_FILTER_NEAREST, VK_FILTER_LINEAR};
  for (std::uint32_t value = 0; value < std::size(filters); ++value) {
    Reset();
    auto compile = SampledImageCompile();
    compile.resources.samplers[0].dwords[0] = 4u << 9u;
    compile.resources.samplers[0].dwords[2] = value << 20u | value << 22u;
    auto set = cache.PrepareTranslated(compile);
    Check(set && g.sampler_info.magFilter == filters[value] &&
              g.sampler_info.minFilter == filters[value] &&
              g.sampler_info.anisotropyEnable == (value >= 2 ? VK_TRUE : VK_FALSE) &&
              g.sampler_info.maxAnisotropy == 16.0f,
        "sampler filter or anisotropic filter enum differs from Vulkan mapping");
    cache.Discard(set);
  }
  const VkSamplerMipmapMode mip_modes[] = {VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                            VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                            VK_SAMPLER_MIPMAP_MODE_LINEAR};
  for (std::uint32_t value = 0; value < std::size(mip_modes); ++value) {
    Reset();
    auto compile = SampledImageCompile();
    compile.resources.samplers[0].dwords[1] = 0x100u | (0x300u << 12u);
    compile.resources.samplers[0].dwords[2] = 0x3f00u | (value << 26u);
    auto set = cache.PrepareTranslated(compile);
    Check(set && g.sampler_info.mipmapMode == mip_modes[value] &&
              g.sampler_info.mipLodBias == -1.0f &&
              (value == 0 ? g.sampler_info.minLod == 0.0f && g.sampler_info.maxLod == 0.0f :
                            g.sampler_info.minLod == 1.0f && g.sampler_info.maxLod == 3.0f),
        "sampler mip filter did not preserve zero or ordered LOD limits and signed bias");
    cache.Discard(set);
  }
  for (std::uint32_t value = 0; value <= 4; ++value) {
    Reset();
    auto compile = SampledImageCompile();
    compile.resources.samplers[0].dwords[0] = value << 9u;
    auto set = cache.PrepareTranslated(compile);
    Check(set && g.sampler_info.maxAnisotropy == static_cast<float>(1u << value),
        "sampler anisotropy ratio enum did not map to its Vulkan maximum");
    cache.Discard(set);
  }

  Reset();
  auto depth = SampledImageCompile();
  depth.resources.samplers[0].dwords[0] = 6u << 12u;
  auto no_compare = cache.PrepareTranslated(depth);
  Check(no_compare && no_compare.samplers.size() == 1 &&
            g.sampler_info.compareEnable == VK_FALSE,
        "sampler comparison was enabled without a depth sampled pair");
  cache.Discard(no_compare);
  Reset();
  depth.program.info.images[0].depth_compare = true;
  depth.program.info.sampled_pairs.push_back({0, 0});
  auto compare = cache.PrepareTranslated(depth);
  Check(compare && g.sampler_info.compareEnable == VK_TRUE &&
            g.sampler_info.compareOp == VK_COMPARE_OP_GREATER_OR_EQUAL,
        "depth sampled pair did not enable the requested compare operation");
  cache.Discard(compare);

  Reset();
  auto float_border = SampledImageCompile();
  float_border.resources.samplers[0].dwords[0] =
      static_cast<std::uint32_t>(P::SamplerClampMode::kClampBorder);
  float_border.resources.samplers[0].dwords[3] =
      static_cast<std::uint32_t>(P::SamplerBorderColor::kOpaqueWhite) << 30u;
  float_border.program.info.sampled_pairs.push_back({0, 0});
  auto floats = cache.PrepareTranslated(float_border);
  Check(floats && g.sampler_info.borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        "noninteger sampled pair did not select a floating static border color");
  cache.Discard(floats);
  Reset();
  auto int_border = float_border;
  int_border.program.info.images[0].kind = ir::ResourceKind::ImageUint;
  int_border.program.bindings.descriptors[0].kind = ir::DescriptorBindingKind::SampledUint2D;
  int_border.resources.images[0].dwords[1] =
      P::GpuEnumValue(P::BufferFormat::k8_8_8_8UInt) << 20u;
  auto integers = cache.PrepareTranslated(int_border);
  Check(integers && g.sampler_info.borderColor == VK_BORDER_COLOR_INT_OPAQUE_WHITE,
        "integer sampled pair did not select an integer static border color");
  cache.Discard(integers);

  Reset();
  auto dedup = SampledImageCompile();
  dedup.resources.samplers.push_back(dedup.resources.samplers[0]);
  dedup.program.info.samplers.push_back(dedup.program.info.samplers[0]);
  dedup.program.bindings.descriptors[1].resources = {0, 1};
  auto shared = cache.PrepareTranslated(dedup);
  Check(shared && shared.samplers.size() == 1 && shared.sampler_descriptors.size() == 2 &&
            shared.sampler_descriptors[0].sampler == shared.sampler_descriptors[1].sampler,
        "identical translated sampler descriptors did not deduplicate their Vulkan sampler");
  cache.Discard(shared);

  const auto CheckRejectedSampler = [&](auto mutate, std::string_view message) {
    Reset();
    auto invalid = SampledImageCompile();
    mutate(invalid);
    const auto set = cache.PrepareTranslated(invalid);
    const auto sampler_created = std::count_if(g.issued.begin(), g.issued.end(),
        [](const FakeOperation& op) { return op.kind == "sampler"; });
    Check(!set && sampler_created == 0, message);
  };
  CheckRejectedSampler([](auto& c) { c.resources.samplers[0].dwords[3] =
                         static_cast<std::uint32_t>(P::SamplerBorderColor::kFromTable) << 30u; },
      "sampler border table was not rejected before sampler creation");
  CheckRejectedSampler([](auto& c) { c.resources.samplers[0].dwords[2] = 3u << 26u; },
      "invalid sampler mip enum was not rejected before sampler creation");
  CheckRejectedSampler([](auto& c) {
    c.resources.samplers[0].dwords[0] = 5u << 9u;
  }, "invalid sampler anisotropy enum was not rejected before sampler creation");
  CheckRejectedSampler([](auto& c) {
    c.resources.samplers[0].dwords[0] = 1u << 15u;
    c.resources.samplers[0].dwords[2] = 1u << 22u;
  }, "unnormalized sampler did not reject incompatible minification and magnification filters");
  CheckRejectedSampler([](auto& c) {
    c.resources.samplers[0].dwords[0] =
        static_cast<std::uint32_t>(P::SamplerClampMode::kClampBorder);
    c.program.info.sampled_pairs.push_back({0, 0});
    c.resources.images.push_back(c.resources.images[0]);
    c.resources.images[1].dwords[1] =
        P::GpuEnumValue(P::BufferFormat::k8_8_8_8UInt) << 20u;
    auto integer = c.program.info.images[0];
    integer.kind = ir::ResourceKind::ImageUint;
    c.program.info.images.push_back(integer);
    c.program.bindings.descriptors.push_back(
        {ir::DescriptorBindingKind::SampledUint2D, 4, {1}});
    c.program.info.sampled_pairs.push_back({1, 0});
  }, "border sampler did not reject mixed integer and noninteger sampled pairs");

  Reset(FailPoint::kCreateSampler);
  auto sampler_failure = cache.PrepareTranslated(SampledImageCompile());
  Check(!sampler_failure, "poisoned sampler creation unexpectedly succeeded");
  CheckExactCleanup({"unmap", "view", "view", "buffer", "image", "memory", "memory"});
}

void TestTranslatedFormatAliasesAndStorageCompatibility() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  Reset();
  auto context = Context();
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                          Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 0x1000, std::byte{0x40}),
        "format-alias guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());

  const struct { P::BufferFormat unorm; P::BufferFormat srgb; bool compressed; } pairs[] = {
      {P::BufferFormat::k8UNorm, P::BufferFormat::k8Srgb, false},
      {P::BufferFormat::k8_8UNorm, P::BufferFormat::k8_8Srgb, false},
      {P::BufferFormat::k8_8_8_8UNorm, P::BufferFormat::k8_8_8_8Srgb, false},
      {P::BufferFormat::kBc1UNorm, P::BufferFormat::kBc1Srgb, true},
      {P::BufferFormat::kBc3UNorm, P::BufferFormat::kBc3Srgb, true},
      {P::BufferFormat::kBc7UNorm, P::BufferFormat::kBc7Srgb, true},
  };
  for (const auto& pair : pairs) {
    const auto unorm = vk::MapGuestImageFormat(P::GpuEnumValue(pair.unorm));
    const auto srgb = vk::MapGuestImageFormat(P::GpuEnumValue(pair.srgb));
    Check(unorm && srgb, "format-alias test used an unmapped pair");
    for (bool srgb_first : {false, true}) {
      Reset();
      auto sampled = srgb_first ? FormatAliasCompile(pair.srgb, false, pair.unorm, false) :
          FormatAliasCompile(pair.unorm, false, pair.srgb, false);
      auto set = cache.PrepareTranslated(sampled);
      Check(set && set.images.size() == 1 && set.image_descriptors.size() == 2 &&
                CapturedViewFormat(set.image_descriptors[0].view) ==
                    (srgb_first ? srgb->format : unorm->format) &&
                CapturedViewFormat(set.image_descriptors[1].view) ==
                    (srgb_first ? unorm->format : srgb->format),
          "sampled exact-key aliases did not select each descriptor's mapped view format");
      cache.Discard(set);
    }
    if (pair.compressed) {
      for (bool srgb_first : {false, true}) {
        Reset();
        auto storage = srgb_first ? FormatAliasCompile(pair.srgb, true, pair.unorm, false) :
            FormatAliasCompile(pair.unorm, false, pair.srgb, true);
        Check(!cache.PrepareTranslated(storage) && g.issued.empty() && g.teardown.empty(),
            "compressed sRGB storage alias reached Vulkan work");
      }
      continue;
    }
    for (bool srgb_first : {false, true}) {
      Reset();
      auto storage = srgb_first ? FormatAliasCompile(pair.srgb, true, pair.unorm, false) :
          FormatAliasCompile(pair.unorm, false, pair.srgb, true);
      auto set = cache.PrepareTranslated(storage);
      Check(set && set.images.size() == 1 && set.image_descriptors.size() == 2 &&
                CapturedViewFormat(set.image_descriptors[0].view) == unorm->format &&
                CapturedViewFormat(set.image_descriptors[1].view) == unorm->format,
          "sRGB storage alias did not bind an UNORM Vulkan view in both dense orders");
      cache.Discard(set);
    }
  }

  Reset();
  auto incompatible_shape = FormatAliasCompile(P::BufferFormat::k8_8_8_8UNorm, false,
                                                P::BufferFormat::k8_8_8_8UNorm, false);
  incompatible_shape.resources.images[0].dwords[1] |= 3u << 30u;
  incompatible_shape.resources.images[0].dwords[2] = 0;             // 4 x 1 x 4 bytes.
  incompatible_shape.resources.images[1].dwords[1] |= 1u << 30u;
  incompatible_shape.resources.images[1].dwords[2] = 1u << 14u;     // 2 x 2 x 4 bytes.
  Check(!cache.PrepareTranslated(incompatible_shape) && g.issued.empty() && g.teardown.empty(),
        "equal-byte incompatible image shapes shared a Vulkan image lease");
}

void TestMappings() {
  const struct {
    P::BufferFormat unorm;
    P::BufferFormat srgb;
  } siblings[] = {
      {P::BufferFormat::k8UNorm, P::BufferFormat::k8Srgb},
      {P::BufferFormat::k8_8UNorm, P::BufferFormat::k8_8Srgb},
      {P::BufferFormat::k8_8_8_8UNorm, P::BufferFormat::k8_8_8_8Srgb},
      {P::BufferFormat::kBc1UNorm, P::BufferFormat::kBc1Srgb},
      {P::BufferFormat::kBc3UNorm, P::BufferFormat::kBc3Srgb},
      {P::BufferFormat::kBc7UNorm, P::BufferFormat::kBc7Srgb},
  };
  for (const auto& pair : siblings) {
    const auto unorm = vk::MapGuestImageFormat(P::GpuEnumValue(pair.unorm));
    const auto srgb = vk::MapGuestImageFormat(P::GpuEnumValue(pair.srgb));
    Check(unorm && srgb && unorm->sibling_format == srgb->format &&
              srgb->sibling_format == unorm->format,
        "mapped UNORM/sRGB format pair did not retain reciprocal sibling views");
  }
  Check(vk::MapGuestImageFormat(P::GpuEnumValue(P::BufferFormat::kBc6SFloat))->format == VK_FORMAT_BC6H_SFLOAT_BLOCK,
        "BC6 signed float mapping differs");
  const auto packed = vk::MapGuestImageFormat(
      P::GpuEnumValue(P::BufferFormat::k10_10_10_2UNorm));
  Check(packed && packed->format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
            packed->storage_class == vk::VulkanImageStorageClass::kA2B10G10R10 &&
            !packed->sibling_format,
        "packed 10:10:10:2 mapping differs");
  Check(!vk::MapGuestImageFormat(P::GpuEnumValue(P::BufferFormat::k32_32_32Float)),
        "unsupported three-component format was guessed");
}

void TestDepthStencilPreparation() {
  Reset();
  auto context = Context();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                         Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1200, 16, std::byte{0}),
        "depth image guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  vk::VulkanGuestImageCache cache(*context, memory, runtime.resource_coherence());
  auto depth = cache.PrepareDepthStencil({0x1200, P::DepthFormat::kZ32F,
      P::StencilFormat::kInvalid, 2, 2, 0, VK_SAMPLE_COUNT_1_BIT, true});
  Check(depth && depth.format.format == VK_FORMAT_D32_SFLOAT &&
            depth.aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT &&
            (g.image_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 &&
            depth.layout.total_bytes == 16,
        "Z32 depth target did not create a checked depth attachment lease");
  const auto command_buffer = Handle<VkCommandBuffer>(0x73);
  Check(cache.RecordUpload(command_buffer, depth,
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) &&
            cache.RecordReadback(command_buffer, depth,
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
        "depth target did not record checked upload/readback transitions");
  Check(g.upload_copies.size() == 1 && g.readback_copies.size() == 1 &&
            g.upload_copies[0].imageSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT &&
            g.readback_copies[0].imageSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT,
        "depth target transfer copies used a non-depth aspect");
  cache.Discard(depth);
  auto unsupported = cache.PrepareDepthStencil({0x1200, P::DepthFormat::kZ16,
      P::StencilFormat::kInvalid, 2, 2, 0, VK_SAMPLE_COUNT_1_BIT, true});
  Check(!unsupported && unsupported.status == vk::VulkanGuestImageStatus::kUnsupportedFormat,
        "unsupported depth format was accepted without device evidence");
}

void TestOptimalTilingFeatureQuery() {
  Reset();
  g.format_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                      VK_FORMAT_FEATURE_BLIT_SRC_BIT;
  auto context = Context();
  Check(context->SupportsOptimalTilingFeatures(
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
            context->SupportsColorAttachmentFormat(VK_FORMAT_R8G8B8A8_UNORM) &&
            !context->SupportsDepthStencilAttachmentFormat(VK_FORMAT_R8G8B8A8_UNORM) &&
            !context->SupportsOptimalTilingFeatures(
                VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_BLIT_DST_BIT),
        "optimal-tiling feature query did not preserve exact required bits");
}
}  // namespace

int main() {
  TestMappings();
  TestOptimalTilingFeatureQuery();
  TestDepthStencilPreparation();
  TestReadOnlyRenderTargetDetilePreparation();
  TestPreparationAndTopology();
  TestCoherentAndRollback();
  TestTransferLeaseAndCoherence();
  TestCoherentCompletion();
  TestMissingCommandFunctionsAreSideEffectFree();
  TestTranslatedImageSamplerSet();
  TestTranslatedDescriptorDeviceLimits();
  TestTranslatedAliasesAndInvalidInputs();
  TestTranslatedSamplerMatrixAndRollback();
  TestTranslatedFormatAliasesAndStorageCompatibility();
  std::cout << "vulkan_image_cache_test: all cases passed\n";
  return 0;
}
