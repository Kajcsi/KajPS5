// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/graphics/host_gpu/vulkanInstance.h and
// src/graphics/presentation/window/vulkanWindow.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs and
// tests/SharpEmu.Libs.Tests/VideoOut/VulkanPhysicalDeviceScoringTests.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/vulkan/device.h"

namespace {

namespace vk = kajps5::gpu::vulkan;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_vulkan_device_test: " << message << '\n';
    std::exit(1);
  }
}

bool HasDiagnostic(const std::vector<vk::VulkanDiagnostic>& diagnostics,
                   vk::VulkanDiagnosticCode code,
                   vk::VulkanDiagnosticSeverity severity,
                   std::optional<std::size_t> candidate_index = std::nullopt) {
  return std::any_of(
      diagnostics.begin(), diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.code == code && diagnostic.severity == severity &&
               (!candidate_index.has_value() ||
                diagnostic.candidate_index == candidate_index);
      });
}

vk::VulkanDeviceCandidate Candidate(
    std::size_t index, std::string_view name, vk::VulkanPhysicalDeviceType type,
    std::string_view stable_id) {
  vk::VulkanDeviceCandidate candidate;
  candidate.candidate_index = index;
  candidate.name = name;
  candidate.stable_id = stable_id;
  candidate.api_version = VK_API_VERSION_1_3;
  candidate.vendor_id = 0x1234;
  candidate.device_id = static_cast<std::uint32_t>(index);
  candidate.type = type;
  candidate.queue_families = {{0, 1, true, true}};
  candidate.features.dynamic_rendering = true;
  candidate.features.synchronization2 = true;
  candidate.features.robust_image_access = true;
  candidate.features.timeline_semaphore = true;
  candidate.features.sampler_mirror_clamp_to_edge = true;
  candidate.features.sample_rate_shading = true;
  candidate.features.fragment_stores_and_atomics = true;
  candidate.features.sampler_anisotropy = true;
  candidate.features.robust_buffer_access = true;
  candidate.features.depth_bounds = true;
  candidate.features.shader_storage_image_write_without_format = true;
  candidate.features.shader_storage_image_read_without_format = true;
  candidate.features.shader_image_gather_extended = true;
  candidate.features.independent_blend = true;
  candidate.features.tessellation_shader = true;
  candidate.features.vertex_pipeline_stores_and_atomics = true;
  candidate.features.shader_int64 = true;
  candidate.features.texture_compression_bc = true;
  return candidate;
}

void TestDeterministicSelection() {
  auto integrated = Candidate(7, "Integrated", vk::VulkanPhysicalDeviceType::kIntegratedGpu,
                              "device-integrated");
  auto discrete = Candidate(9, "Discrete", vk::VulkanPhysicalDeviceType::kDiscreteGpu,
                            "device-discrete");
  const auto ranked = vk::SelectVulkanDevice({integrated, discrete}, {});
  Check(ranked && ranked.selection->candidate_index == discrete.candidate_index,
        "a discrete device did not outrank an integrated device");

  auto later_uuid = Candidate(3, "Same class later", vk::VulkanPhysicalDeviceType::kDiscreteGpu,
                              "bbbb");
  auto earlier_uuid = Candidate(2, "Same class earlier", vk::VulkanPhysicalDeviceType::kDiscreteGpu,
                                "aaaa");
  earlier_uuid.queue_families = {{5, 1, true, true}, {1, 1, true, true}};
  const auto tied = vk::SelectVulkanDevice({later_uuid, earlier_uuid}, {});
  Check(tied && tied.selection->candidate_index == earlier_uuid.candidate_index &&
            tied.selection->queue_family_index == 1,
        "same-class ties did not use stable ID then lowest valid queue family");

  const auto reordered = vk::SelectVulkanDevice({earlier_uuid, later_uuid}, {});
  Check(reordered &&
            reordered.selection->candidate_index == earlier_uuid.candidate_index,
        "stable tie-break changed when discovery order changed");
}

void TestRequiredGateDiagnostics() {
  auto rejected_api = Candidate(4, "Old API", vk::VulkanPhysicalDeviceType::kDiscreteGpu,
                                "old-api");
  rejected_api.api_version = VK_API_VERSION_1_2;
  const auto api = vk::SelectVulkanDevice({rejected_api}, {});
  Check(!api && HasDiagnostic(api.diagnostics,
                              vk::VulkanDiagnosticCode::kCandidateRejectedApiVersion,
                              vk::VulkanDiagnosticSeverity::kWarning, 4) &&
            HasDiagnostic(api.diagnostics, vk::VulkanDiagnosticCode::kNoSuitableDevice,
                          vk::VulkanDiagnosticSeverity::kError),
        "API rejection did not return structured diagnostics");

  auto rejected_queue = Candidate(5, "No universal queue",
                                  vk::VulkanPhysicalDeviceType::kDiscreteGpu,
                                  "no-queue");
  rejected_queue.queue_families = {{0, 1, true, false}, {1, 1, false, true}};
  const auto queue = vk::SelectVulkanDevice({rejected_queue}, {});
  Check(!queue && HasDiagnostic(queue.diagnostics,
                                vk::VulkanDiagnosticCode::kCandidateRejectedQueue,
                                vk::VulkanDiagnosticSeverity::kWarning, 5),
        "queue rejection did not name the rejected candidate");
  Check(std::any_of(queue.diagnostics.begin(), queue.diagnostics.end(),
                    [](const vk::VulkanDiagnostic& diagnostic) {
                      return diagnostic.code ==
                                 vk::VulkanDiagnosticCode::kCandidateRejectedQueue &&
                             diagnostic.message ==
                                 "Vulkan candidate 'No universal queue' has no "
                                 "required graphics/compute queue";
                    }),
        "queue rejection diagnostic lost the candidate closing quote");

  struct RequiredFeature {
    bool vk::VulkanFeatureSupport::*member;
    const char* name;
  };
  const std::array renderer_ready_features = {
      RequiredFeature{&vk::VulkanFeatureSupport::dynamic_rendering,
                      "dynamicRendering"},
      RequiredFeature{&vk::VulkanFeatureSupport::synchronization2,
                      "synchronization2"},
      RequiredFeature{&vk::VulkanFeatureSupport::robust_image_access,
                      "robustImageAccess"},
      RequiredFeature{&vk::VulkanFeatureSupport::timeline_semaphore,
                      "timelineSemaphore"},
      RequiredFeature{&vk::VulkanFeatureSupport::sampler_mirror_clamp_to_edge,
                      "samplerMirrorClampToEdge"},
      RequiredFeature{&vk::VulkanFeatureSupport::sample_rate_shading,
                      "sampleRateShading"},
      RequiredFeature{&vk::VulkanFeatureSupport::fragment_stores_and_atomics,
                      "fragmentStoresAndAtomics"},
      RequiredFeature{&vk::VulkanFeatureSupport::sampler_anisotropy,
                      "samplerAnisotropy"},
      RequiredFeature{&vk::VulkanFeatureSupport::robust_buffer_access,
                      "robustBufferAccess"},
      RequiredFeature{&vk::VulkanFeatureSupport::depth_bounds, "depthBounds"},
      RequiredFeature{
          &vk::VulkanFeatureSupport::shader_storage_image_write_without_format,
          "shaderStorageImageWriteWithoutFormat"},
      RequiredFeature{
          &vk::VulkanFeatureSupport::shader_storage_image_read_without_format,
          "shaderStorageImageReadWithoutFormat"},
      RequiredFeature{&vk::VulkanFeatureSupport::shader_image_gather_extended,
                      "shaderImageGatherExtended"},
      RequiredFeature{&vk::VulkanFeatureSupport::independent_blend,
                      "independentBlend"},
      RequiredFeature{&vk::VulkanFeatureSupport::tessellation_shader,
                      "tessellationShader"},
  };
  for (const RequiredFeature& required_feature : renderer_ready_features) {
    auto rejected_feature = Candidate(
        6, "Missing renderer-ready feature",
        vk::VulkanPhysicalDeviceType::kDiscreteGpu, "no-feature");
    rejected_feature.features.*(required_feature.member) = false;
    const auto feature = vk::SelectVulkanDevice({rejected_feature}, {});
    Check(!feature && HasDiagnostic(
                          feature.diagnostics,
                          vk::VulkanDiagnosticCode::kCandidateRejectedFeature,
                          vk::VulkanDiagnosticSeverity::kWarning, 6),
          required_feature.name);
  }

  auto optional_capabilities = Candidate(
      10, "Core baseline only", vk::VulkanPhysicalDeviceType::kIntegratedGpu,
      "optional-capabilities");
  optional_capabilities.features.vertex_pipeline_stores_and_atomics = false;
  optional_capabilities.features.shader_int64 = false;
  optional_capabilities.features.texture_compression_bc = false;
  const auto optional = vk::SelectVulkanDevice({optional_capabilities}, {});
  Check(optional &&
            optional.selection->candidate_index ==
                optional_capabilities.candidate_index,
        "an optional compatibility capability rejected a renderer-ready device");

  auto rejected_extension = Candidate(8, "No required extension",
                                      vk::VulkanPhysicalDeviceType::kDiscreteGpu,
                                      "no-extension");
  vk::VulkanDeviceRequirements extension_requirements;
  extension_requirements.required_extensions = {"VK_KAJPS5_test_required"};
  const auto extension =
      vk::SelectVulkanDevice({rejected_extension}, extension_requirements);
  Check(!extension && HasDiagnostic(
                          extension.diagnostics,
                          vk::VulkanDiagnosticCode::kCandidateRejectedExtension,
                          vk::VulkanDiagnosticSeverity::kWarning, 8),
        "extension rejection did not return structured diagnostics");
}

struct FakeVulkanState {
  std::uint32_t loader_version = VK_API_VERSION_1_3;
  std::uint32_t device_api_version = VK_API_VERSION_1_3;
  bool fail_device_create = false;
  bool omit_device_wait_idle = false;
  bool return_null_queue = false;
  bool saw_renderer_ready_features = false;
  std::uint32_t instances_created = 0;
  std::uint32_t instances_destroyed = 0;
  std::uint32_t devices_created = 0;
  std::uint32_t devices_destroyed = 0;
  std::uint32_t wait_idle_calls = 0;
};

FakeVulkanState g_fake;

VkInstance FakeInstance() {
  return reinterpret_cast<VkInstance>(static_cast<std::uintptr_t>(0x101));
}

VkPhysicalDevice FakePhysicalDevice() {
  return reinterpret_cast<VkPhysicalDevice>(
      static_cast<std::uintptr_t>(0x202));
}

VkDevice FakeDevice() {
  return reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(0x303));
}

VkQueue FakeQueue() {
  return reinterpret_cast<VkQueue>(static_cast<std::uintptr_t>(0x404));
}

VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerateInstanceVersion(
    std::uint32_t* version) {
  *version = g_fake.loader_version;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateInstance(
    const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance) {
  ++g_fake.instances_created;
  *instance = FakeInstance();
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
  properties->apiVersion = g_fake.device_api_version;
  properties->driverVersion = VK_MAKE_API_VERSION(0, 1, 2, 3);
  properties->vendorID = 0x10de;
  properties->deviceID = 0x4090;
  properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  std::strncpy(properties->deviceName, "Fake RTX 4090",
               VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
  properties->limits.minStorageBufferOffsetAlignment = 256;
  properties->limits.maxStorageBufferRange = 8192;
  properties->limits.maxPerStageDescriptorStorageBuffers = 7;
  properties->limits.maxDescriptorSetStorageBuffers = 8;
  properties->limits.maxPerStageDescriptorSampledImages = 9;
  properties->limits.maxDescriptorSetSampledImages = 10;
  properties->limits.maxPerStageDescriptorStorageImages = 11;
  properties->limits.maxDescriptorSetStorageImages = 12;
  properties->limits.maxPerStageDescriptorSamplers = 13;
  properties->limits.maxDescriptorSetSamplers = 14;
  properties->limits.maxPerStageResources = 15;
}

VKAPI_ATTR void VKAPI_CALL FakeGetPhysicalDeviceProperties2(
    VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
  if (properties->pNext == nullptr) {
    return;
  }
  auto* identifiers =
      static_cast<VkPhysicalDeviceIDProperties*>(properties->pNext);
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
  if (*count == 0) {
    return;
  }
  properties[0] = {};
  properties[0].queueCount = 1;
  properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
  *count = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerateDeviceExtensionProperties(
    VkPhysicalDevice, const char*, std::uint32_t* count,
    VkExtensionProperties* properties) {
  (void)properties;
  *count = 0;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDevice(
    VkPhysicalDevice, const VkDeviceCreateInfo* info,
    const VkAllocationCallbacks*, VkDevice* device) {
  ++g_fake.devices_created;
  const auto* features = static_cast<const VkPhysicalDeviceFeatures2*>(info->pNext);
  const auto* features13 =
      static_cast<const VkPhysicalDeviceVulkan13Features*>(features->pNext);
  const auto* features12 =
      static_cast<const VkPhysicalDeviceVulkan12Features*>(features13->pNext);
  g_fake.saw_renderer_ready_features =
      features13->dynamicRendering == VK_TRUE &&
      features13->synchronization2 == VK_TRUE &&
      features13->robustImageAccess == VK_TRUE &&
      features12->timelineSemaphore == VK_TRUE &&
      features12->samplerMirrorClampToEdge == VK_TRUE &&
      features->features.sampleRateShading == VK_TRUE &&
      features->features.fragmentStoresAndAtomics == VK_TRUE &&
      features->features.samplerAnisotropy == VK_TRUE &&
      features->features.robustBufferAccess == VK_TRUE &&
      features->features.depthBounds == VK_TRUE &&
      features->features.shaderStorageImageWriteWithoutFormat == VK_TRUE &&
      features->features.shaderStorageImageReadWithoutFormat == VK_TRUE &&
      features->features.shaderImageGatherExtended == VK_TRUE &&
      features->features.independentBlend == VK_TRUE &&
      features->features.tessellationShader == VK_TRUE &&
      features->features.vertexPipelineStoresAndAtomics == VK_TRUE &&
      features->features.shaderInt64 == VK_TRUE &&
      features->features.textureCompressionBC == VK_TRUE;
  if (g_fake.fail_device_create) {
    *device = VK_NULL_HANDLE;
    return VK_ERROR_FEATURE_NOT_PRESENT;
  }
  *device = FakeDevice();
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroyDevice(VkDevice,
                                              const VkAllocationCallbacks*) {
  ++g_fake.devices_destroyed;
}

VKAPI_ATTR void VKAPI_CALL FakeGetDeviceQueue(VkDevice, std::uint32_t,
                                               std::uint32_t, VkQueue* queue) {
  *queue = g_fake.return_null_queue ? VK_NULL_HANDLE : FakeQueue();
}

VKAPI_ATTR VkResult VKAPI_CALL FakeDeviceWaitIdle(VkDevice) {
  ++g_fake.wait_idle_calls;
  return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetDeviceProcAddr(VkDevice,
                                                                 const char* name) {
  if (std::strcmp(name, "vkDestroyDevice") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDestroyDevice);
  }
  if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetDeviceQueue);
  }
  if (std::strcmp(name, "vkDeviceWaitIdle") == 0) {
    if (g_fake.omit_device_wait_idle) {
      return nullptr;
    }
    return reinterpret_cast<PFN_vkVoidFunction>(FakeDeviceWaitIdle);
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
    return reinterpret_cast<PFN_vkVoidFunction>(FakeGetPhysicalDeviceProperties2);
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

void TestInjectedTransactionality() {
  g_fake = {};
  g_fake.fail_device_create = true;
  {
    kajps5::memory::GuestMemory memory{
        0x700000, 0x1000,
        kajps5::memory::GuestMemoryProtection::kRead |
            kajps5::memory::GuestMemoryProtection::kWrite};
    kajps5::gpu::GpuRuntime runtime{memory};
    Check(!runtime.has_vulkan_context(),
          "GpuRuntime constructed a Vulkan context without an explicit request");

    const auto failed = runtime.InitializeVulkan(FakeLoader());
    Check(failed.status == vk::VulkanContextStatus::kDeviceCreationFailed &&
              !runtime.has_vulkan_context() && g_fake.instances_created == 1 &&
              g_fake.instances_destroyed == 1 && g_fake.devices_created == 1 &&
              g_fake.devices_destroyed == 0,
          "late device-creation failure did not roll back transactionally");
    Check(HasDiagnostic(failed.diagnostics,
                        vk::VulkanDiagnosticCode::kDeviceCreationFailed,
                        vk::VulkanDiagnosticSeverity::kError),
          "late device-creation failure lost its structured diagnostic");

    g_fake.fail_device_create = false;
    const auto initialized = runtime.InitializeVulkan(FakeLoader());
    Check(initialized && runtime.has_vulkan_context() &&
              runtime.vulkan_context() != nullptr &&
              runtime.vulkan_context()->queue_family_index() == 0,
          "retry after failure did not create a complete Vulkan context");
    const auto& supported = runtime.vulkan_context()->supported_capabilities();
    const auto& enabled = runtime.vulkan_context()->enabled_capabilities();
    const auto& limits = runtime.vulkan_context()->properties();
    Check(g_fake.saw_renderer_ready_features &&
              supported.vertex_pipeline_stores_and_atomics &&
              supported.shader_int64 && supported.texture_compression_bc &&
              enabled.dynamic_rendering && enabled.synchronization2 &&
              enabled.robust_image_access && enabled.timeline_semaphore &&
              enabled.sampler_mirror_clamp_to_edge && enabled.sample_rate_shading &&
              enabled.fragment_stores_and_atomics && enabled.sampler_anisotropy &&
              enabled.robust_buffer_access && enabled.depth_bounds &&
              enabled.shader_storage_image_write_without_format &&
              enabled.shader_storage_image_read_without_format &&
              enabled.shader_image_gather_extended && enabled.independent_blend &&
              enabled.tessellation_shader &&
              enabled.vertex_pipeline_stores_and_atomics && enabled.shader_int64 &&
              enabled.texture_compression_bc &&
              limits.max_storage_buffer_range == 8192 &&
              limits.max_per_stage_descriptor_storage_buffers == 7 &&
              limits.max_descriptor_set_storage_buffers == 8 &&
              limits.max_per_stage_descriptor_sampled_images == 9 &&
              limits.max_descriptor_set_sampled_images == 10 &&
              limits.max_per_stage_descriptor_storage_images == 11 &&
              limits.max_descriptor_set_storage_images == 12 &&
              limits.max_per_stage_descriptor_samplers == 13 &&
              limits.max_descriptor_set_samplers == 14 &&
              limits.max_per_stage_resources == 15,
          "created logical device did not report its exact renderer-ready and optional enablement");
    const std::uint32_t instances_before_duplicate = g_fake.instances_created;
    const auto duplicate = runtime.InitializeVulkan(FakeLoader());
    Check(duplicate.status == vk::VulkanContextStatus::kAlreadyInitialized &&
              HasDiagnostic(duplicate.diagnostics,
                            vk::VulkanDiagnosticCode::kDuplicateInitialization,
                            vk::VulkanDiagnosticSeverity::kError) &&
              g_fake.instances_created == instances_before_duplicate,
          "duplicate initialization changed the existing context transactionally");
  }
  Check(g_fake.devices_destroyed == 1 && g_fake.instances_destroyed == 2 &&
            g_fake.wait_idle_calls == 1,
        "RAII destruction did not release device before instance exactly once");
}

void TestPostCreateFailureRollback() {
  g_fake = {};
  g_fake.omit_device_wait_idle = true;
  const auto missing_dispatch = vk::VulkanDeviceContext::Create(FakeLoader());
  Check(missing_dispatch.initialization.status ==
                vk::VulkanContextStatus::kDeviceFunctionUnavailable &&
            !missing_dispatch.context &&
            HasDiagnostic(missing_dispatch.initialization.diagnostics,
                          vk::VulkanDiagnosticCode::kDeviceFunctionUnavailable,
                          vk::VulkanDiagnosticSeverity::kError) &&
            g_fake.saw_renderer_ready_features && g_fake.instances_created == 1 &&
            g_fake.instances_destroyed == 1 && g_fake.devices_created == 1 &&
            g_fake.devices_destroyed == 1 && g_fake.wait_idle_calls == 0,
        "post-create missing device dispatch did not return the exact status and clean up once");

  g_fake = {};
  g_fake.return_null_queue = true;
  const auto null_queue = vk::VulkanDeviceContext::Create(FakeLoader());
  Check(null_queue.initialization.status == vk::VulkanContextStatus::kQueueUnavailable &&
            !null_queue.context &&
            HasDiagnostic(null_queue.initialization.diagnostics,
                          vk::VulkanDiagnosticCode::kQueueUnavailable,
                          vk::VulkanDiagnosticSeverity::kError) &&
            g_fake.saw_renderer_ready_features && g_fake.instances_created == 1 &&
            g_fake.instances_destroyed == 1 && g_fake.devices_created == 1 &&
            g_fake.devices_destroyed == 1 && g_fake.wait_idle_calls == 1,
        "post-create null queue did not return the exact status and clean up once");
}

}  // namespace

int main() {
  TestDeterministicSelection();
  TestRequiredGateDiagnostics();
  TestInjectedTransactionality();
  TestPostCreateFailureRollback();
  return 0;
}
