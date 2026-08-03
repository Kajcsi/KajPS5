// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/{commandScheduler,masterSemaphore,render}.*
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/vulkan/execution.h"

namespace {

namespace vk = kajps5::gpu::vulkan;

constexpr std::array<std::uint32_t, 5> kValidatedSpirv = {
    0x07230203U, 0x00010600U, 0x0008000bU, 0x00000001U, 0x00000000U};

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_vulkan_execution_test: " << message << '\n';
    std::exit(1);
  }
}

bool HasDiagnostic(const vk::VulkanComputeResult& result,
                   vk::VulkanComputeDiagnosticCode code,
                   vk::VulkanDiagnosticSeverity severity) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const vk::VulkanComputeDiagnostic& diagnostic) {
                       return diagnostic.code == code &&
                              diagnostic.severity == severity;
                     });
}

enum class FailurePoint {
  kNone,
  kCreateInstance,
  kCreateDevice,
  kCreateCommandPool,
  kAllocateCommandBuffers,
  kCreateShaderModule,
  kCreatePipelineLayout,
  kCreateComputePipelines,
  kCreateFence,
  kBeginCommandBuffer,
  kEndCommandBuffer,
  kQueueSubmit,
  kCreateBuffer,
  kAllocateMemory,
  kBindBufferMemory,
  kMapMemory,
  kFlushMappedMemoryRanges,
  kCreateDescriptorSetLayout,
  kCreateDescriptorPool,
  kAllocateDescriptorSets,
  kInvalidateMappedMemoryRanges,
};

struct FenceRecord {
  VkFence fence = VK_NULL_HANDLE;
  VkResult status = VK_NOT_READY;
};

struct FakeMemoryRecord {
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize allocation_size = 0;
  std::vector<std::byte> bytes;
  bool mapped = false;
  bool freed = false;
};

enum class CommandEvent {
  kDispatch,
  kPipelineBarrier,
  kEndCommandBuffer,
};

struct FakeVulkanState {
  std::uint32_t next_handle = 0x1000;
  FailurePoint failure = FailurePoint::kNone;
  const char* missing_device_function = nullptr;
  VkResult queue_submit_result = VK_SUCCESS;
  VkResult device_wait_idle_result = VK_SUCCESS;
  bool poison_outputs_on_failure = false;
  bool require_wait_idle_before_retained_child_destroy = false;
  std::vector<VkResult> wait_results;
  std::size_t next_wait_result = 0;
  std::vector<VkFence> submitted_fences;
  std::vector<VkFence> waited_fences;
  std::vector<std::uint64_t> wait_timeouts;
  std::vector<FenceRecord> fences;
  std::uint32_t command_pools_created = 0;
  std::uint32_t command_pools_destroyed = 0;
  std::uint32_t shader_modules_created = 0;
  std::uint32_t shader_modules_destroyed = 0;
  std::uint32_t pipeline_layouts_created = 0;
  std::uint32_t pipeline_layouts_destroyed = 0;
  std::uint32_t pipelines_created = 0;
  std::uint32_t pipelines_destroyed = 0;
  std::uint32_t fences_created = 0;
  std::uint32_t fences_destroyed = 0;
  std::uint32_t queue_submit_calls = 0;
  std::uint32_t bind_pipeline_calls = 0;
  std::uint32_t dispatch_calls = 0;
  std::array<std::uint32_t, 3> dispatch_groups = {};
  std::vector<std::array<std::uint32_t, 3>> dispatches;
  std::vector<CommandEvent> command_events;
  std::uint32_t pipeline_barrier_calls = 0;
  VkPipelineStageFlags barrier_src_stage_mask = 0;
  VkPipelineStageFlags barrier_dst_stage_mask = 0;
  VkAccessFlags barrier_src_access_mask = 0;
  VkAccessFlags barrier_dst_access_mask = 0;
  bool saw_primary_command_buffer = false;
  bool saw_empty_pipeline_layout = false;
  std::uint32_t device_wait_idle_calls = 0;
  std::uint32_t devices_created = 0;
  std::uint32_t devices_destroyed = 0;
  std::uint32_t instances_created = 0;
  std::uint32_t instances_destroyed = 0;
  std::vector<FakeMemoryRecord> memories;
  std::unordered_map<std::uintptr_t, VkDeviceMemory> buffer_memories;
  std::vector<VkDescriptorBufferInfo> descriptor_infos;
  std::vector<std::vector<VkDescriptorBufferInfo>> descriptor_history;
  std::vector<std::uint32_t> pushed_words;
  std::uint32_t descriptor_binding = 0;
  std::uint32_t flush_calls = 0;
  std::uint32_t invalidate_calls = 0;
  VkMappedMemoryRange flush_range{};
  VkMappedMemoryRange invalidate_range{};
  std::uint32_t buffers_created = 0, buffers_destroyed = 0;
  std::uint32_t memories_allocated = 0, memories_freed = 0, unmap_calls = 0;
  std::uint32_t descriptor_layouts_created = 0,
                descriptor_layouts_destroyed = 0;
  std::uint32_t descriptor_pools_created = 0, descriptor_pools_destroyed = 0;
};

FakeVulkanState g_fake;
std::mutex g_create_pause_mutex;
std::condition_variable g_create_pause_condition;
bool g_pause_execution_create = false;
bool g_execution_create_paused = false;

void PauseExecutionCreateAtOwnerAcquisition() {
  std::unique_lock lock(g_create_pause_mutex);
  g_execution_create_paused = true;
  g_create_pause_condition.notify_all();
  g_create_pause_condition.wait(lock,
                                [] { return !g_pause_execution_create; });
}

template <typename Handle>
Handle MakeHandle() {
  return reinterpret_cast<Handle>(
      static_cast<std::uintptr_t>(g_fake.next_handle++));
}

template <typename Handle>
Handle FailureOutputHandle() {
  return g_fake.poison_outputs_on_failure ? MakeHandle<Handle>() : VK_NULL_HANDLE;
}

VkInstance FakeInstance() { return MakeHandle<VkInstance>(); }
VkPhysicalDevice FakePhysicalDevice() { return MakeHandle<VkPhysicalDevice>(); }
VkDevice FakeDevice() { return MakeHandle<VkDevice>(); }
VkQueue FakeQueue() { return MakeHandle<VkQueue>(); }

FenceRecord* FindFence(VkFence fence) {
  const auto found = std::find_if(
      g_fake.fences.begin(), g_fake.fences.end(),
      [&](const FenceRecord& record) { return record.fence == fence; });
  return found == g_fake.fences.end() ? nullptr : &*found;
}

void SignalFence(VkFence fence) {
  FenceRecord* record = FindFence(fence);
  Check(record != nullptr, "test attempted to signal an unknown Vulkan fence");
  record->status = VK_SUCCESS;
}

bool ShouldFail(FailurePoint point) {
  return g_fake.failure == point;
}

FakeMemoryRecord *FindMemory(VkDeviceMemory memory) {
  const auto found = std::find_if(
      g_fake.memories.begin(), g_fake.memories.end(),
      [&](const FakeMemoryRecord &record) { return record.memory == memory; });
  return found == g_fake.memories.end() ? nullptr : &*found;
}

std::vector<std::byte> &BytesForBuffer(VkBuffer buffer) {
  const auto found =
      g_fake.buffer_memories.find(reinterpret_cast<std::uintptr_t>(buffer));
  Check(found != g_fake.buffer_memories.end(),
        "fake buffer has no bound memory");
  FakeMemoryRecord *record = FindMemory(found->second);
  Check(record != nullptr && !record->freed,
        "fake buffer references missing memory");
  return record->bytes;
}

void CheckRetainedChildTeardownOrdering() {
  if (!g_fake.require_wait_idle_before_retained_child_destroy) {
    return;
  }
  Check(g_fake.device_wait_idle_calls != 0,
        "retained Vulkan child was destroyed before vkDeviceWaitIdle");
}

VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerateInstanceVersion(
    std::uint32_t* version) {
  *version = VK_API_VERSION_1_3;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateInstance(
    const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance) {
  if (ShouldFail(FailurePoint::kCreateInstance)) {
    *instance = FailureOutputHandle<VkInstance>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *instance = FakeInstance();
  ++g_fake.instances_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyInstance(VkInstance,
                                                 const VkAllocationCallbacks*) {
  ++g_fake.instances_destroyed;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeEnumeratePhysicalDevices(
    VkInstance, std::uint32_t* count, VkPhysicalDevice* devices) {
  if (devices == nullptr) {
    *count = 1;
    return VK_SUCCESS;
  }
  if (*count == 0) {
    return VK_INCOMPLETE;
  }
  devices[0] = FakePhysicalDevice();
  *count = 1;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceProperties(
    VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
  *properties = {};
  properties->apiVersion = VK_API_VERSION_1_3;
  properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  properties->limits.minStorageBufferOffsetAlignment = 256;
  properties->limits.maxStorageBufferRange = 4096;
  properties->limits.maxPerStageDescriptorStorageBuffers = 32;
  properties->limits.maxDescriptorSetStorageBuffers = 32;
  properties->limits.maxPushConstantsSize = 128;
  properties->limits.nonCoherentAtomSize = 64;
  properties->limits.maxComputeWorkGroupCount[0] = 7;
  properties->limits.maxComputeWorkGroupCount[1] = 11;
  properties->limits.maxComputeWorkGroupCount[2] = 13;
  std::strncpy(properties->deviceName, "KajPS5 injected Vulkan device",
               VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}

VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *properties) {
  *properties = {};
  properties->memoryTypeCount = 1;
  properties->memoryTypes[0].propertyFlags =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  properties->memoryHeaps[0].size = 4096;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateBuffer(VkDevice,
                                                const VkBufferCreateInfo *,
                                                const VkAllocationCallbacks *,
                                                VkBuffer *buffer) {
  if (ShouldFail(FailurePoint::kCreateBuffer)) {
    *buffer = FailureOutputHandle<VkBuffer>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *buffer = MakeHandle<VkBuffer>();
  ++g_fake.buffers_created;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyBuffer(VkDevice, VkBuffer,
                                             const VkAllocationCallbacks *) {
  ++g_fake.buffers_destroyed;
}
VKAPI_ATTR void VKAPI_CALL
FakeGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements *r) {
  *r = {};
  r->size = 4096;
  r->alignment = 256;
  r->memoryTypeBits = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL
FakeAllocateMemory(VkDevice, const VkMemoryAllocateInfo *info,
                   const VkAllocationCallbacks *, VkDeviceMemory *memory) {
  if (ShouldFail(FailurePoint::kAllocateMemory)) {
    *memory = FailureOutputHandle<VkDeviceMemory>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *memory = MakeHandle<VkDeviceMemory>();
  g_fake.memories.push_back(
      {*memory, info->allocationSize,
       std::vector<std::byte>(static_cast<std::size_t>(info->allocationSize))});
  ++g_fake.memories_allocated;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeFreeMemory(VkDevice, VkDeviceMemory memory,
                                          const VkAllocationCallbacks *) {
  FakeMemoryRecord *record = FindMemory(memory);
  Check(record != nullptr && !record->freed && !record->mapped,
        "fake memory freed out of order");
  record->freed = true;
  ++g_fake.memories_freed;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeBindBufferMemory(VkDevice, VkBuffer buffer,
                                                    VkDeviceMemory memory,
                                                    VkDeviceSize) {
  if (ShouldFail(FailurePoint::kBindBufferMemory))
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  Check(FindMemory(memory) != nullptr, "fake bind used unknown memory");
  g_fake.buffer_memories[reinterpret_cast<std::uintptr_t>(buffer)] = memory;
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeMapMemory(VkDevice, VkDeviceMemory memory,
                                             VkDeviceSize offset,
                                             VkDeviceSize size,
                                             VkMemoryMapFlags, void **p) {
  if (ShouldFail(FailurePoint::kMapMemory)) {
    *p = reinterpret_cast<void *>(1);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  FakeMemoryRecord *record = FindMemory(memory);
  Check(record != nullptr && !record->freed && !record->mapped &&
            offset <= record->allocation_size &&
            size <= record->allocation_size - offset,
        "fake map used an invalid memory range");
  record->mapped = true;
  *p = record->bytes.data() + offset;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeUnmapMemory(VkDevice, VkDeviceMemory memory) {
  FakeMemoryRecord *record = FindMemory(memory);
  Check(record != nullptr && record->mapped && !record->freed,
        "fake unmap ordering invalid");
  record->mapped = false;
  ++g_fake.unmap_calls;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeFlush(VkDevice, std::uint32_t,
                                         const VkMappedMemoryRange *range) {
  ++g_fake.flush_calls;
  g_fake.flush_range = *range;
  return ShouldFail(FailurePoint::kFlushMappedMemoryRanges)
             ? VK_ERROR_OUT_OF_HOST_MEMORY
             : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
FakeInvalidate(VkDevice, std::uint32_t, const VkMappedMemoryRange *range) {
  ++g_fake.invalidate_calls;
  g_fake.invalidate_range = *range;
  return ShouldFail(FailurePoint::kInvalidateMappedMemoryRanges)
             ? VK_ERROR_OUT_OF_HOST_MEMORY
             : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL
FakeCreateDsl(VkDevice, const VkDescriptorSetLayoutCreateInfo *,
              const VkAllocationCallbacks *, VkDescriptorSetLayout *h) {
  if (ShouldFail(FailurePoint::kCreateDescriptorSetLayout)) {
    *h = FailureOutputHandle<VkDescriptorSetLayout>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *h = MakeHandle<VkDescriptorSetLayout>();
  ++g_fake.descriptor_layouts_created;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyDsl(VkDevice, VkDescriptorSetLayout,
                                          const VkAllocationCallbacks *) {
  ++g_fake.descriptor_layouts_destroyed;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDp(VkDevice,
                                            const VkDescriptorPoolCreateInfo *,
                                            const VkAllocationCallbacks *,
                                            VkDescriptorPool *h) {
  if (ShouldFail(FailurePoint::kCreateDescriptorPool)) {
    *h = FailureOutputHandle<VkDescriptorPool>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *h = MakeHandle<VkDescriptorPool>();
  ++g_fake.descriptor_pools_created;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeDestroyDp(VkDevice, VkDescriptorPool,
                                         const VkAllocationCallbacks *) {
  ++g_fake.descriptor_pools_destroyed;
}
VKAPI_ATTR VkResult VKAPI_CALL FakeAllocateDs(
    VkDevice, const VkDescriptorSetAllocateInfo *, VkDescriptorSet *h) {
  if (ShouldFail(FailurePoint::kAllocateDescriptorSets)) {
    *h = FailureOutputHandle<VkDescriptorSet>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *h = MakeHandle<VkDescriptorSet>();
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FakeUpdateDs(VkDevice, std::uint32_t,
                                        const VkWriteDescriptorSet *w,
                                        std::uint32_t,
                                        const VkCopyDescriptorSet *) {
  g_fake.descriptor_binding = w->dstBinding;
  g_fake.descriptor_infos.assign(w->pBufferInfo,
                                 w->pBufferInfo + w->descriptorCount);
  g_fake.descriptor_history.push_back(g_fake.descriptor_infos);
}
VKAPI_ATTR void VKAPI_CALL FakeCmdBindDs(VkCommandBuffer, VkPipelineBindPoint,
                                         VkPipelineLayout, std::uint32_t,
                                         std::uint32_t, const VkDescriptorSet *,
                                         std::uint32_t, const std::uint32_t *) {
}
VKAPI_ATTR void VKAPI_CALL FakeCmdPush(VkCommandBuffer, VkPipelineLayout,
                                       VkShaderStageFlags, std::uint32_t,
                                       std::uint32_t size, const void *data) {
  auto *p = static_cast<const std::uint32_t *>(data);
  g_fake.pushed_words.assign(p, p + size / 4);
}

VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceProperties2(
    VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
  auto* identifiers =
      static_cast<VkPhysicalDeviceIDProperties*>(properties->pNext);
  if (identifiers == nullptr) {
    return;
  }
  for (std::uint32_t index = 0; index < VK_UUID_SIZE; ++index) {
    identifiers->deviceUUID[index] = static_cast<std::uint8_t>(index + 1U);
  }
}

VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceFeatures2(
    VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
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
  features->features.vertexPipelineStoresAndAtomics = VK_TRUE;
  features->features.shaderInt64 = VK_TRUE;
  features->features.textureCompressionBC = VK_TRUE;
  auto* features13 =
      static_cast<VkPhysicalDeviceVulkan13Features*>(features->pNext);
  auto* features12 =
      static_cast<VkPhysicalDeviceVulkan12Features*>(features13->pNext);
  features13->dynamicRendering = VK_TRUE;
  features13->synchronization2 = VK_TRUE;
  features13->robustImageAccess = VK_TRUE;
  features12->timelineSemaphore = VK_TRUE;
  features12->samplerMirrorClampToEdge = VK_TRUE;
}

VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* properties) {
  if (properties == nullptr) {
    *count = 1;
    return;
  }
  if (*count != 0) {
    properties[0] = {};
    properties[0].queueCount = 1;
    properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    *count = 1;
  }
}

VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerateDeviceExtensionProperties(
    VkPhysicalDevice, const char*, std::uint32_t* count,
    VkExtensionProperties*) {
  *count = 0;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDevice(
    VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*,
    VkDevice* device) {
  if (ShouldFail(FailurePoint::kCreateDevice)) {
    *device = FailureOutputHandle<VkDevice>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *device = FakeDevice();
  ++g_fake.devices_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyDevice(VkDevice,
                                               const VkAllocationCallbacks*) {
  Check(g_fake.command_pools_created == g_fake.command_pools_destroyed &&
            g_fake.shader_modules_created == g_fake.shader_modules_destroyed &&
            g_fake.pipeline_layouts_created ==
                g_fake.pipeline_layouts_destroyed &&
            g_fake.pipelines_created == g_fake.pipelines_destroyed &&
            g_fake.fences_created == g_fake.fences_destroyed &&
            g_fake.fences.empty(),
        "Vulkan device was destroyed while compute children were still owned");
  ++g_fake.devices_destroyed;
}

VKAPI_ATTR void VKAPI_CALL FakeGetDeviceQueue(VkDevice, std::uint32_t,
                                               std::uint32_t, VkQueue* queue) {
  *queue = FakeQueue();
}

VKAPI_ATTR VkResult VKAPI_CALL FakeDeviceWaitIdle(VkDevice) {
  ++g_fake.device_wait_idle_calls;
  return g_fake.device_wait_idle_result;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateCommandPool(
    VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*,
    VkCommandPool* command_pool) {
  if (ShouldFail(FailurePoint::kCreateCommandPool)) {
    *command_pool = FailureOutputHandle<VkCommandPool>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *command_pool = MakeHandle<VkCommandPool>();
  ++g_fake.command_pools_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyCommandPool(
    VkDevice, VkCommandPool, const VkAllocationCallbacks*) {
  CheckRetainedChildTeardownOrdering();
  ++g_fake.command_pools_destroyed;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeAllocateCommandBuffers(
    VkDevice, const VkCommandBufferAllocateInfo* info,
    VkCommandBuffer* command_buffer) {
  g_fake.saw_primary_command_buffer =
      info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
      info->commandBufferCount == 1;
  if (ShouldFail(FailurePoint::kAllocateCommandBuffers)) {
    *command_buffer = FailureOutputHandle<VkCommandBuffer>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *command_buffer = MakeHandle<VkCommandBuffer>();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeBeginCommandBuffer(
    VkCommandBuffer, const VkCommandBufferBeginInfo*) {
  return ShouldFail(FailurePoint::kBeginCommandBuffer)
             ? VK_ERROR_OUT_OF_HOST_MEMORY
             : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeEndCommandBuffer(VkCommandBuffer) {
  g_fake.command_events.push_back(CommandEvent::kEndCommandBuffer);
  return ShouldFail(FailurePoint::kEndCommandBuffer) ? VK_ERROR_OUT_OF_HOST_MEMORY
                                                       : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateShaderModule(
    VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*,
    VkShaderModule* shader_module) {
  if (ShouldFail(FailurePoint::kCreateShaderModule)) {
    *shader_module = FailureOutputHandle<VkShaderModule>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *shader_module = MakeHandle<VkShaderModule>();
  ++g_fake.shader_modules_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyShaderModule(
    VkDevice, VkShaderModule, const VkAllocationCallbacks*) {
  CheckRetainedChildTeardownOrdering();
  ++g_fake.shader_modules_destroyed;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreatePipelineLayout(
    VkDevice, const VkPipelineLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkPipelineLayout* pipeline_layout) {
  g_fake.saw_empty_pipeline_layout =
      info->setLayoutCount == 0 && info->pushConstantRangeCount == 0;
  if (ShouldFail(FailurePoint::kCreatePipelineLayout)) {
    *pipeline_layout = FailureOutputHandle<VkPipelineLayout>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *pipeline_layout = MakeHandle<VkPipelineLayout>();
  ++g_fake.pipeline_layouts_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyPipelineLayout(
    VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) {
  CheckRetainedChildTeardownOrdering();
  ++g_fake.pipeline_layouts_destroyed;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateComputePipelines(
    VkDevice, VkPipelineCache, std::uint32_t, const VkComputePipelineCreateInfo*,
    const VkAllocationCallbacks*, VkPipeline* pipeline) {
  if (ShouldFail(FailurePoint::kCreateComputePipelines)) {
    *pipeline = FailureOutputHandle<VkPipeline>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *pipeline = MakeHandle<VkPipeline>();
  ++g_fake.pipelines_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyPipeline(
    VkDevice, VkPipeline, const VkAllocationCallbacks*) {
  CheckRetainedChildTeardownOrdering();
  ++g_fake.pipelines_destroyed;
}

VKAPI_ATTR void VKAPI_CALL FakeCmdBindPipeline(VkCommandBuffer,
                                                VkPipelineBindPoint bind_point,
                                                VkPipeline) {
  Check(bind_point == VK_PIPELINE_BIND_POINT_COMPUTE,
        "execution bound a non-compute pipeline");
  ++g_fake.bind_pipeline_calls;
}

VKAPI_ATTR void VKAPI_CALL FakeCmdDispatch(VkCommandBuffer,
                                            std::uint32_t group_count_x,
                                            std::uint32_t group_count_y,
                                            std::uint32_t group_count_z) {
  ++g_fake.dispatch_calls;
  g_fake.dispatch_groups = {group_count_x, group_count_y, group_count_z};
  g_fake.dispatches.push_back(g_fake.dispatch_groups);
  g_fake.command_events.push_back(CommandEvent::kDispatch);
}

VKAPI_ATTR void VKAPI_CALL FakeCmdPipelineBarrier(
    VkCommandBuffer, VkPipelineStageFlags src_stage_mask,
    VkPipelineStageFlags dst_stage_mask, VkDependencyFlags,
    std::uint32_t memory_barrier_count, const VkMemoryBarrier *memory_barriers,
    std::uint32_t, const VkBufferMemoryBarrier *, std::uint32_t,
    const VkImageMemoryBarrier *) {
  Check(memory_barrier_count == 1 && memory_barriers != nullptr,
        "execution recorded an invalid memory barrier");
  ++g_fake.pipeline_barrier_calls;
  g_fake.barrier_src_stage_mask = src_stage_mask;
  g_fake.barrier_dst_stage_mask = dst_stage_mask;
  g_fake.barrier_src_access_mask = memory_barriers[0].srcAccessMask;
  g_fake.barrier_dst_access_mask = memory_barriers[0].dstAccessMask;
  g_fake.command_events.push_back(CommandEvent::kPipelineBarrier);
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateFence(
    VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* fence) {
  if (ShouldFail(FailurePoint::kCreateFence)) {
    *fence = FailureOutputHandle<VkFence>();
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *fence = MakeHandle<VkFence>();
  g_fake.fences.push_back({*fence, VK_NOT_READY});
  ++g_fake.fences_created;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyFence(VkDevice, VkFence fence,
                                            const VkAllocationCallbacks*) {
  CheckRetainedChildTeardownOrdering();
  const auto found = std::find_if(
      g_fake.fences.begin(), g_fake.fences.end(),
      [&](const FenceRecord& record) { return record.fence == fence; });
  Check(found != g_fake.fences.end(), "execution destroyed an unknown fence");
  g_fake.fences.erase(found);
  ++g_fake.fences_destroyed;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeWaitForFences(
    VkDevice, std::uint32_t fence_count, const VkFence* fences, VkBool32,
    std::uint64_t timeout_ns) {
  Check(fence_count == 1, "execution waited on an unexpected fence count");
  g_fake.waited_fences.push_back(fences[0]);
  g_fake.wait_timeouts.push_back(timeout_ns);
  const VkResult result =
      g_fake.next_wait_result < g_fake.wait_results.size()
          ? g_fake.wait_results[g_fake.next_wait_result++]
          : VK_SUCCESS;
  if (result == VK_SUCCESS) {
    SignalFence(fences[0]);
  }
  return result;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeGetFenceStatus(VkDevice, VkFence fence) {
  const FenceRecord* record = FindFence(fence);
  return record == nullptr ? VK_ERROR_UNKNOWN : record->status;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeQueueSubmit(VkQueue, std::uint32_t submit_count,
                                                const VkSubmitInfo*, VkFence fence) {
  Check(submit_count == 1, "execution submitted an unexpected batch count");
  ++g_fake.queue_submit_calls;
  if (g_fake.queue_submit_result != VK_SUCCESS) {
    return g_fake.queue_submit_result;
  }
  if (ShouldFail(FailurePoint::kQueueSubmit)) {
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  g_fake.submitted_fences.push_back(fence);
  return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetDeviceProcAddr(VkDevice,
                                                                 const char* name) {
  if (std::strcmp(name, "vkCmdDispatch") == 0) {
    std::unique_lock lock(g_create_pause_mutex);
    if (g_pause_execution_create) {
      lock.unlock();
      PauseExecutionCreateAtOwnerAcquisition();
    }
  }
  if (g_fake.missing_device_function != nullptr &&
      std::strcmp(name, g_fake.missing_device_function) == 0) {
    return nullptr;
  }
  if (std::strcmp(name, "vkDestroyDevice") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDevice);
  }
  if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetDeviceQueue);
  }
  if (std::strcmp(name, "vkDeviceWaitIdle") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDeviceWaitIdle);
  }
  if (std::strcmp(name, "vkCreateCommandPool") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateCommandPool);
  }
  if (std::strcmp(name, "vkDestroyCommandPool") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyCommandPool);
  }
  if (std::strcmp(name, "vkAllocateCommandBuffers") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeAllocateCommandBuffers);
  }
  if (std::strcmp(name, "vkBeginCommandBuffer") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeBeginCommandBuffer);
  }
  if (std::strcmp(name, "vkEndCommandBuffer") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeEndCommandBuffer);
  }
  if (std::strcmp(name, "vkCreateShaderModule") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateShaderModule);
  }
  if (std::strcmp(name, "vkDestroyShaderModule") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyShaderModule);
  }
  if (std::strcmp(name, "vkCreatePipelineLayout") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreatePipelineLayout);
  }
  if (std::strcmp(name, "vkDestroyPipelineLayout") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyPipelineLayout);
  }
  if (std::strcmp(name, "vkCreateComputePipelines") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateComputePipelines);
  }
  if (std::strcmp(name, "vkDestroyPipeline") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyPipeline);
  }
  if (std::strcmp(name, "vkCmdBindPipeline") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdBindPipeline);
  }
  if (std::strcmp(name, "vkCmdDispatch") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdDispatch);
  }
  if (std::strcmp(name, "vkCmdPipelineBarrier") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdPipelineBarrier);
  }
  if (std::strcmp(name, "vkCreateFence") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateFence);
  }
  if (std::strcmp(name, "vkDestroyFence") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyFence);
  }
  if (std::strcmp(name, "vkWaitForFences") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeWaitForFences);
  }
  if (std::strcmp(name, "vkGetFenceStatus") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetFenceStatus);
  }
  if (std::strcmp(name, "vkQueueSubmit") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeQueueSubmit);
  }
  if (std::strcmp(name, "vkCreateBuffer") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateBuffer);
  if (std::strcmp(name, "vkDestroyBuffer") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyBuffer);
  if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(
        FakeGetBufferMemoryRequirements);
  if (std::strcmp(name, "vkAllocateMemory") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeAllocateMemory);
  if (std::strcmp(name, "vkFreeMemory") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeFreeMemory);
  if (std::strcmp(name, "vkBindBufferMemory") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeBindBufferMemory);
  if (std::strcmp(name, "vkMapMemory") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeMapMemory);
  if (std::strcmp(name, "vkUnmapMemory") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeUnmapMemory);
  if (std::strcmp(name, "vkFlushMappedMemoryRanges") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeFlush);
  if (std::strcmp(name, "vkInvalidateMappedMemoryRanges") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeInvalidate);
  if (std::strcmp(name, "vkCreateDescriptorSetLayout") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateDsl);
  if (std::strcmp(name, "vkDestroyDescriptorSetLayout") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDsl);
  if (std::strcmp(name, "vkCreateDescriptorPool") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateDp);
  if (std::strcmp(name, "vkDestroyDescriptorPool") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDp);
  if (std::strcmp(name, "vkAllocateDescriptorSets") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeAllocateDs);
  if (std::strcmp(name, "vkUpdateDescriptorSets") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeUpdateDs);
  if (std::strcmp(name, "vkCmdBindDescriptorSets") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdBindDs);
  if (std::strcmp(name, "vkCmdPushConstants") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCmdPush);
  return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetInstanceProcAddr(
    VkInstance, const char* name) {
  if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumerateInstanceVersion);
  }
  if (std::strcmp(name, "vkCreateInstance") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateInstance);
  }
  if (std::strcmp(name, "vkDestroyInstance") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyInstance);
  }
  if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeEnumeratePhysicalDevices);
  }
  if (std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceProperties);
  }
  if (std::strcmp(name, "vkGetPhysicalDeviceProperties2") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(
        FakeGetPhysicalDeviceProperties2);
  }
  if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(
        FakeGetPhysicalDeviceMemoryProperties);
  if (std::strcmp(name, "vkGetPhysicalDeviceFeatures2") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceFeatures2);
  }
  if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(
        FakeGetPhysicalDeviceQueueFamilyProperties);
  }
  if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(
        FakeEnumerateDeviceExtensionProperties);
  }
  if (std::strcmp(name, "vkCreateDevice") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeCreateDevice);
  }
  if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetDeviceProcAddr);
  }
  if (std::strcmp(name, "vkDestroyDevice") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDevice);
  }
  return nullptr;
}

vk::VulkanLoader FakeLoader() {
  return vk::VulkanLoader::FromGetInstanceProcAddr(FakeGetInstanceProcAddr);
}

vk::VulkanContextCreateResult CreateContext() {
  auto context = vk::VulkanDeviceContext::Create(FakeLoader());
  Check(static_cast<bool>(context),
        "injected Vulkan device context was not created");
  return context;
}

std::unique_ptr<vk::VulkanComputeExecution> CreateExecution(
    vk::VulkanDeviceContext& context) {
  auto execution = vk::VulkanComputeExecution::Create(context);
  Check(static_cast<bool>(execution),
        "injected Vulkan compute execution was not created");
  return std::move(execution.execution);
}

void CheckNoLiveExecutionResources(std::string_view message) {
  Check(g_fake.command_pools_created == g_fake.command_pools_destroyed &&
            g_fake.shader_modules_created == g_fake.shader_modules_destroyed &&
            g_fake.pipeline_layouts_created ==
                g_fake.pipeline_layouts_destroyed &&
            g_fake.pipelines_created == g_fake.pipelines_destroyed &&
            g_fake.fences_created == g_fake.fences_destroyed &&
            g_fake.fences.empty(),
        message);
}

void TestInjectedSuccessAndTransactionalFailures() {
  struct FailureCase {
    FailurePoint point;
    vk::VulkanComputeStatus status;
    vk::VulkanComputeDiagnosticCode diagnostic;
  };
  const std::array failures = {
      FailureCase{FailurePoint::kCreateCommandPool,
                  vk::VulkanComputeStatus::kCommandPoolCreationFailed,
                  vk::VulkanComputeDiagnosticCode::kCommandPoolCreationFailed},
      FailureCase{FailurePoint::kAllocateCommandBuffers,
                  vk::VulkanComputeStatus::kCommandBufferAllocationFailed,
                  vk::VulkanComputeDiagnosticCode::kCommandBufferAllocationFailed},
      FailureCase{FailurePoint::kCreateShaderModule,
                  vk::VulkanComputeStatus::kShaderModuleCreationFailed,
                  vk::VulkanComputeDiagnosticCode::kShaderModuleCreationFailed},
      FailureCase{FailurePoint::kCreatePipelineLayout,
                  vk::VulkanComputeStatus::kPipelineLayoutCreationFailed,
                  vk::VulkanComputeDiagnosticCode::kPipelineLayoutCreationFailed},
      FailureCase{FailurePoint::kCreateComputePipelines,
                  vk::VulkanComputeStatus::kComputePipelineCreationFailed,
                  vk::VulkanComputeDiagnosticCode::kComputePipelineCreationFailed},
      FailureCase{FailurePoint::kCreateFence,
                  vk::VulkanComputeStatus::kFenceCreationFailed,
                  vk::VulkanComputeDiagnosticCode::kFenceCreationFailed},
      FailureCase{FailurePoint::kBeginCommandBuffer,
                  vk::VulkanComputeStatus::kCommandBufferBeginFailed,
                  vk::VulkanComputeDiagnosticCode::kCommandBufferBeginFailed},
      FailureCase{FailurePoint::kEndCommandBuffer,
                  vk::VulkanComputeStatus::kCommandBufferEndFailed,
                  vk::VulkanComputeDiagnosticCode::kCommandBufferEndFailed},
      FailureCase{FailurePoint::kQueueSubmit,
                  vk::VulkanComputeStatus::kQueueSubmitFailed,
                  vk::VulkanComputeDiagnosticCode::kQueueSubmitFailed},
  };

  for (const FailureCase& failure : failures) {
    g_fake = {};
    auto context = CreateContext();
    auto execution = CreateExecution(*context.context);
    g_fake.failure = failure.point;
    g_fake.poison_outputs_on_failure = true;
    const auto result = execution->Submit(kValidatedSpirv, 1, 2, 3, 10);
    Check(result.status == failure.status &&
              HasDiagnostic(result, failure.diagnostic,
                            vk::VulkanDiagnosticSeverity::kError),
          "material execution failure lost its structured status");
    CheckNoLiveExecutionResources(
        "material execution failure did not clean partial Vulkan resources");
  }

  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  const auto invalid = execution->Submit({}, 1, 1, 1, 1);
  Check(invalid.status == vk::VulkanComputeStatus::kInvalidArgument &&
            HasDiagnostic(invalid, vk::VulkanComputeDiagnosticCode::kInputRejected,
                          vk::VulkanDiagnosticSeverity::kError),
        "empty SPIR-V did not fail before Vulkan resource creation");
  const auto infinite = execution->Submit(kValidatedSpirv, 1, 1, 1,
                                          UINT64_MAX);
  Check(infinite.status == vk::VulkanComputeStatus::kInvalidArgument,
        "infinite Vulkan fence wait was accepted");
  CheckNoLiveExecutionResources(
      "invalid input allocated execution resources before rejection");

  g_fake.wait_results = {VK_SUCCESS};
  const auto success = execution->Submit(kValidatedSpirv, 1, 2, 3, 10);
  Check(success && success.timeline == 1 && success.completed_timeline == 1 &&
            g_fake.saw_primary_command_buffer &&
            g_fake.saw_empty_pipeline_layout && g_fake.bind_pipeline_calls == 1 &&
            g_fake.dispatch_calls == 1 &&
            g_fake.dispatch_groups == std::array<std::uint32_t, 3>{1, 2, 3} &&
            HasDiagnostic(success,
                          vk::VulkanComputeDiagnosticCode::kSubmissionCompleted,
                          vk::VulkanDiagnosticSeverity::kInfo),
        "successful injected execution did not record a primary empty-layout "
        "compute dispatch");
  CheckNoLiveExecutionResources(
      "successful execution did not destroy its completed resources");
}

void TestContextCreationDoesNotAdoptPoisonedFailureOutputs() {
  g_fake = {};
  g_fake.failure = FailurePoint::kCreateInstance;
  g_fake.poison_outputs_on_failure = true;
  const auto instance_failure = vk::VulkanDeviceContext::Create(FakeLoader());
  Check(instance_failure.initialization.status ==
                vk::VulkanContextStatus::kInstanceCreationFailed &&
            !instance_failure.context && g_fake.instances_created == 0 &&
            g_fake.instances_destroyed == 0 && g_fake.devices_created == 0 &&
            g_fake.devices_destroyed == 0,
        "Vulkan context adopted a poisoned vkCreateInstance failure output");

  g_fake = {};
  g_fake.failure = FailurePoint::kCreateDevice;
  g_fake.poison_outputs_on_failure = true;
  const auto device_failure = vk::VulkanDeviceContext::Create(FakeLoader());
  Check(device_failure.initialization.status ==
                vk::VulkanContextStatus::kDeviceCreationFailed &&
            !device_failure.context && g_fake.devices_created == 0 &&
            g_fake.devices_destroyed == 0 && g_fake.instances_created == 1 &&
            g_fake.instances_destroyed == 1,
        "Vulkan context adopted a poisoned vkCreateDevice failure output");
}

void TestDispatchLimitsCoverEveryDimension() {
  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  constexpr std::array<std::array<std::uint32_t, 3>, 3> boundaries = {
      std::array<std::uint32_t, 3>{7, 1, 1},
      std::array<std::uint32_t, 3>{1, 11, 1},
      std::array<std::uint32_t, 3>{1, 1, 13},
  };
  for (const auto& groups : boundaries) {
    const auto result = execution->Submit(kValidatedSpirv, groups[0], groups[1],
                                          groups[2], 10);
    Check(static_cast<bool>(result),
          "dispatch at a selected-device group-count limit was rejected");
  }
  Check(g_fake.dispatches == std::vector<std::array<std::uint32_t, 3>>(
                                  boundaries.begin(), boundaries.end()),
        "dispatch limits were not persisted for every selected-device dimension");

  constexpr std::array<std::array<std::uint32_t, 3>, 3> over_limits = {
      std::array<std::uint32_t, 3>{8, 1, 1},
      std::array<std::uint32_t, 3>{1, 12, 1},
      std::array<std::uint32_t, 3>{1, 1, 14},
  };
  const std::uint32_t dispatches_before_rejection = g_fake.dispatch_calls;
  for (const auto& groups : over_limits) {
    const auto result = execution->Submit(kValidatedSpirv, groups[0], groups[1],
                                          groups[2], 10);
    Check(result.status == vk::VulkanComputeStatus::kInvalidArgument &&
              HasDiagnostic(result, vk::VulkanComputeDiagnosticCode::kInputRejected,
                            vk::VulkanDiagnosticSeverity::kError),
          "over-limit dispatch was not rejected before command recording");
  }
  Check(g_fake.dispatch_calls == dispatches_before_rejection,
        "over-limit dispatch reached vkCmdDispatch");
  CheckNoLiveExecutionResources(
      "dispatch-limit validation retained Vulkan execution resources");
}

void TestMissingDispatchAndRuntimeOwnerBoundary() {
  g_fake = {};
  {
    auto context = CreateContext();
    auto first = CreateExecution(*context.context);
    const auto duplicate = vk::VulkanComputeExecution::Create(*context.context);
    Check(duplicate.initialization.status ==
                  vk::VulkanComputeStatus::kExecutionAlreadyOwned &&
              !duplicate.execution &&
              HasDiagnostic(duplicate.initialization,
                            vk::VulkanComputeDiagnosticCode::kExecutionAlreadyOwned,
                            vk::VulkanDiagnosticSeverity::kError),
          "VulkanDeviceContext accepted a second compute execution owner");
    first.reset();
    auto replacement = CreateExecution(*context.context);
    replacement.reset();

    g_fake.missing_device_function = "vkCmdDispatch";
    const auto missing = vk::VulkanComputeExecution::Create(*context.context);
    Check(missing.initialization.status ==
                  vk::VulkanComputeStatus::kDeviceFunctionUnavailable &&
              !missing.execution &&
              HasDiagnostic(
                  missing.initialization,
                  vk::VulkanComputeDiagnosticCode::kDeviceFunctionUnavailable,
                  vk::VulkanDiagnosticSeverity::kError),
          "missing core device dispatch was not diagnosed during executor "
          "creation");
  }

  g_fake = {};
  kajps5::memory::GuestMemory memory{
      0x700000, 0x1000,
      kajps5::memory::GuestMemoryProtection::kRead |
          kajps5::memory::GuestMemoryProtection::kWrite};
  kajps5::gpu::GpuRuntime runtime{memory};
  const auto before_context = runtime.SubmitVulkanCompute(kValidatedSpirv, 1, 1, 1,
                                                          10);
  Check(before_context.status == vk::VulkanComputeStatus::kContextUnavailable &&
            !runtime.has_vulkan_compute_execution() &&
            HasDiagnostic(before_context,
                          vk::VulkanComputeDiagnosticCode::kContextUnavailable,
                          vk::VulkanDiagnosticSeverity::kError),
        "GpuRuntime executed without its required Vulkan context");
  const auto initialized = runtime.InitializeVulkan(FakeLoader());
  Check(initialized && runtime.has_vulkan_context(),
        "GpuRuntime did not accept the injected Vulkan context");
  g_fake.wait_results = {VK_SUCCESS, VK_SUCCESS};
  const auto first = runtime.SubmitVulkanCompute(kValidatedSpirv, 1, 1, 1, 10);
  const auto second = runtime.SubmitVulkanCompute(kValidatedSpirv, 1, 1, 1, 10);
  Check(first && second && runtime.has_vulkan_compute_execution() &&
            g_fake.devices_created == 1 && g_fake.device_wait_idle_calls == 0,
        "GpuRuntime did not retain its sole compute execution owner");
  CheckNoLiveExecutionResources(
      "runtime-owned completed submissions retained execution resources");
}

void TestTimeoutDoesNotBlockLaterSubmissionOrFreeEarly() {
  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  g_fake.wait_results = {VK_TIMEOUT, VK_SUCCESS};

  const auto first = execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
  Check(first.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
            first.timeline == 1 && first.retained_submission_count == 1 &&
            g_fake.command_pools_created == 1 &&
            g_fake.command_pools_destroyed == 0 && g_fake.fences_created == 1 &&
            g_fake.fences_destroyed == 0 &&
            HasDiagnostic(first, vk::VulkanComputeDiagnosticCode::kFenceWaitTimedOut,
                          vk::VulkanDiagnosticSeverity::kWarning),
        "timed-out submission was not retained intact");
  const VkFence first_fence = g_fake.submitted_fences.front();

  const auto second = execution->Submit(kValidatedSpirv, 1, 1, 1, 10);
  Check(second && second.timeline == 2 && second.retained_submission_count == 1 &&
            g_fake.submitted_fences.size() == 2 &&
            g_fake.waited_fences.size() == 2 &&
            g_fake.waited_fences[0] == first_fence &&
            g_fake.waited_fences[1] != first_fence,
        "later submission waited for or reused the timed-out submission");
  Check(std::all_of(g_fake.wait_timeouts.begin(), g_fake.wait_timeouts.end(),
                    [](std::uint64_t timeout) { return timeout != UINT64_MAX; }),
        "execution issued an infinite Vulkan fence wait");
  Check(g_fake.command_pools_destroyed == 1 && g_fake.fences_destroyed == 1,
        "second completed submission did not clean only its own resources");

  SignalFence(first_fence);
  const auto reclaimed = execution->PollCompleted();
  Check(reclaimed && reclaimed.reclaimed_submission_count == 1 &&
            reclaimed.retained_submission_count == 0 &&
            reclaimed.completed_timeline == 2 &&
            HasDiagnostic(reclaimed,
                          vk::VulkanComputeDiagnosticCode::kSubmissionReclaimed,
                          vk::VulkanDiagnosticSeverity::kInfo),
        "later fence polling did not reclaim the timed-out submission");
  CheckNoLiveExecutionResources(
      "polling reclaimed a timeout without releasing every owned resource");
}

void TestTimeoutTeardownReleasesChildrenBeforeDeviceTeardown() {
  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  g_fake.wait_results = {VK_TIMEOUT};
  const auto timed_out = execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
  Check(timed_out.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
            timed_out.retained_submission_count == 1,
        "timeout did not establish deterministic in-flight ownership");
  g_fake.require_wait_idle_before_retained_child_destroy = true;
  execution.reset();
  CheckNoLiveExecutionResources(
      "executor teardown did not release timed-out Vulkan children");
  Check(g_fake.device_wait_idle_calls == 1,
        "timed-out executor teardown did not drain retained work exactly once");
  context.context.reset();
  Check(g_fake.devices_destroyed == 1 && g_fake.device_wait_idle_calls >= 1,
        "timed-out executor teardown did not reach Vulkan device destruction");
}

void TestSeparateTimedOutExecutionsAlwaysDrainBeforeTeardown() {
  g_fake = {};
  auto context = CreateContext();
  g_fake.wait_results = {VK_TIMEOUT, VK_TIMEOUT};
  g_fake.require_wait_idle_before_retained_child_destroy = true;

  auto first_execution = CreateExecution(*context.context);
  const auto first_timeout = first_execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
  Check(first_timeout.status == vk::VulkanComputeStatus::kFenceWaitTimedOut,
        "first executor did not retain its timed-out submission");
  first_execution.reset();
  Check(g_fake.device_wait_idle_calls == 1,
        "first timed-out executor did not issue a real idle drain");

  auto second_execution = CreateExecution(*context.context);
  const auto second_timeout =
      second_execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
  Check(second_timeout.status == vk::VulkanComputeStatus::kFenceWaitTimedOut,
        "second executor did not retain its timed-out submission");
  second_execution.reset();
  Check(g_fake.device_wait_idle_calls == 2,
        "second timed-out executor reused an earlier successful idle wait");
  CheckNoLiveExecutionResources(
      "separate timed-out executor teardown retained Vulkan children");
  context.context.reset();
  Check(g_fake.devices_destroyed == 1 && g_fake.device_wait_idle_calls >= 2,
        "separate timed-out executor teardown did not destroy the context "
        "safely");
}

void TestTimeoutTeardownHandlesWaitIdleDeviceLoss() {
  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  g_fake.wait_results = {VK_TIMEOUT};
  const auto timed_out = execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
  Check(timed_out.status == vk::VulkanComputeStatus::kFenceWaitTimedOut,
        "device-loss teardown test did not retain timed-out work");
  g_fake.require_wait_idle_before_retained_child_destroy = true;
  g_fake.device_wait_idle_result = VK_ERROR_DEVICE_LOST;
  execution.reset();
  CheckNoLiveExecutionResources(
      "wait-idle device loss did not release retained Vulkan children");
  Check(g_fake.device_wait_idle_calls == 1,
        "wait-idle device loss did not perform one terminal drain attempt");
  const auto rejected_after_loss =
      vk::VulkanComputeExecution::Create(*context.context);
  Check(rejected_after_loss.initialization.status ==
                vk::VulkanComputeStatus::kDeviceLost &&
            !rejected_after_loss.execution,
        "context accepted a new execution owner after terminal device loss");
  context.context.reset();
  Check(g_fake.devices_destroyed == 1 && g_fake.device_wait_idle_calls == 1,
        "wait-idle device loss caused a duplicate wait or unsafe device "
        "teardown");
}

void TestDeviceLossWinsOwnerAcquisitionRace() {
  g_fake = {};
  auto context = CreateContext();
  auto first_execution = CreateExecution(*context.context);
  g_fake.wait_results = {VK_TIMEOUT};
  Check(first_execution->Submit(kValidatedSpirv, 1, 1, 1, 0).status ==
            vk::VulkanComputeStatus::kFenceWaitTimedOut,
        "owner-race setup did not retain timed-out work");

  {
    std::lock_guard lock(g_create_pause_mutex);
    g_execution_create_paused = false;
    g_pause_execution_create = true;
  }
  std::optional<vk::VulkanComputeExecutionCreateResult> raced_create;
  std::thread creator([&] {
    raced_create.emplace(vk::VulkanComputeExecution::Create(*context.context));
  });
  {
    std::unique_lock lock(g_create_pause_mutex);
    g_create_pause_condition.wait(lock,
                                  [] { return g_execution_create_paused; });
  }

  g_fake.device_wait_idle_result = VK_ERROR_DEVICE_LOST;
  first_execution.reset();
  {
    std::lock_guard lock(g_create_pause_mutex);
    g_pause_execution_create = false;
  }
  g_create_pause_condition.notify_all();
  creator.join();

  Check(raced_create.has_value() &&
            raced_create->initialization.status ==
                vk::VulkanComputeStatus::kDeviceLost &&
            !raced_create->execution,
        "owner acquisition returned a successful execution after device loss");
  const auto retry = vk::VulkanComputeExecution::Create(*context.context);
  Check(retry.initialization.status == vk::VulkanComputeStatus::kDeviceLost &&
            !retry.execution,
        "device-loss owner-acquisition race leaked the owner slot");
  context.context.reset();
}

void TestRetainedSubmissionBound() {
  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  g_fake.wait_results.assign(vk::kMaximumVulkanComputeRetainedSubmissions,
                             VK_TIMEOUT);
  for (std::size_t index = 0;
       index < vk::kMaximumVulkanComputeRetainedSubmissions; ++index) {
    const auto timed_out = execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
    Check(timed_out.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
              timed_out.retained_submission_count == index + 1,
          "retained timed-out work was not counted deterministically");
  }
  const std::uint32_t pools_before_limit = g_fake.command_pools_created;
  const auto limited = execution->Submit(kValidatedSpirv, 1, 1, 1, 0);
  Check(limited.status == vk::VulkanComputeStatus::kResourceLimit &&
            limited.retained_submission_count ==
                vk::kMaximumVulkanComputeRetainedSubmissions &&
            g_fake.command_pools_created == pools_before_limit &&
            HasDiagnostic(limited, vk::VulkanComputeDiagnosticCode::kResourceLimit,
                          vk::VulkanDiagnosticSeverity::kError),
        "retained-work bound did not reject a ninth in-flight submission");
  for (VkFence fence : g_fake.submitted_fences) {
    SignalFence(fence);
  }
  const auto reclaimed = execution->PollCompleted();
  Check(reclaimed && reclaimed.reclaimed_submission_count ==
                            vk::kMaximumVulkanComputeRetainedSubmissions &&
            reclaimed.retained_submission_count == 0,
        "bounded retained submissions did not reclaim after all fences "
        "signalled");
  CheckNoLiveExecutionResources(
      "reclaiming bounded retained work leaked execution resources");
}

void TestDeviceLossIsExplicitAndTerminal() {
  g_fake = {};
  auto context = CreateContext();
  auto execution = CreateExecution(*context.context);
  g_fake.queue_submit_result = VK_ERROR_DEVICE_LOST;
  const auto lost = execution->Submit(kValidatedSpirv, 1, 1, 1, 10);
  Check(lost.status == vk::VulkanComputeStatus::kDeviceLost &&
            HasDiagnostic(lost, vk::VulkanComputeDiagnosticCode::kDeviceLost,
                          vk::VulkanDiagnosticSeverity::kError),
        "Vulkan device loss was not surfaced as an explicit execution status");
  CheckNoLiveExecutionResources(
      "failed device-loss submit did not roll back unsubmitted resources");
  const auto rejected_after_loss = execution->Submit(kValidatedSpirv, 1, 1, 1, 10);
  Check(rejected_after_loss.status == vk::VulkanComputeStatus::kDeviceLost,
        "compute execution accepted work after Vulkan device loss");

  g_fake = {};
  auto wait_lost_context = CreateContext();
  auto wait_lost_execution = CreateExecution(*wait_lost_context.context);
  g_fake.wait_results = {VK_ERROR_DEVICE_LOST};
  const auto wait_lost = wait_lost_execution->Submit(kValidatedSpirv, 1, 1, 1, 10);
  Check(wait_lost.status == vk::VulkanComputeStatus::kDeviceLost &&
            wait_lost.timeline == 1 && g_fake.queue_submit_calls == 1,
        "post-submit fence device loss was not surfaced");
  CheckNoLiveExecutionResources(
      "post-submit device loss did not release retained Vulkan children");
  wait_lost_execution.reset();
  wait_lost_context.context.reset();
  Check(g_fake.devices_destroyed == 1,
        "device-loss teardown destroyed the device before Vulkan children");
}

void TestTranslatedDescriptorAlignmentAndPushData(
    FailurePoint failure = FailurePoint::kNone) {
  g_fake = {};
  g_fake.failure = failure;
  g_fake.poison_outputs_on_failure = failure != FailurePoint::kNone;
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  const std::array<std::byte, 16> bytes{};
  Check(memory.Initialize(0x700004, bytes) &&
            memory.Initialize(0x700108, bytes),
        "translated fixture guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
        "translated fixture Vulkan initialization failed");
  kajps5::gpu::shader::recompiler::CompileResult compile;
  compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
  auto &program = compile.program;
  program.stage = kajps5::gpu::ShaderType::Compute;
  program.resource_tracking_complete = program.shader_info_complete =
      program.binding_layout_complete = true;
  program.bindings.descriptor_set = 0;
  program.bindings.push_constant_size = 8;
  program.bindings.buffer_offset_dword = 1;
  program.bindings.buffer_offset_count = 2;
  program.bindings.user_data_registers = {0};
  program.bindings.descriptors.push_back(
      {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
       5,
       {0, 1}});
  for (std::uint32_t address : {0x700004U, 0x700108U}) {
    program.info.buffers.push_back({.max_byte_extent = 16,
                                    .packed_stride = 4,
                                    .descriptor_format = 0,
                                    .read = true,
                                    .written = true});
    kajps5::gpu::shader::recompiler::IR::DescriptorValue value;
    value.dword_count = 4;
    value.dwords[0] = address;
    value.dwords[1] = 4U << 16U;
    value.dwords[2] = 4;
    compile.resources.buffers.push_back(value);
  }
  compile.resources.user_data = {0x12345678U};
  const auto submitted =
      runtime.SubmitVulkanTranslatedCompute(compile, 2, 3, 4, 10);
  if (failure != FailurePoint::kNone) {
    Check(!submitted && submitted.retained_submission_count == 0,
          "translated injected failure retained work");
    const bool queue_failure = failure == FailurePoint::kQueueSubmit;
    Check(
        g_fake.queue_submit_calls == (queue_failure ? 1U : 0U),
        "translated injected failure reached an unexpected queue-submit count");
    Check(g_fake.buffers_created == g_fake.buffers_destroyed &&
              g_fake.memories_allocated == g_fake.memories_freed &&
              g_fake.descriptor_layouts_created ==
                  g_fake.descriptor_layouts_destroyed &&
              g_fake.descriptor_pools_created ==
                  g_fake.descriptor_pools_destroyed,
          "translated injected failure leaked an adopted backing or descriptor "
          "child");
    return;
  }
  Check(submitted && g_fake.descriptor_binding == 5 &&
            g_fake.descriptor_infos.size() == 2,
        "translated submission did not create the expected descriptor array");
  Check(g_fake.descriptor_infos[0].offset == 0 &&
            g_fake.descriptor_infos[0].range == 16 &&
            g_fake.descriptor_infos[1].offset == 256 &&
            g_fake.descriptor_infos[1].range == 20 &&
            g_fake.descriptor_infos[0].buffer ==
                g_fake.descriptor_infos[1].buffer,
        "translated descriptor offsets did not use one aligned backing with "
        "prefix range");
  Check(g_fake.pushed_words.size() == 2 &&
            g_fake.pushed_words[0] == 0x12345678U &&
            g_fake.pushed_words[1] == (1U << 10U) &&
            g_fake.dispatch_groups == std::array<std::uint32_t, 3>{2, 3, 4},
        "translated push constants did not preserve user data and packed "
        "prefix offsets");
  Check(g_fake.pipeline_barrier_calls == 1 &&
            g_fake.barrier_src_stage_mask ==
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
            g_fake.barrier_dst_stage_mask == VK_PIPELINE_STAGE_HOST_BIT &&
            g_fake.barrier_src_access_mask == VK_ACCESS_SHADER_WRITE_BIT &&
            g_fake.barrier_dst_access_mask == VK_ACCESS_HOST_READ_BIT &&
            g_fake.command_events ==
                std::vector<CommandEvent>{CommandEvent::kDispatch,
                                          CommandEvent::kPipelineBarrier,
                                          CommandEvent::kEndCommandBuffer},
        "translated writable work did not record the compute-to-host memory "
        "barrier");
  const auto &mapped = BytesForBuffer(g_fake.descriptor_infos.front().buffer);
  Check(std::equal(bytes.begin(), bytes.end(), mapped.begin()) &&
            std::equal(bytes.begin(), bytes.end(), mapped.begin() + 260),
        "translated upload did not place guest bytes at data offsets");
}

void TestTranslatedTransactionalFailures() {
  constexpr std::array failures = {FailurePoint::kCreateBuffer,
                                   FailurePoint::kAllocateMemory,
                                   FailurePoint::kBindBufferMemory,
                                   FailurePoint::kMapMemory,
                                   FailurePoint::kFlushMappedMemoryRanges,
                                   FailurePoint::kCreateDescriptorSetLayout,
                                   FailurePoint::kCreateDescriptorPool,
                                   FailurePoint::kAllocateDescriptorSets,
                                   FailurePoint::kCreatePipelineLayout,
                                   FailurePoint::kCreateShaderModule,
                                   FailurePoint::kCreateComputePipelines,
                                   FailurePoint::kCreateCommandPool,
                                   FailurePoint::kAllocateCommandBuffers,
                                   FailurePoint::kCreateFence,
                                   FailurePoint::kBeginCommandBuffer,
                                   FailurePoint::kEndCommandBuffer,
                                   FailurePoint::kQueueSubmit};
  for (const FailurePoint failure : failures) {
    TestTranslatedDescriptorAlignmentAndPushData(failure);
    TestTranslatedDescriptorAlignmentAndPushData();
  }
}

void TestTranslatedTimeoutReadbackRetry() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  std::array<std::byte, 16> first{};
  std::array<std::byte, 16> second{};
  for (std::size_t i = 0; i < first.size(); ++i) {
    first[i] = static_cast<std::byte>(0x20U + i);
    second[i] = static_cast<std::byte>(0x60U + i);
  }
  const std::array<std::byte, 4> before = {std::byte{0xa1}, std::byte{0xa2},
                                           std::byte{0xa3}, std::byte{0xa4}};
  const std::array<std::byte, 4> after = {std::byte{0xb1}, std::byte{0xb2},
                                          std::byte{0xb3}, std::byte{0xb4}};
  Check(memory.Initialize(0x700000, before) &&
            memory.Initialize(0x700004, first) &&
            memory.Initialize(0x700108, second) &&
            memory.Initialize(0x700118, after),
        "timeout fixture guest initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
        "timeout fixture Vulkan initialization failed");
  kajps5::gpu::shader::recompiler::CompileResult compile;
  compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
  auto &program = compile.program;
  program.stage = kajps5::gpu::ShaderType::Compute;
  program.resource_tracking_complete = program.shader_info_complete =
      program.binding_layout_complete = true;
  program.bindings.descriptor_set = 0;
  program.bindings.push_constant_size = 8;
  program.bindings.buffer_offset_dword = 1;
  program.bindings.buffer_offset_count = 2;
  program.bindings.user_data_registers = {0};
  program.bindings.descriptors.push_back(
      {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
       5,
       {0, 1}});
  for (std::uint32_t address : {0x700004U, 0x700108U}) {
    program.info.buffers.push_back({.max_byte_extent = 16,
                                    .packed_stride = 4,
                                    .descriptor_format = 0,
                                    .read = true,
                                    .written = true});
    kajps5::gpu::shader::recompiler::IR::DescriptorValue value;
    value.dword_count = 4;
    value.dwords[0] = address;
    value.dwords[1] = 4U << 16U;
    value.dwords[2] = 4;
    compile.resources.buffers.push_back(value);
  }
  compile.resources.user_data = {0x12345678U};
  g_fake.wait_results = {VK_TIMEOUT};
  const auto timed_out =
      runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 0);
  Check(timed_out.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
            timed_out.retained_submission_count == 1 &&
            g_fake.invalidate_calls == 0,
        "translated timeout did not retain work without readback");
  Check(g_fake.flush_calls == 1 && g_fake.flush_range.offset == 0 &&
            g_fake.flush_range.size % 64 == 0,
        "translated upload did not use an atom-aligned noncoherent flush");
  const std::array<std::byte, 1> cpu_new = {std::byte{0xee}};
  Check(memory.Write(0x700006, cpu_new), "timeout fixture CPU update failed");
  BytesForBuffer(g_fake.descriptor_infos.front().buffer)[1] = std::byte{0x99};
  SignalFence(g_fake.submitted_fences.front());
  g_fake.failure = FailurePoint::kInvalidateMappedMemoryRanges;
  const auto failed = runtime.PollVulkanCompute();
  Check(failed.status == vk::VulkanComputeStatus::kReadbackFailed &&
            failed.retained_submission_count == 1,
        "failed invalidate did not retain translated readback work");
  std::array<std::byte, 16> unchanged{};
  first[2] = cpu_new[0];
  Check(memory.Read(0x700004, unchanged) && unchanged == first,
        "failed invalidate fabricated a guest writeback");
  unchanged[2] = cpu_new[0];
  g_fake.failure = FailurePoint::kNone;
  const auto completed = runtime.PollVulkanCompute();
  Check(
      completed && completed.retained_submission_count == 0 &&
          g_fake.invalidate_calls == 2 && g_fake.invalidate_range.offset == 0 &&
          g_fake.invalidate_range.size % 64 == 0,
      "translated retry did not complete one atom-aligned invalidate/readback");
  std::array<std::byte, 16> readback{};
  std::array<std::byte, 4> read_before{};
  std::array<std::byte, 4> read_after{};
  Check(memory.Read(0x700004, readback) && memory.Read(0x700000, read_before) &&
            memory.Read(0x700118, read_after) &&
            readback[1] == std::byte{0x99} && readback[2] == cpu_new[0] &&
            read_before == before && read_after == after,
        "translated readback did not merge GPU delta with newer CPU data and "
        "sentinels");
  const std::uint32_t invalidates = g_fake.invalidate_calls;
  const auto third = runtime.PollVulkanCompute();
  Check(third && g_fake.invalidate_calls == invalidates,
        "third poll performed a second translated readback");
}

void TestTranslatedDeviceLossPreservesDirtyState() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  const std::array<std::byte, 16> bytes = {std::byte{1}, std::byte{2}};
  const std::array<std::byte, 4> sentinel = {std::byte{0xa1}, std::byte{0xa2},
                                             std::byte{0xa3}, std::byte{0xa4}};
  Check(memory.Initialize(0x700000, sentinel) &&
            memory.Initialize(0x700004, bytes) &&
            memory.Initialize(0x700108, bytes) &&
            memory.Initialize(0x700118, sentinel),
        "device-loss fixture initialization failed");
  kajps5::gpu::GpuRuntime runtime(memory);
  Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
        "device-loss fixture Vulkan initialization failed");
  kajps5::gpu::shader::recompiler::CompileResult compile;
  compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
  auto &p = compile.program;
  p.stage = kajps5::gpu::ShaderType::Compute;
  p.resource_tracking_complete = p.shader_info_complete =
      p.binding_layout_complete = true;
  p.bindings.descriptor_set = 0;
  p.bindings.push_constant_size = 8;
  p.bindings.buffer_offset_dword = 1;
  p.bindings.buffer_offset_count = 2;
  p.bindings.user_data_registers = {0};
  p.bindings.descriptors.push_back(
      {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
       5,
       {0, 1}});
  for (std::uint32_t address : {0x700004U, 0x700108U}) {
    p.info.buffers.push_back({.max_byte_extent = 16,
                              .packed_stride = 4,
                              .descriptor_format = 0,
                              .read = true,
                              .written = true});
    kajps5::gpu::shader::recompiler::IR::DescriptorValue d;
    d.dword_count = 4;
    d.dwords[0] = address;
    d.dwords[1] = 4U << 16U;
    d.dwords[2] = 4;
    compile.resources.buffers.push_back(d);
  }
  compile.resources.user_data = {0};
  g_fake.wait_results = {VK_ERROR_DEVICE_LOST};
  const auto lost = runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 10);
  Check(lost.status == vk::VulkanComputeStatus::kDeviceLost &&
            g_fake.queue_submit_calls == 1 &&
            lost.retained_submission_count == 0 &&
            lost.lost_dirty_resource_count == 2 && g_fake.invalidate_calls == 0,
        "translated device loss did not preserve two dirty resources");
  std::array<std::byte, 16> check_bytes{};
  std::array<std::byte, 4> check_sentinel{};
  Check(memory.Read(0x700004, check_bytes) && check_bytes == bytes &&
            memory.Read(0x700000, check_sentinel) && check_sentinel == sentinel,
        "device loss fabricated translated guest readback");
  const auto polled = runtime.PollVulkanCompute();
  Check(polled.status == vk::VulkanComputeStatus::kDeviceLost &&
            polled.retained_submission_count == 0 &&
            polled.lost_dirty_resource_count == 2 &&
            g_fake.invalidate_calls == 0,
        "terminal translated device loss was not stable across poll");
}

void TestTranslatedReadOnlyDeviceLossHasNoDirtyRecords() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kGpuRead);
  kajps5::gpu::GpuRuntime runtime(memory);
  Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
        "read-only device-loss Vulkan initialization failed");
  kajps5::gpu::shader::recompiler::CompileResult compile;
  compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
  auto &p = compile.program;
  p.stage = kajps5::gpu::ShaderType::Compute;
  p.resource_tracking_complete = p.shader_info_complete =
      p.binding_layout_complete = true;
  p.bindings.descriptor_set = 0;
  p.bindings.push_constant_size = 4;
  p.bindings.buffer_offset_count = 1;
  p.bindings.descriptors.push_back(
      {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
       5,
       {0}});
  p.info.buffers.push_back({.max_byte_extent = 16,
                            .packed_stride = 4,
                            .descriptor_format = 0,
                            .read = true});
  kajps5::gpu::shader::recompiler::IR::DescriptorValue d;
  d.dword_count = 4;
  d.dwords[0] = 0x700000;
  d.dwords[1] = 4U << 16U;
  d.dwords[2] = 4;
  compile.resources.buffers.push_back(d);
  g_fake.wait_results = {VK_ERROR_DEVICE_LOST};
  const auto lost = runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 10);
  Check(lost.status == vk::VulkanComputeStatus::kDeviceLost &&
            lost.retained_submission_count == 0 &&
            lost.lost_dirty_resource_count == 0 && g_fake.invalidate_calls == 0,
        "read-only translated device loss retained a dirty resource");
  Check(g_fake.pipeline_barrier_calls == 0 &&
            g_fake.command_events ==
                std::vector<CommandEvent>{CommandEvent::kDispatch,
                                          CommandEvent::kEndCommandBuffer},
        "read-only translated work recorded an unnecessary host-read barrier");
  const auto polled = runtime.PollVulkanCompute();
  Check(polled.status == vk::VulkanComputeStatus::kDeviceLost &&
            polled.lost_dirty_resource_count == 0 &&
            g_fake.invalidate_calls == 0,
        "read-only terminal loss changed state during poll");
}

void TestTranslatedSequentialBackingReuseAndCpuRefresh() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  std::array<std::byte, 16> initial{};
  std::array<std::byte, 16> refreshed{};
  initial[0] = std::byte{0x11};
  refreshed[0] = std::byte{0x77};
  Check(memory.Initialize(0x700000, initial),
        "reuse fixture initialization failed");
  {
    kajps5::gpu::GpuRuntime runtime(memory);
    Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
          "reuse fixture Vulkan initialization failed");
    kajps5::gpu::shader::recompiler::CompileResult compile;
    compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
    auto &p = compile.program;
    p.stage = kajps5::gpu::ShaderType::Compute;
    p.resource_tracking_complete = p.shader_info_complete =
        p.binding_layout_complete = true;
    p.bindings.descriptor_set = 0;
    p.bindings.push_constant_size = 4;
    p.bindings.buffer_offset_count = 1;
    p.bindings.descriptors.push_back(
        {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
         5,
         {0}});
    p.info.buffers.push_back({.max_byte_extent = 16,
                              .packed_stride = 4,
                              .descriptor_format = 0,
                              .read = true,
                              .written = true});
    kajps5::gpu::shader::recompiler::IR::DescriptorValue d;
    d.dword_count = 4;
    d.dwords[0] = 0x700000;
    d.dwords[1] = 4U << 16U;
    d.dwords[2] = 4;
    compile.resources.buffers.push_back(d);
    const auto first =
        runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 10);
    Check(first && g_fake.descriptor_history.size() == 1,
          "first reuse fixture submission failed");
    const VkBuffer first_buffer = g_fake.descriptor_history[0][0].buffer;
    const auto buffers = g_fake.buffers_created;
    const auto memories = g_fake.memories_allocated;
    Check(memory.Write(0x700000, refreshed),
          "reuse fixture CPU refresh failed");
    const auto second =
        runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 10);
    Check(second && g_fake.descriptor_history.size() == 2 &&
              g_fake.descriptor_history[1][0].buffer == first_buffer &&
              g_fake.buffers_created == buffers &&
              g_fake.memories_allocated == memories &&
              BytesForBuffer(first_buffer)[0] == refreshed[0],
          "completed translated backing was not reused and refreshed");
  }
  Check(g_fake.buffers_created == g_fake.buffers_destroyed &&
            g_fake.memories_allocated == g_fake.memories_freed &&
            g_fake.unmap_calls == g_fake.memories_allocated,
        "pooled translated backing was not released exactly once at teardown");
}

void TestTranslatedInFlightVersionSeparation() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  std::array<std::byte, 16> initial{};
  initial[1] = std::byte{0x12};
  const std::array<std::byte, 4> before = {std::byte{0xa1}, std::byte{0xa2},
                                           std::byte{0xa3}, std::byte{0xa4}};
  const std::array<std::byte, 4> after = {std::byte{0xb1}, std::byte{0xb2},
                                          std::byte{0xb3}, std::byte{0xb4}};
  Check(memory.Initialize(0x700000, before) &&
            memory.Initialize(0x700004, initial) &&
            memory.Initialize(0x700014, after),
        "version fixture initialization failed");
  {
    kajps5::gpu::GpuRuntime runtime(memory);
    Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
          "version fixture Vulkan initialization failed");
    kajps5::gpu::shader::recompiler::CompileResult compile;
    compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
    auto &p = compile.program;
    p.stage = kajps5::gpu::ShaderType::Compute;
    p.resource_tracking_complete = p.shader_info_complete =
        p.binding_layout_complete = true;
    p.bindings.descriptor_set = 0;
    p.bindings.push_constant_size = 4;
    p.bindings.buffer_offset_count = 1;
    p.bindings.descriptors.push_back(
        {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
         5,
         {0}});
    p.info.buffers.push_back({.max_byte_extent = 16,
                              .packed_stride = 4,
                              .descriptor_format = 0,
                              .read = true,
                              .written = true});
    kajps5::gpu::shader::recompiler::IR::DescriptorValue d;
    d.dword_count = 4;
    d.dwords[0] = 0x700004;
    d.dwords[1] = 4U << 16U;
    d.dwords[2] = 4;
    compile.resources.buffers.push_back(d);
    g_fake.wait_results = {VK_TIMEOUT};
    const auto first =
        runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 0);
    Check(first.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
              g_fake.descriptor_history.size() == 1,
          "first version was not retained");
    const VkBuffer first_buffer = g_fake.descriptor_history[0][0].buffer;
    BytesForBuffer(first_buffer)[1] = std::byte{0x99};
    const std::array<std::byte, 1> cpu_new = {std::byte{0xee}};
    Check(memory.Write(0x700006, cpu_new), "version fixture CPU update failed");
    const auto second =
        runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 10);
    const VkBuffer second_buffer = g_fake.descriptor_history[1][0].buffer;
    Check(second && second_buffer != first_buffer &&
              g_fake.buffers_created == 2 && g_fake.memories_allocated == 2 &&
              BytesForBuffer(second_buffer)[2] == cpu_new[0],
          "in-flight version reused or failed to refresh the first backing");
    SignalFence(g_fake.submitted_fences.front());
    const auto reclaimed = runtime.PollVulkanCompute();
    std::array<std::byte, 16> readback{};
    std::array<std::byte, 4> read_before{};
    std::array<std::byte, 4> read_after{};
    Check(reclaimed && reclaimed.retained_submission_count == 0 &&
              memory.Read(0x700004, readback) &&
              memory.Read(0x700000, read_before) &&
              memory.Read(0x700014, read_after) &&
              readback[1] == std::byte{0x99} && readback[2] == cpu_new[0] &&
              read_before == before && read_after == after,
          "first-version readback overwrote newer CPU bytes or sentinels");
  }
  Check(g_fake.buffers_created == g_fake.buffers_destroyed &&
            g_fake.memories_allocated == g_fake.memories_freed &&
            g_fake.unmap_calls == g_fake.memories_allocated,
        "versioned translated backings were not released exactly once");
}

void TestTranslatedBackingBound() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  Check(memory.InitializeFill(0x700000, 16, std::byte{0x42}),
        "bound fixture initialization failed");
  {
    kajps5::gpu::GpuRuntime runtime(memory);
    Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
          "bound fixture Vulkan initialization failed");
    kajps5::gpu::shader::recompiler::CompileResult compile;
    compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
    auto &p = compile.program;
    p.stage = kajps5::gpu::ShaderType::Compute;
    p.resource_tracking_complete = p.shader_info_complete =
        p.binding_layout_complete = true;
    p.bindings.descriptor_set = 0;
    p.bindings.push_constant_size = 4;
    p.bindings.buffer_offset_count = 1;
    p.bindings.descriptors.push_back(
        {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
         5,
         {0}});
    p.info.buffers.push_back({.max_byte_extent = 16,
                              .packed_stride = 4,
                              .descriptor_format = 0,
                              .read = true,
                              .written = true});
    kajps5::gpu::shader::recompiler::IR::DescriptorValue d;
    d.dword_count = 4;
    d.dwords[0] = 0x700000;
    d.dwords[1] = 4U << 16U;
    d.dwords[2] = 4;
    compile.resources.buffers.push_back(d);
    g_fake.wait_results.assign(8, VK_TIMEOUT);
    std::vector<VkBuffer> buffers;
    for (std::size_t index = 0; index < 8; ++index) {
      const auto timed_out =
          runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 0);
      Check(timed_out.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
                timed_out.retained_submission_count == index + 1,
            "translated backing bound did not retain timed-out work");
      buffers.push_back(g_fake.descriptor_history.back()[0].buffer);
    }
    std::sort(buffers.begin(), buffers.end());
    Check(
        std::adjacent_find(buffers.begin(), buffers.end()) == buffers.end() &&
            g_fake.buffers_created == 8 && g_fake.memories_allocated == 8,
        "eight retained translated submissions did not own distinct backings");
    const auto ninth =
        runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 0);
    Check(ninth.status == vk::VulkanComputeStatus::kResourceLimit &&
              ninth.retained_submission_count == 8 &&
              g_fake.queue_submit_calls == 8,
          "ninth translated submission exceeded retained backing bound");
    for (VkFence fence : g_fake.submitted_fences)
      SignalFence(fence);
    const auto reclaimed = runtime.PollVulkanCompute();
    Check(reclaimed && reclaimed.reclaimed_submission_count == 8 &&
              reclaimed.retained_submission_count == 0,
          "translated backing bound did not reclaim all signalled submissions");
  }
  Check(g_fake.buffers_created == g_fake.buffers_destroyed &&
            g_fake.memories_allocated == g_fake.memories_freed &&
            g_fake.unmap_calls == g_fake.memories_allocated,
        "bounded translated backings did not balance at teardown");
}

void TestTranslatedOverlappingAliasCoalescing() {
  g_fake = {};
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  std::array<std::byte, 24> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>(0x20U + index);
  }
  const std::array<std::byte, 4> before = {std::byte{0xa1}, std::byte{0xa2},
                                           std::byte{0xa3}, std::byte{0xa4}};
  const std::array<std::byte, 4> after = {std::byte{0xb1}, std::byte{0xb2},
                                          std::byte{0xb3}, std::byte{0xb4}};
  Check(memory.Initialize(0x700000, before) &&
            memory.Initialize(0x700004, bytes) &&
            memory.Initialize(0x70001c, after),
        "overlapping-alias fixture initialization failed");
  {
    kajps5::gpu::GpuRuntime runtime(memory);
    Check(static_cast<bool>(runtime.InitializeVulkan(FakeLoader())),
          "overlapping-alias Vulkan initialization failed");
    kajps5::gpu::shader::recompiler::CompileResult compile;
    compile.spirv.assign(kValidatedSpirv.begin(), kValidatedSpirv.end());
    auto &program = compile.program;
    program.stage = kajps5::gpu::ShaderType::Compute;
    program.resource_tracking_complete = program.shader_info_complete =
        program.binding_layout_complete = true;
    program.bindings.descriptor_set = 0;
    program.bindings.push_constant_size = 8;
    program.bindings.buffer_offset_dword = 1;
    program.bindings.buffer_offset_count = 2;
    program.bindings.user_data_registers = {0};
    program.bindings.descriptors.push_back(
        {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers,
         5,
         {0, 1}});
    for (std::uint32_t address : {0x700004U, 0x70000cU}) {
      program.info.buffers.push_back({.max_byte_extent = 16,
                                      .packed_stride = 4,
                                      .descriptor_format = 0,
                                      .read = true,
                                      .written = true});
      kajps5::gpu::shader::recompiler::IR::DescriptorValue descriptor;
      descriptor.dword_count = 4;
      descriptor.dwords[0] = address;
      descriptor.dwords[1] = 4U << 16U;
      descriptor.dwords[2] = 4;
      compile.resources.buffers.push_back(descriptor);
    }
    compile.resources.user_data = {0x12345678U};
    g_fake.wait_results = {VK_TIMEOUT};
    const auto timed_out =
        runtime.SubmitVulkanTranslatedCompute(compile, 1, 1, 1, 0);
    Check(timed_out.status == vk::VulkanComputeStatus::kFenceWaitTimedOut &&
              timed_out.retained_submission_count == 1 &&
              g_fake.descriptor_infos.size() == 2 &&
              g_fake.buffers_created == 1 && g_fake.memories_allocated == 1,
          "overlapping aliases did not submit through one retained backing");
    const VkDescriptorBufferInfo &first_descriptor = g_fake.descriptor_infos[0];
    const VkDescriptorBufferInfo &second_descriptor =
        g_fake.descriptor_infos[1];
    Check(first_descriptor.buffer == second_descriptor.buffer &&
              first_descriptor.offset == 0 && first_descriptor.range == 16 &&
              second_descriptor.offset == 0 && second_descriptor.range == 24 &&
              g_fake.pushed_words.size() == 2 &&
              g_fake.pushed_words[0] == 0x12345678U &&
              g_fake.pushed_words[1] == (2U << 10U),
          "overlapping aliases did not retain aligned descriptor offsets and "
          "prefixes");

    auto &mapped = BytesForBuffer(first_descriptor.buffer);
    constexpr std::size_t kOverlapOffset = 11;
    constexpr std::size_t kSecondViewOverlapOffset = kOverlapOffset - 8;
    constexpr std::size_t kSecondViewUniqueOffset = 20;
    const std::byte gpu_overlap = std::byte{0xe1};
    const std::byte gpu_unique = std::byte{0xe2};
    const std::array<std::byte, 1> cpu_new = {std::byte{0xee}};
    mapped[kOverlapOffset] = gpu_overlap;
    mapped[kSecondViewUniqueOffset] = gpu_unique;
    Check(memory.Write(0x700005, cpu_new),
          "overlapping-alias CPU mutation failed");
    SignalFence(g_fake.submitted_fences.front());
    const auto reclaimed = runtime.PollVulkanCompute();
    std::array<std::byte, 16> first_view{};
    std::array<std::byte, 16> second_view{};
    std::array<std::byte, 4> read_before{};
    std::array<std::byte, 4> read_after{};
    Check(reclaimed && reclaimed.retained_submission_count == 0 &&
              memory.Read(0x700004, first_view) &&
              memory.Read(0x70000c, second_view) &&
              memory.Read(0x700000, read_before) &&
              memory.Read(0x70001c, read_after) &&
              first_view[kOverlapOffset] == gpu_overlap &&
              second_view[kSecondViewOverlapOffset] == gpu_overlap &&
              second_view[kSecondViewUniqueOffset - 8] == gpu_unique &&
              first_view[1] == cpu_new[0] && read_before == before &&
              read_after == after && g_fake.buffers_destroyed == 0 &&
              g_fake.memories_freed == 0 && g_fake.unmap_calls == 0,
          "overlapping alias readback did not preserve GPU, CPU, and sentinel "
          "data");
  }
  Check(g_fake.buffers_created == 1 && g_fake.buffers_destroyed == 1 &&
            g_fake.memories_allocated == 1 && g_fake.memories_freed == 1 &&
            g_fake.unmap_calls == 1,
        "pooled overlapping-alias backing was not released exactly once at "
        "teardown");
}

}  // namespace

int main() {
  TestInjectedSuccessAndTransactionalFailures();
  TestContextCreationDoesNotAdoptPoisonedFailureOutputs();
  TestDispatchLimitsCoverEveryDimension();
  TestMissingDispatchAndRuntimeOwnerBoundary();
  TestTimeoutDoesNotBlockLaterSubmissionOrFreeEarly();
  TestTimeoutTeardownReleasesChildrenBeforeDeviceTeardown();
  TestSeparateTimedOutExecutionsAlwaysDrainBeforeTeardown();
  TestTimeoutTeardownHandlesWaitIdleDeviceLoss();
  TestDeviceLossWinsOwnerAcquisitionRace();
  TestRetainedSubmissionBound();
  TestDeviceLossIsExplicitAndTerminal();
  TestTranslatedDescriptorAlignmentAndPushData();
  TestTranslatedTransactionalFailures();
  TestTranslatedTimeoutReadbackRetry();
  TestTranslatedDeviceLossPreservesDirtyState();
  TestTranslatedReadOnlyDeviceLossHasNoDirtyRecords();
  TestTranslatedSequentialBackingReuseAndCpuRefresh();
  TestTranslatedInFlightVersionSeparation();
  TestTranslatedBackingBound();
  TestTranslatedOverlappingAliasCoalescing();
  return 0;
}
