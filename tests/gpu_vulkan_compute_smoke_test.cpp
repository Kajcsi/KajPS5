// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/{commandScheduler,masterSemaphore,render}.*
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace {

namespace vk = kajps5::gpu::vulkan;

constexpr std::uint32_t kSyntheticEndProgram = 0xbf810000U;

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
      return HasDiagnostic(initialization,
                           vk::VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed,
                           VK_SUCCESS,
                           "Vulkan instance reported no physical devices");
    case vk::VulkanContextStatus::kNoSuitableDevice:
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

void PrintInitializationDiagnostics(
    const vk::VulkanInitializationResult& initialization, std::ostream& output) {
  for (const auto& diagnostic : initialization.diagnostics) {
    output << "  " << vk::VulkanDiagnosticCodeName(diagnostic.code) << ": "
           << diagnostic.message << '\n';
  }
}

void PrintExecutionDiagnostics(const vk::VulkanComputeResult& result,
                               std::ostream& output) {
  for (const auto& diagnostic : result.diagnostics) {
    output << "  " << vk::VulkanComputeDiagnosticCodeName(diagnostic.code)
           << ": " << diagnostic.message << '\n';
  }
}

}  // namespace

int main() {
  kajps5::memory::GuestMemory memory{
      0x700000, 0x1000,
      kajps5::memory::GuestMemoryProtection::kRead |
          kajps5::memory::GuestMemoryProtection::kWrite};
  kajps5::gpu::GpuRuntime runtime{memory};
  const auto initialization = runtime.InitializeVulkan();
  if (!initialization) {
    const bool unavailable_or_unsupported =
        IsUnavailableOrUnsupportedHost(initialization);
    std::ostream& output = unavailable_or_unsupported ? std::cout : std::cerr;
    output << (unavailable_or_unsupported ? "SKIP" : "FAIL")
           << ": Vulkan compute smoke selection status="
           << vk::VulkanContextStatusName(initialization.status) << '\n';
    PrintInitializationDiagnostics(initialization, output);
    return unavailable_or_unsupported ? 77 : 1;
  }

  kajps5::gpu::shader::recompiler::CompileOptions options;
  options.stage = kajps5::gpu::ShaderType::Compute;
  options.dump_ir = false;
  kajps5::gpu::shader::recompiler::CompileResult compiled;
  std::string compile_error;
  const std::vector<std::uint32_t> synthetic_compute = {kSyntheticEndProgram};
  if (!kajps5::gpu::shader::recompiler::TryRecompile(
          synthetic_compute, options, compiled, &compile_error) ||
      compiled.spirv.empty()) {
    std::cerr << "FAIL: public synthetic compute shader recompilation failed: "
              << compile_error << '\n';
    return 1;
  }

  const auto submitted = runtime.SubmitVulkanCompute(
      compiled.spirv, 1, 1, 1, vk::kDefaultVulkanComputeFenceWaitNanoseconds);
  if (!submitted) {
    std::cerr << "FAIL: Vulkan compute execution status="
              << vk::VulkanComputeStatusName(submitted.status) << '\n';
    PrintExecutionDiagnostics(submitted, std::cerr);
    return 1;
  }

  const auto* context = runtime.vulkan_context();
  if (context == nullptr || !runtime.has_vulkan_compute_execution()) {
    std::cerr << "FAIL: runtime did not retain its selected context and "
                 "compute owner\n";
    return 1;
  }
  const auto& properties = context->properties();
  std::cout << "Vulkan compute smoke selected device=\"" << properties.name
            << "\" api=" << VK_VERSION_MAJOR(properties.api_version) << '.'
            << VK_VERSION_MINOR(properties.api_version) << '.'
            << VK_VERSION_PATCH(properties.api_version)
            << " queue_family=" << context->queue_family_index()
            << " dispatch=1/1/1 timeline=" << submitted.timeline << '\n';
  return 0;
}
