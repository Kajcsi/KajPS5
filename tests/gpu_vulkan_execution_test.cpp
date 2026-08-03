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
};

struct FenceRecord {
  VkFence fence = VK_NULL_HANDLE;
  VkResult status = VK_NOT_READY;
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
  bool saw_primary_command_buffer = false;
  bool saw_empty_pipeline_layout = false;
  std::uint32_t device_wait_idle_calls = 0;
  std::uint32_t devices_created = 0;
  std::uint32_t devices_destroyed = 0;
  std::uint32_t instances_created = 0;
  std::uint32_t instances_destroyed = 0;
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
  properties->limits.maxComputeWorkGroupCount[0] = 7;
  properties->limits.maxComputeWorkGroupCount[1] = 11;
  properties->limits.maxComputeWorkGroupCount[2] = 13;
  std::strncpy(properties->deviceName, "KajPS5 injected Vulkan device",
               VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
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
          "missing core device dispatch was not diagnosed during executor creation");
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
        "separate timed-out executor teardown did not destroy the context safely");
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
        "wait-idle device loss caused a duplicate wait or unsafe device teardown");
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
        "bounded retained submissions did not reclaim after all fences signalled");
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
  return 0;
}
