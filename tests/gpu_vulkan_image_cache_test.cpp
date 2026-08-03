// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/vulkan/image_cache.h"
#include "gpu/vulkan/loader.h"

namespace {
namespace P = kajps5::gpu::Prospero;
namespace vk = kajps5::gpu::vulkan;

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
                       kAllocateStaging, kBindStaging, kMap };

struct FakeOperation {
  std::string_view kind;
  std::uintptr_t handle = 0;
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
  std::vector<std::byte> mapped_bytes;
  std::vector<FakeBarrier> barriers;
  std::vector<VkBufferImageCopy> upload_copies;
  std::vector<VkBufferImageCopy> readback_copies;
  std::vector<FakeCommandOperation> command_operations;
  std::vector<FakeOperation> issued;
  std::vector<FakeOperation> poisoned;
  std::vector<FakeOperation> teardown;
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
  properties->limits.maxPerStageDescriptorStorageBuffers = 16;
  properties->limits.maxDescriptorSetStorageBuffers = 16;
  properties->limits.maxPushConstantsSize = 128;
  properties->limits.nonCoherentAtomSize = 64;
  properties->limits.maxComputeWorkGroupCount[0] = 16;
  properties->limits.maxComputeWorkGroupCount[1] = 16;
  properties->limits.maxComputeWorkGroupCount[2] = 16;
  std::strncpy(properties->deviceName, "injected image cache", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}
VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceProperties2(VkPhysicalDevice,
    VkPhysicalDeviceProperties2* properties) { properties->properties = {}; }
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
  ++g.views; g.view_type = info->viewType;
  if ((g.fail == FailPoint::kCreateFirstView && g.views == 1) ||
      (g.fail == FailPoint::kCreateSecondView && g.views == 2)) { *view = Handle<VkImageView>(0xdead); Poison("view", HandleId(*view)); return VK_ERROR_OUT_OF_HOST_MEMORY; }
  *view = Handle<VkImageView>(g.next_handle++); Issue("view", HandleId(*view)); return VK_SUCCESS;
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
  if (g.commands_available && std::strcmp(name, "vkCmdPipelineBarrier") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdPipelineBarrier);
  if (g.commands_available && std::strcmp(name, "vkCmdCopyBufferToImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdCopyBufferToImage);
  if (g.commands_available && std::strcmp(name, "vkCmdCopyImageToBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdCopyImageToBuffer);
  return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetInstanceProcAddr(VkInstance, const char* name) {
  if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumerateInstanceVersion);
  if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateInstance);
  if (std::strcmp(name, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyInstance);
  if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumeratePhysicalDevices);
  if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceProperties);
  if (std::strcmp(name, "vkGetPhysicalDeviceProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceProperties2);
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
      {P::ImageType::kColor3D, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D},
      {P::ImageType::kCube, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE},
      {P::ImageType::kColor2DArray, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY},
  };
  for (const auto& test : cases) {
    Reset();
    auto request = Request(test.type);
    if (test.type == P::ImageType::kColor1D) request.input.height = 1;
    if (test.type == P::ImageType::kColor3D) request.input.depth = 2;
    if (test.type == P::ImageType::kCube) { request.input.width = 2; request.input.height = 2; request.input.layers = 1; }
    if (test.type == P::ImageType::kColor2DArray) request.input.layers = 2;
    auto next = cache.Prepare(request);
    Check(static_cast<bool>(next), "topology preparation failed");
    Check(g.image_type == test.image && g.view_type == test.view,
          "image type did not map to expected Vulkan topology");
    cache.Discard(next);
  }
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

void TestMappings() {
  Check(vk::MapGuestImageFormat(P::GpuEnumValue(P::BufferFormat::kBc6SFloat))->format == VK_FORMAT_BC6H_SFLOAT_BLOCK,
        "BC6 signed float mapping differs");
  Check(!vk::MapGuestImageFormat(P::GpuEnumValue(P::BufferFormat::k32_32_32Float)),
        "unsupported three-component format was guessed");
}
}  // namespace

int main() {
  TestMappings();
  TestPreparationAndTopology();
  TestCoherentAndRollback();
  TestTransferLeaseAndCoherence();
  TestCoherentCompletion();
  TestMissingCommandFunctionsAreSideEffectFree();
  std::cout << "vulkan_image_cache_test: all cases passed\n";
  return 0;
}
