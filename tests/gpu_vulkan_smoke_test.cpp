// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/graphics/host_gpu/vulkanInstance.h and
// src/graphics/presentation/window/vulkanWindow.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <cstdint>
#include <iostream>

#include "gpu/vulkan/device.h"

namespace {

namespace vk = kajps5::gpu::vulkan;

bool HasDiagnostic(const vk::VulkanInitializationResult& initialization,
                   vk::VulkanDiagnosticCode code,
                   std::int32_t api_result = VK_SUCCESS,
                   const char* message = nullptr) {
  return std::any_of(initialization.diagnostics.begin(),
                     initialization.diagnostics.end(),
                     [&](const vk::VulkanDiagnostic& diagnostic) {
                       return diagnostic.code == code &&
                              diagnostic.api_result == api_result &&
                              (message == nullptr || diagnostic.message == message);
                     });
}

bool IsUnavailableOrUnsupportedHost(
    const vk::VulkanInitializationResult& initialization) {
  switch (initialization.status) {
    case vk::VulkanContextStatus::kLoaderUnavailable:
    case vk::VulkanContextStatus::kLoaderApiVersionUnsupported:
      return true;
    case vk::VulkanContextStatus::kInstanceCreationFailed:
      return HasDiagnostic(initialization,
                           vk::VulkanDiagnosticCode::kInstanceCreationFailed,
                           static_cast<std::int32_t>(
                               VK_ERROR_INCOMPATIBLE_DRIVER));
    case vk::VulkanContextStatus::kPhysicalDeviceEnumerationFailed:
      // A successful enumeration with no devices is an unsupported host. An
      // API failure is an implementation/driver failure and must remain red.
      return HasDiagnostic(initialization,
                           vk::VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed,
                           VK_SUCCESS,
                           "Vulkan instance reported no physical devices");
    case vk::VulkanContextStatus::kNoSuitableDevice:
      // This status is only skipped when selection itself found no device that
      // satisfies the renderer-ready baseline. Failures after selection use
      // distinct statuses below and intentionally fail the smoke test.
      return HasDiagnostic(initialization, vk::VulkanDiagnosticCode::kNoSuitableDevice,
                           VK_SUCCESS,
                           "no Vulkan physical device passed the required gates");
    case vk::VulkanContextStatus::kOk:
    case vk::VulkanContextStatus::kInstanceFunctionUnavailable:
    case vk::VulkanContextStatus::kDeviceCreationFailed:
    case vk::VulkanContextStatus::kDeviceFunctionUnavailable:
    case vk::VulkanContextStatus::kQueueUnavailable:
    case vk::VulkanContextStatus::kAlreadyInitialized:
      return false;
  }
  return false;
}

void PrintDiagnostics(const vk::VulkanInitializationResult& initialization,
                      std::ostream& output) {
  for (const auto& diagnostic : initialization.diagnostics) {
    output << "  " << vk::VulkanDiagnosticCodeName(diagnostic.code) << ": "
           << diagnostic.message << '\n';
  }
}

}  // namespace

int main() {
  auto created = kajps5::gpu::vulkan::VulkanDeviceContext::Create();
  if (!created) {
    const bool unavailable_or_unsupported =
        IsUnavailableOrUnsupportedHost(created.initialization);
    std::ostream& output = unavailable_or_unsupported ? std::cout : std::cerr;
    output << (unavailable_or_unsupported ? "SKIP" : "FAIL")
           << ": Vulkan smoke status="
           << vk::VulkanContextStatusName(created.initialization.status) << '\n';
    PrintDiagnostics(created.initialization, output);
    if (unavailable_or_unsupported) {
      return 77;
    }
    return 1;
  }

  const auto& properties = created.context->properties();
  std::cout << "Vulkan smoke selected device=\"" << properties.name
            << "\" api=" << VK_VERSION_MAJOR(properties.api_version) << '.'
            << VK_VERSION_MINOR(properties.api_version) << '.'
            << VK_VERSION_PATCH(properties.api_version)
            << " queue_family=" << created.context->queue_family_index()
            << " vendor=0x" << std::hex << properties.vendor_id << std::dec
            << '\n';
  if (created.context->instance() == VK_NULL_HANDLE ||
      created.context->physical_device() == VK_NULL_HANDLE ||
      created.context->device() == VK_NULL_HANDLE ||
      created.context->queue() == VK_NULL_HANDLE) {
    std::cerr << "Vulkan smoke returned an incomplete context\n";
    return 1;
  }
  return 0;
}
