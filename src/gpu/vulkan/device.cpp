// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/vulkanInstance.h and
// src/graphics/presentation/window/vulkanWindow.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and diagnostic reference: Copyright (C) 2026 SharpEmu Emulator
// Project, src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs and
// tests/SharpEmu.Libs.Tests/VideoOut/VulkanPhysicalDeviceScoringTests.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/device.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string_view>
#include <utility>

namespace kajps5::gpu::vulkan {
namespace {

constexpr char kPortabilitySubsetExtension[] = "VK_KHR_portability_subset";

template <typename Function>
auto LoadInstanceFunction(PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                          VkInstance instance, const char* name) noexcept {
  return reinterpret_cast<Function>(get_instance_proc_addr(instance, name));
}

template <typename Value, typename Enumerate>
VkResult EnumerateVulkan(Enumerate&& enumerate, std::vector<Value>& values) {
  for (std::uint32_t attempt = 0; attempt != 4; ++attempt) {
    std::uint32_t count = 0;
    const VkResult count_result = enumerate(&count, nullptr);
    if (count_result != VK_SUCCESS) {
      return count_result;
    }

    values.resize(count);
    if (count == 0) {
      return VK_SUCCESS;
    }

    const VkResult value_result = enumerate(&count, values.data());
    if (value_result == VK_SUCCESS) {
      values.resize(count);
      return VK_SUCCESS;
    }
    if (value_result != VK_INCOMPLETE) {
      values.clear();
      return value_result;
    }
  }

  values.clear();
  return VK_INCOMPLETE;
}

void AddDiagnostic(std::vector<VulkanDiagnostic>& diagnostics,
                   VulkanDiagnosticSeverity severity,
                   VulkanDiagnosticCode code, std::string message,
                   std::optional<std::size_t> candidate_index = std::nullopt,
                   VkResult api_result = VK_SUCCESS) {
  diagnostics.push_back({severity, code, candidate_index,
                         static_cast<std::int32_t>(api_result),
                         std::move(message)});
}

std::string VersionString(std::uint32_t version) {
  return std::to_string(VK_VERSION_MAJOR(version)) + "." +
         std::to_string(VK_VERSION_MINOR(version)) + "." +
         std::to_string(VK_VERSION_PATCH(version));
}

bool HasExtension(const VulkanDeviceCandidate& candidate,
                  std::string_view extension) {
  return std::any_of(candidate.extensions.begin(), candidate.extensions.end(),
                     [extension](const std::string& available) {
                       return available == extension;
                     });
}

std::optional<std::uint32_t> FindQueueFamily(
    const VulkanDeviceCandidate& candidate,
    const VulkanDeviceRequirements& requirements) {
  std::optional<std::uint32_t> selected;
  for (const VulkanQueueFamily& family : candidate.queue_families) {
    if (family.queue_count == 0 ||
        (requirements.require_graphics_queue && !family.supports_graphics) ||
        (requirements.require_compute_queue && !family.supports_compute)) {
      continue;
    }
    if (!selected.has_value() || family.index < *selected) {
      selected = family.index;
    }
  }
  return selected;
}

int DeviceTypeRank(VulkanPhysicalDeviceType type) noexcept {
  switch (type) {
    case VulkanPhysicalDeviceType::kDiscreteGpu:
      return 4;
    case VulkanPhysicalDeviceType::kIntegratedGpu:
      return 3;
    case VulkanPhysicalDeviceType::kVirtualGpu:
      return 2;
    case VulkanPhysicalDeviceType::kCpu:
      return 1;
    case VulkanPhysicalDeviceType::kOther:
      return 0;
  }
  return 0;
}

bool IsBetterSelection(const VulkanDeviceCandidate& candidate,
                       std::uint32_t queue_family_index,
                       const VulkanDeviceCandidate& current,
                       std::uint32_t current_queue_family_index) noexcept {
  const int candidate_rank = DeviceTypeRank(candidate.type);
  const int current_rank = DeviceTypeRank(current.type);
  if (candidate_rank != current_rank) {
    return candidate_rank > current_rank;
  }
  if (candidate.api_version != current.api_version) {
    return candidate.api_version > current.api_version;
  }
  if (candidate.stable_id != current.stable_id) {
    return candidate.stable_id < current.stable_id;
  }
  if (candidate.vendor_id != current.vendor_id) {
    return candidate.vendor_id < current.vendor_id;
  }
  if (candidate.device_id != current.device_id) {
    return candidate.device_id < current.device_id;
  }
  if (candidate.name != current.name) {
    return candidate.name < current.name;
  }
  if (queue_family_index != current_queue_family_index) {
    return queue_family_index < current_queue_family_index;
  }
  return candidate.candidate_index < current.candidate_index;
}

VulkanPhysicalDeviceType ToDeviceType(VkPhysicalDeviceType type) noexcept {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return VulkanPhysicalDeviceType::kIntegratedGpu;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return VulkanPhysicalDeviceType::kDiscreteGpu;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return VulkanPhysicalDeviceType::kVirtualGpu;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return VulkanPhysicalDeviceType::kCpu;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
      return VulkanPhysicalDeviceType::kOther;
  }
}

std::string DeviceStableId(const VkPhysicalDeviceProperties& properties,
                           PFN_vkGetPhysicalDeviceProperties2
                               get_physical_device_properties2,
                           VkPhysicalDevice physical_device) {
  VkPhysicalDeviceIDProperties identifiers{};
  identifiers.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  VkPhysicalDeviceProperties2 properties2{};
  properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties2.pNext = &identifiers;
  get_physical_device_properties2(physical_device, &properties2);

  constexpr std::array<char, 16> kHex = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string stable_id;
  stable_id.reserve(VK_UUID_SIZE * 2);
  for (std::uint32_t index = 0; index < VK_UUID_SIZE; ++index) {
    const std::uint8_t value = identifiers.deviceUUID[index];
    stable_id.push_back(kHex[value >> 4U]);
    stable_id.push_back(kHex[value & 0x0fU]);
  }

  const bool all_zero = std::all_of(
      stable_id.begin(), stable_id.end(), [](char value) { return value == '0'; });
  if (!all_zero) {
    return stable_id;
  }

  return std::to_string(properties.vendorID) + ":" +
         std::to_string(properties.deviceID) + ":" + properties.deviceName;
}

struct InstanceDispatch {
  PFN_vkDestroyInstance destroy_instance = nullptr;
  PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
  PFN_vkGetPhysicalDeviceProperties get_physical_device_properties = nullptr;
  PFN_vkGetPhysicalDeviceProperties2 get_physical_device_properties2 =
      nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 get_physical_device_features2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties
      get_physical_device_queue_family_properties = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties
      enumerate_device_extension_properties = nullptr;
  PFN_vkCreateDevice create_device = nullptr;
  PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
  PFN_vkDestroyDevice destroy_device = nullptr;
};

struct DeviceDispatch {
  PFN_vkDestroyDevice destroy_device = nullptr;
  PFN_vkGetDeviceQueue get_device_queue = nullptr;
  PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
};

bool LoadInstanceDispatch(PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                          VkInstance instance, InstanceDispatch& dispatch,
                          std::vector<VulkanDiagnostic>& diagnostics) {
  dispatch.destroy_instance = LoadInstanceFunction<PFN_vkDestroyInstance>(
      get_instance_proc_addr, instance, "vkDestroyInstance");
  dispatch.enumerate_physical_devices =
      LoadInstanceFunction<PFN_vkEnumeratePhysicalDevices>(
          get_instance_proc_addr, instance, "vkEnumeratePhysicalDevices");
  dispatch.get_physical_device_properties =
      LoadInstanceFunction<PFN_vkGetPhysicalDeviceProperties>(
          get_instance_proc_addr, instance, "vkGetPhysicalDeviceProperties");
  dispatch.get_physical_device_properties2 =
      LoadInstanceFunction<PFN_vkGetPhysicalDeviceProperties2>(
          get_instance_proc_addr, instance, "vkGetPhysicalDeviceProperties2");
  dispatch.get_physical_device_features2 =
      LoadInstanceFunction<PFN_vkGetPhysicalDeviceFeatures2>(
          get_instance_proc_addr, instance, "vkGetPhysicalDeviceFeatures2");
  dispatch.get_physical_device_queue_family_properties =
      LoadInstanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
          get_instance_proc_addr, instance,
          "vkGetPhysicalDeviceQueueFamilyProperties");
  dispatch.enumerate_device_extension_properties =
      LoadInstanceFunction<PFN_vkEnumerateDeviceExtensionProperties>(
          get_instance_proc_addr, instance,
          "vkEnumerateDeviceExtensionProperties");
  dispatch.create_device = LoadInstanceFunction<PFN_vkCreateDevice>(
      get_instance_proc_addr, instance, "vkCreateDevice");
  dispatch.get_device_proc_addr = LoadInstanceFunction<PFN_vkGetDeviceProcAddr>(
      get_instance_proc_addr, instance, "vkGetDeviceProcAddr");
  dispatch.destroy_device = LoadInstanceFunction<PFN_vkDestroyDevice>(
      get_instance_proc_addr, instance, "vkDestroyDevice");

  if (dispatch.destroy_instance != nullptr &&
      dispatch.enumerate_physical_devices != nullptr &&
      dispatch.get_physical_device_properties != nullptr &&
      dispatch.get_physical_device_properties2 != nullptr &&
      dispatch.get_physical_device_features2 != nullptr &&
      dispatch.get_physical_device_queue_family_properties != nullptr &&
      dispatch.enumerate_device_extension_properties != nullptr &&
      dispatch.create_device != nullptr &&
      dispatch.get_device_proc_addr != nullptr && dispatch.destroy_device != nullptr) {
    return true;
  }

  AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                VulkanDiagnosticCode::kInstanceFunctionUnavailable,
                "Vulkan instance is missing a required physical-device "
                "discovery entry point");
  return false;
}

bool LoadDeviceDispatch(const InstanceDispatch& instance_dispatch,
                        VkDevice device,
                        DeviceDispatch& dispatch,
                        std::vector<VulkanDiagnostic>& diagnostics) {
  dispatch.destroy_device = reinterpret_cast<PFN_vkDestroyDevice>(
      instance_dispatch.get_device_proc_addr(device, "vkDestroyDevice"));
  dispatch.get_device_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(
      instance_dispatch.get_device_proc_addr(device, "vkGetDeviceQueue"));
  dispatch.device_wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
      instance_dispatch.get_device_proc_addr(device, "vkDeviceWaitIdle"));

  if (dispatch.destroy_device == nullptr) {
    dispatch.destroy_device = instance_dispatch.destroy_device;
  }

  if (dispatch.destroy_device != nullptr && dispatch.get_device_queue != nullptr &&
      dispatch.device_wait_idle != nullptr) {
    return true;
  }

  AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                VulkanDiagnosticCode::kDeviceFunctionUnavailable,
                "Vulkan device is missing a required queue/device entry "
                "point");
  return false;
}

bool DiscoverCandidate(const InstanceDispatch& dispatch,
                       VkPhysicalDevice physical_device,
                       std::size_t candidate_index,
                       VulkanDeviceCandidate& candidate,
                       std::vector<VulkanDiagnostic>& diagnostics) {
  VkPhysicalDeviceProperties properties{};
  dispatch.get_physical_device_properties(physical_device, &properties);

  candidate.candidate_index = candidate_index;
  candidate.name = properties.deviceName;
  candidate.stable_id = DeviceStableId(
      properties, dispatch.get_physical_device_properties2, physical_device);
  candidate.api_version = properties.apiVersion;
  candidate.driver_version = properties.driverVersion;
  candidate.vendor_id = properties.vendorID;
  candidate.device_id = properties.deviceID;
  candidate.type = ToDeviceType(properties.deviceType);
  candidate.min_storage_buffer_offset_alignment =
      std::max<std::uint64_t>(
          properties.limits.minStorageBufferOffsetAlignment, 1U);

  VkPhysicalDeviceVulkan12Features features12{};
  features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.pNext = &features12;
  VkPhysicalDeviceFeatures2 features2{};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = &features13;
  dispatch.get_physical_device_features2(physical_device, &features2);

  candidate.features.dynamic_rendering =
      features13.dynamicRendering == VK_TRUE;
  candidate.features.synchronization2 = features13.synchronization2 == VK_TRUE;
  candidate.features.robust_image_access =
      features13.robustImageAccess == VK_TRUE;
  candidate.features.timeline_semaphore =
      features12.timelineSemaphore == VK_TRUE;
  candidate.features.sampler_mirror_clamp_to_edge =
      features12.samplerMirrorClampToEdge == VK_TRUE;
  candidate.features.sample_rate_shading =
      features2.features.sampleRateShading == VK_TRUE;
  candidate.features.fragment_stores_and_atomics =
      features2.features.fragmentStoresAndAtomics == VK_TRUE;
  candidate.features.sampler_anisotropy =
      features2.features.samplerAnisotropy == VK_TRUE;
  candidate.features.robust_buffer_access =
      features2.features.robustBufferAccess == VK_TRUE;
  candidate.features.depth_bounds = features2.features.depthBounds == VK_TRUE;
  candidate.features.vertex_pipeline_stores_and_atomics =
      features2.features.vertexPipelineStoresAndAtomics == VK_TRUE;
  candidate.features.shader_int64 = features2.features.shaderInt64 == VK_TRUE;
  candidate.features.shader_image_gather_extended =
      features2.features.shaderImageGatherExtended == VK_TRUE;
  candidate.features.shader_storage_image_read_without_format =
      features2.features.shaderStorageImageReadWithoutFormat == VK_TRUE;
  candidate.features.shader_storage_image_write_without_format =
      features2.features.shaderStorageImageWriteWithoutFormat == VK_TRUE;
  candidate.features.independent_blend =
      features2.features.independentBlend == VK_TRUE;
  candidate.features.tessellation_shader =
      features2.features.tessellationShader == VK_TRUE;
  candidate.features.texture_compression_bc =
      features2.features.textureCompressionBC == VK_TRUE;

  std::uint32_t queue_family_count = 0;
  dispatch.get_physical_device_queue_family_properties(
      physical_device, &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  if (queue_family_count != 0) {
    dispatch.get_physical_device_queue_family_properties(
        physical_device, &queue_family_count, queue_families.data());
    queue_families.resize(queue_family_count);
  }
  candidate.queue_families.reserve(queue_families.size());
  for (std::uint32_t index = 0; index < queue_families.size(); ++index) {
    const VkQueueFlags flags = queue_families[index].queueFlags;
    candidate.queue_families.push_back(
        {index, queue_families[index].queueCount,
         (flags & VK_QUEUE_GRAPHICS_BIT) != 0,
         (flags & VK_QUEUE_COMPUTE_BIT) != 0});
  }

  std::vector<VkExtensionProperties> extensions;
  const VkResult extension_result = EnumerateVulkan<VkExtensionProperties>(
      [&](std::uint32_t* count, VkExtensionProperties* values) {
        return dispatch.enumerate_device_extension_properties(
            physical_device, nullptr, count, values);
      },
      extensions);
  if (extension_result != VK_SUCCESS) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed,
                  "could not enumerate extensions for Vulkan candidate '" +
                      candidate.name + "'",
                  candidate_index, extension_result);
    return false;
  }
  candidate.extensions.reserve(extensions.size());
  for (const VkExtensionProperties& extension : extensions) {
    candidate.extensions.emplace_back(extension.extensionName);
  }
  return true;
}

void AddOptionalCapabilityDiagnostics(
    const VulkanDeviceCandidate& candidate,
    std::vector<VulkanDiagnostic>& diagnostics) {
  struct OptionalCapability {
    bool available;
    const char* name;
    const char* implication;
  };
  const std::array optional_capabilities = {
      OptionalCapability{candidate.features.shader_int64, "shaderInt64",
                          "translated shaders using 64-bit integers will fail"},
      OptionalCapability{
          candidate.features.vertex_pipeline_stores_and_atomics,
          "vertexPipelineStoresAndAtomics",
          "storage-buffer atomics in vertex stages may fail"},
      OptionalCapability{candidate.features.texture_compression_bc,
                          "textureCompressionBC",
                          "BC1-BC7 textures cannot be sampled directly"},
  };
  for (const OptionalCapability& capability : optional_capabilities) {
    if (capability.available) {
      continue;
    }
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kWarning,
                  VulkanDiagnosticCode::kOptionalCapabilityUnavailable,
                  "Vulkan candidate '" + candidate.name + "' lacks optional " +
                      capability.name + "; " + capability.implication,
                  candidate.candidate_index);
  }
}

VulkanFeatureEnablement BuildEnabledCapabilities(
    const VulkanDeviceCandidate& candidate,
    const VulkanDeviceRequirements& requirements) {
  VulkanFeatureEnablement enabled;
  enabled.dynamic_rendering = requirements.require_dynamic_rendering;
  enabled.synchronization2 = requirements.require_synchronization2;
  enabled.robust_image_access = requirements.require_robust_image_access;
  enabled.timeline_semaphore = requirements.require_timeline_semaphore;
  enabled.sampler_mirror_clamp_to_edge =
      requirements.require_sampler_mirror_clamp_to_edge;
  enabled.sample_rate_shading = requirements.require_sample_rate_shading;
  enabled.fragment_stores_and_atomics =
      requirements.require_fragment_stores_and_atomics;
  enabled.sampler_anisotropy = requirements.require_sampler_anisotropy;
  enabled.robust_buffer_access = requirements.require_robust_buffer_access;
  enabled.depth_bounds = requirements.require_depth_bounds;
  enabled.shader_storage_image_write_without_format =
      requirements.require_shader_storage_image_write_without_format;
  enabled.shader_storage_image_read_without_format =
      requirements.require_shader_storage_image_read_without_format;
  enabled.shader_image_gather_extended =
      requirements.require_shader_image_gather_extended;
  enabled.independent_blend = requirements.require_independent_blend;
  enabled.tessellation_shader = requirements.require_tessellation_shader;

  // These are compatibility extensions to the renderer-ready core. They are
  // enabled opportunistically only when physical-device discovery reported
  // support, so the logical-device state never claims an unsupported bit.
  enabled.vertex_pipeline_stores_and_atomics =
      candidate.features.vertex_pipeline_stores_and_atomics;
  enabled.shader_int64 = candidate.features.shader_int64;
  enabled.texture_compression_bc = candidate.features.texture_compression_bc;
  return enabled;
}

VulkanContextCreateResult Failure(VulkanContextStatus status,
                                  std::vector<VulkanDiagnostic> diagnostics) {
  VulkanContextCreateResult result;
  result.initialization.status = status;
  result.initialization.diagnostics = std::move(diagnostics);
  return result;
}

}  // namespace

struct VulkanDeviceContext::Impl {
  explicit Impl(VulkanLoader loaded_loader) : loader(std::move(loaded_loader)) {}

  ~Impl() {
    if (device != VK_NULL_HANDLE) {
      if (device_dispatch.device_wait_idle != nullptr) {
        (void)device_dispatch.device_wait_idle(device);
      }
      if (device_dispatch.destroy_device != nullptr) {
        device_dispatch.destroy_device(device, nullptr);
      }
      device = VK_NULL_HANDLE;
      queue = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE && instance_dispatch.destroy_instance != nullptr) {
      instance_dispatch.destroy_instance(instance, nullptr);
      instance = VK_NULL_HANDLE;
      physical_device = VK_NULL_HANDLE;
    }
  }

  VulkanLoader loader;
  InstanceDispatch instance_dispatch;
  DeviceDispatch device_dispatch;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VulkanDeviceCandidate selected_candidate;
  VulkanFeatureEnablement enabled_capabilities;
  std::uint32_t queue_family_index = 0;
  std::mutex queue_mutex;
};

VulkanDeviceSelectionResult SelectVulkanDevice(
    const std::vector<VulkanDeviceCandidate>& candidates,
    const VulkanDeviceRequirements& requirements) {
  VulkanDeviceSelectionResult result;
  const VulkanDeviceCandidate* selected_candidate = nullptr;
  std::uint32_t selected_queue_family = 0;

  for (const VulkanDeviceCandidate& candidate : candidates) {
    bool accepted = true;
    if (candidate.api_version < kRequiredVulkanApiVersion) {
      AddDiagnostic(result.diagnostics, VulkanDiagnosticSeverity::kWarning,
                    VulkanDiagnosticCode::kCandidateRejectedApiVersion,
                    "Vulkan candidate '" + candidate.name + "' supports " +
                        VersionString(candidate.api_version) +
                        ", but Vulkan 1.3 is required",
                    candidate.candidate_index);
      accepted = false;
    }

    const auto queue_family = FindQueueFamily(candidate, requirements);
    if (!queue_family.has_value()) {
      AddDiagnostic(result.diagnostics, VulkanDiagnosticSeverity::kWarning,
                    VulkanDiagnosticCode::kCandidateRejectedQueue,
                    "Vulkan candidate '" + candidate.name +
                        "' has no required graphics/compute queue",
                    candidate.candidate_index);
      accepted = false;
    }

    const auto require_feature = [&](bool required, bool available,
                                     const char* name) {
      if (!required || available) {
        return;
      }
      AddDiagnostic(result.diagnostics, VulkanDiagnosticSeverity::kWarning,
                    VulkanDiagnosticCode::kCandidateRejectedFeature,
                    "Vulkan candidate '" + candidate.name +
                        " lacks required feature " + name,
                    candidate.candidate_index);
      accepted = false;
    };
    require_feature(requirements.require_dynamic_rendering,
                    candidate.features.dynamic_rendering, "dynamicRendering");
    require_feature(requirements.require_synchronization2,
                    candidate.features.synchronization2, "synchronization2");
    require_feature(requirements.require_robust_image_access,
                    candidate.features.robust_image_access, "robustImageAccess");
    require_feature(requirements.require_timeline_semaphore,
                    candidate.features.timeline_semaphore, "timelineSemaphore");
    require_feature(requirements.require_sampler_mirror_clamp_to_edge,
                    candidate.features.sampler_mirror_clamp_to_edge,
                    "samplerMirrorClampToEdge");
    require_feature(requirements.require_sample_rate_shading,
                    candidate.features.sample_rate_shading, "sampleRateShading");
    require_feature(requirements.require_fragment_stores_and_atomics,
                    candidate.features.fragment_stores_and_atomics,
                    "fragmentStoresAndAtomics");
    require_feature(requirements.require_sampler_anisotropy,
                    candidate.features.sampler_anisotropy, "samplerAnisotropy");
    require_feature(requirements.require_robust_buffer_access,
                    candidate.features.robust_buffer_access, "robustBufferAccess");
    require_feature(requirements.require_depth_bounds,
                    candidate.features.depth_bounds, "depthBounds");
    require_feature(
        requirements.require_shader_storage_image_write_without_format,
        candidate.features.shader_storage_image_write_without_format,
        "shaderStorageImageWriteWithoutFormat");
    require_feature(
        requirements.require_shader_storage_image_read_without_format,
        candidate.features.shader_storage_image_read_without_format,
        "shaderStorageImageReadWithoutFormat");
    require_feature(requirements.require_shader_image_gather_extended,
                    candidate.features.shader_image_gather_extended,
                    "shaderImageGatherExtended");
    require_feature(requirements.require_independent_blend,
                    candidate.features.independent_blend, "independentBlend");
    require_feature(requirements.require_tessellation_shader,
                    candidate.features.tessellation_shader, "tessellationShader");

    for (const std::string& extension : requirements.required_extensions) {
      if (HasExtension(candidate, extension)) {
        continue;
      }
      AddDiagnostic(result.diagnostics, VulkanDiagnosticSeverity::kWarning,
                    VulkanDiagnosticCode::kCandidateRejectedExtension,
                    "Vulkan candidate '" + candidate.name +
                        " lacks required extension " + extension,
                    candidate.candidate_index);
      accepted = false;
    }

    if (!accepted || !queue_family.has_value()) {
      continue;
    }

    AddDiagnostic(result.diagnostics, VulkanDiagnosticSeverity::kInfo,
                  VulkanDiagnosticCode::kCandidateAccepted,
                  "Vulkan candidate '" + candidate.name +
                      " passed required API, queue, feature, and extension "
                      "gates",
                  candidate.candidate_index);
    if (selected_candidate == nullptr ||
        IsBetterSelection(candidate, *queue_family, *selected_candidate,
                          selected_queue_family)) {
      selected_candidate = &candidate;
      selected_queue_family = *queue_family;
    }
  }

  if (selected_candidate == nullptr) {
    AddDiagnostic(result.diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kNoSuitableDevice,
                  "no Vulkan physical device passed the required gates");
    return result;
  }

  VulkanDeviceSelection selection;
  selection.candidate_index = selected_candidate->candidate_index;
  selection.queue_family_index = selected_queue_family;
  for (const std::string& extension : requirements.required_extensions) {
    if (std::find(selection.enabled_extensions.begin(),
                  selection.enabled_extensions.end(),
                  extension) == selection.enabled_extensions.end()) {
      selection.enabled_extensions.push_back(extension);
    }
  }
  if (HasExtension(*selected_candidate, kPortabilitySubsetExtension) &&
      std::find(selection.enabled_extensions.begin(),
                selection.enabled_extensions.end(),
                kPortabilitySubsetExtension) ==
          selection.enabled_extensions.end()) {
    // Vulkan portability implementations require this extension when it is
    // advertised. It is conditional, not an M8 universal hard requirement.
    selection.enabled_extensions.emplace_back(kPortabilitySubsetExtension);
  }
  result.selection = std::move(selection);
  return result;
}

VulkanDeviceContext::VulkanDeviceContext(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

VulkanDeviceContext::~VulkanDeviceContext() = default;

VulkanContextCreateResult VulkanDeviceContext::Create(
    const VulkanContextOptions& options) {
  std::string loader_diagnostic;
  auto loader = VulkanLoader::Open(loader_diagnostic);
  if (!loader.has_value()) {
    std::vector<VulkanDiagnostic> diagnostics;
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kLoaderUnavailable,
                  loader_diagnostic.empty() ? "Vulkan loader is unavailable"
                                            : std::move(loader_diagnostic));
    return Failure(VulkanContextStatus::kLoaderUnavailable,
                   std::move(diagnostics));
  }

  return Create(std::move(*loader), options);
}

VulkanContextCreateResult VulkanDeviceContext::Create(
    VulkanLoader loader, const VulkanContextOptions& options) {
  std::vector<VulkanDiagnostic> diagnostics;
  if (!loader.valid()) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kLoaderUnavailable,
                  "Vulkan loader has no vkGetInstanceProcAddr resolver");
    return Failure(VulkanContextStatus::kLoaderUnavailable,
                   std::move(diagnostics));
  }

  const PFN_vkGetInstanceProcAddr get_instance_proc_addr =
      loader.get_instance_proc_addr();
  const auto enumerate_instance_version =
      LoadInstanceFunction<PFN_vkEnumerateInstanceVersion>(
          get_instance_proc_addr, VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
  std::uint32_t loader_version = VK_API_VERSION_1_0;
  if (enumerate_instance_version == nullptr ||
      enumerate_instance_version(&loader_version) != VK_SUCCESS) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kLoaderVersionUnsupported,
                  "Vulkan loader cannot report Vulkan 1.3 support");
    return Failure(VulkanContextStatus::kLoaderApiVersionUnsupported,
                   std::move(diagnostics));
  }
  if (loader_version < kRequiredVulkanApiVersion) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kLoaderVersionUnsupported,
                  "Vulkan loader supports " + VersionString(loader_version) +
                      ", but Vulkan 1.3 is required");
    return Failure(VulkanContextStatus::kLoaderApiVersionUnsupported,
                   std::move(diagnostics));
  }
  AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kInfo,
                VulkanDiagnosticCode::kLoaderOpened,
                "Vulkan loader supports API " + VersionString(loader_version) +
                    " through vkGetInstanceProcAddr");

  const auto create_instance = LoadInstanceFunction<PFN_vkCreateInstance>(
      get_instance_proc_addr, VK_NULL_HANDLE, "vkCreateInstance");
  if (create_instance == nullptr) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kGlobalFunctionUnavailable,
                  "Vulkan loader is missing vkCreateInstance");
    return Failure(VulkanContextStatus::kInstanceFunctionUnavailable,
                   std::move(diagnostics));
  }

  auto impl = std::make_unique<Impl>(std::move(loader));
  const char* application_name =
      options.application_name.empty() ? "KajPS5" : options.application_name.c_str();
  VkApplicationInfo application_info{};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pApplicationName = application_name;
  application_info.applicationVersion = 1;
  application_info.pEngineName = "KajPS5";
  application_info.engineVersion = 1;
  application_info.apiVersion = kRequiredVulkanApiVersion;
  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &application_info;

  const VkResult instance_result =
      create_instance(&instance_info, nullptr, &impl->instance);
  if (instance_result != VK_SUCCESS) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kInstanceCreationFailed,
                  "vkCreateInstance failed", std::nullopt, instance_result);
    return Failure(VulkanContextStatus::kInstanceCreationFailed,
                   std::move(diagnostics));
  }

  if (!LoadInstanceDispatch(get_instance_proc_addr, impl->instance,
                            impl->instance_dispatch, diagnostics)) {
    return Failure(VulkanContextStatus::kInstanceFunctionUnavailable,
                   std::move(diagnostics));
  }

  std::vector<VkPhysicalDevice> physical_devices;
  const VkResult device_enumeration_result =
      EnumerateVulkan<VkPhysicalDevice>(
          [&](std::uint32_t* count, VkPhysicalDevice* values) {
            return impl->instance_dispatch.enumerate_physical_devices(
                impl->instance, count, values);
          },
          physical_devices);
  if (device_enumeration_result != VK_SUCCESS || physical_devices.empty()) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed,
                  device_enumeration_result == VK_SUCCESS
                      ? "Vulkan instance reported no physical devices"
                      : "vkEnumeratePhysicalDevices failed",
                  std::nullopt, device_enumeration_result);
    return Failure(VulkanContextStatus::kPhysicalDeviceEnumerationFailed,
                   std::move(diagnostics));
  }

  std::vector<VulkanDeviceCandidate> candidates;
  candidates.reserve(physical_devices.size());
  for (std::size_t index = 0; index < physical_devices.size(); ++index) {
    VulkanDeviceCandidate candidate;
    if (!DiscoverCandidate(impl->instance_dispatch, physical_devices[index],
                           index, candidate, diagnostics)) {
      return Failure(VulkanContextStatus::kPhysicalDeviceEnumerationFailed,
                     std::move(diagnostics));
    }
    candidates.push_back(std::move(candidate));
  }

  VulkanDeviceSelectionResult selection =
      SelectVulkanDevice(candidates, options.requirements);
  diagnostics.insert(diagnostics.end(),
                     std::make_move_iterator(selection.diagnostics.begin()),
                     std::make_move_iterator(selection.diagnostics.end()));
  if (!selection.selection.has_value()) {
    return Failure(VulkanContextStatus::kNoSuitableDevice,
                   std::move(diagnostics));
  }

  const VulkanDeviceSelection& selected = *selection.selection;
  if (selected.candidate_index >= physical_devices.size()) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kNoSuitableDevice,
                  "selected Vulkan candidate index is outside discovery data");
    return Failure(VulkanContextStatus::kNoSuitableDevice,
                   std::move(diagnostics));
  }
  impl->physical_device = physical_devices[selected.candidate_index];
  impl->selected_candidate = candidates[selected.candidate_index];
  impl->queue_family_index = selected.queue_family_index;
  AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kInfo,
                VulkanDiagnosticCode::kSelectedDevice,
                "selected Vulkan device '" + impl->selected_candidate.name +
                    "' (" + VulkanPhysicalDeviceTypeName(
                                      impl->selected_candidate.type) +
                    ", API " +
                    VersionString(impl->selected_candidate.api_version) +
                    ") queue family " +
                    std::to_string(impl->queue_family_index),
                impl->selected_candidate.candidate_index);
  AddOptionalCapabilityDiagnostics(impl->selected_candidate, diagnostics);

  const float queue_priority = 1.0F;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = impl->queue_family_index;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &queue_priority;

  impl->enabled_capabilities =
      BuildEnabledCapabilities(impl->selected_candidate, options.requirements);
  const VulkanFeatureEnablement& enabled = impl->enabled_capabilities;

  VkPhysicalDeviceVulkan12Features enabled_features12{};
  enabled_features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  enabled_features12.timelineSemaphore =
      enabled.timeline_semaphore ? VK_TRUE : VK_FALSE;
  enabled_features12.samplerMirrorClampToEdge =
      enabled.sampler_mirror_clamp_to_edge ? VK_TRUE : VK_FALSE;
  VkPhysicalDeviceVulkan13Features enabled_features13{};
  enabled_features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  enabled_features13.pNext = &enabled_features12;
  enabled_features13.dynamicRendering =
      enabled.dynamic_rendering ? VK_TRUE : VK_FALSE;
  enabled_features13.synchronization2 =
      enabled.synchronization2 ? VK_TRUE : VK_FALSE;
  enabled_features13.robustImageAccess =
      enabled.robust_image_access ? VK_TRUE : VK_FALSE;
  VkPhysicalDeviceFeatures2 enabled_features{};
  enabled_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  enabled_features.pNext = &enabled_features13;
  enabled_features.features.sampleRateShading =
      enabled.sample_rate_shading ? VK_TRUE : VK_FALSE;
  enabled_features.features.fragmentStoresAndAtomics =
      enabled.fragment_stores_and_atomics ? VK_TRUE : VK_FALSE;
  enabled_features.features.samplerAnisotropy =
      enabled.sampler_anisotropy ? VK_TRUE : VK_FALSE;
  enabled_features.features.robustBufferAccess =
      enabled.robust_buffer_access ? VK_TRUE : VK_FALSE;
  enabled_features.features.depthBounds = enabled.depth_bounds ? VK_TRUE : VK_FALSE;
  enabled_features.features.shaderStorageImageWriteWithoutFormat =
      enabled.shader_storage_image_write_without_format ? VK_TRUE : VK_FALSE;
  enabled_features.features.shaderStorageImageReadWithoutFormat =
      enabled.shader_storage_image_read_without_format ? VK_TRUE : VK_FALSE;
  enabled_features.features.shaderImageGatherExtended =
      enabled.shader_image_gather_extended ? VK_TRUE : VK_FALSE;
  enabled_features.features.independentBlend =
      enabled.independent_blend ? VK_TRUE : VK_FALSE;
  enabled_features.features.tessellationShader =
      enabled.tessellation_shader ? VK_TRUE : VK_FALSE;
  enabled_features.features.vertexPipelineStoresAndAtomics =
      enabled.vertex_pipeline_stores_and_atomics ? VK_TRUE : VK_FALSE;
  enabled_features.features.shaderInt64 = enabled.shader_int64 ? VK_TRUE : VK_FALSE;
  enabled_features.features.textureCompressionBC =
      enabled.texture_compression_bc ? VK_TRUE : VK_FALSE;

  std::vector<const char*> enabled_extension_names;
  enabled_extension_names.reserve(selected.enabled_extensions.size());
  for (const std::string& extension : selected.enabled_extensions) {
    enabled_extension_names.push_back(extension.c_str());
  }
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = &enabled_features;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.enabledExtensionCount =
      static_cast<std::uint32_t>(enabled_extension_names.size());
  device_info.ppEnabledExtensionNames = enabled_extension_names.empty()
                                            ? nullptr
                                            : enabled_extension_names.data();

  const VkResult device_result = impl->instance_dispatch.create_device(
      impl->physical_device, &device_info, nullptr, &impl->device);
  if (device_result != VK_SUCCESS) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kDeviceCreationFailed,
                  "vkCreateDevice failed for selected Vulkan device",
                  impl->selected_candidate.candidate_index, device_result);
    return Failure(VulkanContextStatus::kDeviceCreationFailed,
                   std::move(diagnostics));
  }

  if (!LoadDeviceDispatch(impl->instance_dispatch, impl->device,
                          impl->device_dispatch, diagnostics)) {
    return Failure(VulkanContextStatus::kDeviceFunctionUnavailable,
                   std::move(diagnostics));
  }
  impl->device_dispatch.get_device_queue(impl->device, impl->queue_family_index,
                                         0, &impl->queue);
  if (impl->queue == VK_NULL_HANDLE) {
    AddDiagnostic(diagnostics, VulkanDiagnosticSeverity::kError,
                  VulkanDiagnosticCode::kQueueUnavailable,
                  "selected Vulkan queue family returned no queue",
                  impl->selected_candidate.candidate_index);
    return Failure(VulkanContextStatus::kQueueUnavailable,
                   std::move(diagnostics));
  }

  VulkanContextCreateResult result;
  result.initialization.status = VulkanContextStatus::kOk;
  result.initialization.diagnostics = std::move(diagnostics);
  result.context = std::unique_ptr<VulkanDeviceContext>(
      new VulkanDeviceContext(std::move(impl)));
  return result;
}

const VulkanDeviceCandidate& VulkanDeviceContext::properties() const noexcept {
  return impl_->selected_candidate;
}

const VulkanFeatureSupport& VulkanDeviceContext::capabilities() const noexcept {
  return impl_->selected_candidate.features;
}

const VulkanFeatureSupport& VulkanDeviceContext::supported_capabilities() const
    noexcept {
  return impl_->selected_candidate.features;
}

const VulkanFeatureEnablement& VulkanDeviceContext::enabled_capabilities() const
    noexcept {
  return impl_->enabled_capabilities;
}

std::uint32_t VulkanDeviceContext::queue_family_index() const noexcept {
  return impl_->queue_family_index;
}

VkInstance VulkanDeviceContext::instance() const noexcept {
  return impl_->instance;
}

VkPhysicalDevice VulkanDeviceContext::physical_device() const noexcept {
  return impl_->physical_device;
}

VkDevice VulkanDeviceContext::device() const noexcept {
  return impl_->device;
}

VkQueue VulkanDeviceContext::queue() const noexcept {
  return impl_->queue;
}

std::mutex& VulkanDeviceContext::queue_mutex() noexcept {
  return impl_->queue_mutex;
}

const char* VulkanContextStatusName(VulkanContextStatus status) noexcept {
  switch (status) {
    case VulkanContextStatus::kOk:
      return "ok";
    case VulkanContextStatus::kLoaderUnavailable:
      return "loader_unavailable";
    case VulkanContextStatus::kLoaderApiVersionUnsupported:
      return "loader_api_version_unsupported";
    case VulkanContextStatus::kInstanceCreationFailed:
      return "instance_creation_failed";
    case VulkanContextStatus::kInstanceFunctionUnavailable:
      return "instance_function_unavailable";
    case VulkanContextStatus::kPhysicalDeviceEnumerationFailed:
      return "physical_device_enumeration_failed";
    case VulkanContextStatus::kNoSuitableDevice:
      return "no_suitable_device";
    case VulkanContextStatus::kDeviceCreationFailed:
      return "device_creation_failed";
    case VulkanContextStatus::kDeviceFunctionUnavailable:
      return "device_function_unavailable";
    case VulkanContextStatus::kQueueUnavailable:
      return "queue_unavailable";
    case VulkanContextStatus::kAlreadyInitialized:
      return "already_initialized";
  }
  return "unknown";
}

const char* VulkanDiagnosticCodeName(VulkanDiagnosticCode code) noexcept {
  switch (code) {
    case VulkanDiagnosticCode::kLoaderOpened:
      return "loader_opened";
    case VulkanDiagnosticCode::kLoaderUnavailable:
      return "loader_unavailable";
    case VulkanDiagnosticCode::kLoaderVersionUnsupported:
      return "loader_version_unsupported";
    case VulkanDiagnosticCode::kGlobalFunctionUnavailable:
      return "global_function_unavailable";
    case VulkanDiagnosticCode::kInstanceCreationFailed:
      return "instance_creation_failed";
    case VulkanDiagnosticCode::kInstanceFunctionUnavailable:
      return "instance_function_unavailable";
    case VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed:
      return "physical_device_enumeration_failed";
    case VulkanDiagnosticCode::kCandidateRejectedApiVersion:
      return "candidate_rejected_api_version";
    case VulkanDiagnosticCode::kCandidateRejectedQueue:
      return "candidate_rejected_queue";
    case VulkanDiagnosticCode::kCandidateRejectedFeature:
      return "candidate_rejected_feature";
    case VulkanDiagnosticCode::kCandidateRejectedExtension:
      return "candidate_rejected_extension";
    case VulkanDiagnosticCode::kCandidateAccepted:
      return "candidate_accepted";
    case VulkanDiagnosticCode::kNoSuitableDevice:
      return "no_suitable_device";
    case VulkanDiagnosticCode::kSelectedDevice:
      return "selected_device";
    case VulkanDiagnosticCode::kOptionalCapabilityUnavailable:
      return "optional_capability_unavailable";
    case VulkanDiagnosticCode::kDeviceCreationFailed:
      return "device_creation_failed";
    case VulkanDiagnosticCode::kDeviceFunctionUnavailable:
      return "device_function_unavailable";
    case VulkanDiagnosticCode::kQueueUnavailable:
      return "queue_unavailable";
    case VulkanDiagnosticCode::kDuplicateInitialization:
      return "duplicate_initialization";
  }
  return "unknown";
}

const char* VulkanPhysicalDeviceTypeName(VulkanPhysicalDeviceType type) noexcept {
  switch (type) {
    case VulkanPhysicalDeviceType::kOther:
      return "other";
    case VulkanPhysicalDeviceType::kIntegratedGpu:
      return "integrated";
    case VulkanPhysicalDeviceType::kDiscreteGpu:
      return "discrete";
    case VulkanPhysicalDeviceType::kVirtualGpu:
      return "virtual";
    case VulkanPhysicalDeviceType::kCpu:
      return "cpu";
  }
  return "unknown";
}

}  // namespace kajps5::gpu::vulkan
