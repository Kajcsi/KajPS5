// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/cache/bufferCache.*
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/shader/bindings.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace {

namespace vk = kajps5::gpu::vulkan;
namespace recomp = kajps5::gpu::shader::recompiler;

constexpr std::uint32_t InlineU32(std::uint32_t value) { return 128U + value; }

constexpr std::uint32_t EncodeSopp(std::uint32_t opcode,
                                   std::uint32_t simm = 0) {
  return 0x80000000U | (0x7fU << 23U) | ((opcode & 0x7fU) << 16U) |
         (simm & 0xffffU);
}

constexpr std::uint32_t EncodeVop1(std::uint32_t opcode, std::uint32_t dst,
                                   std::uint32_t src0) {
  return (0x3fU << 25U) | ((dst & 0xffU) << 17U) | ((opcode & 0xffU) << 9U) |
         (src0 & 0x1ffU);
}

constexpr std::uint32_t EncodeMubuf0(std::uint32_t opcode,
                                     std::uint32_t offset = 0,
                                     bool idxen = false, bool offen = true) {
  return (0x38U << 26U) | ((opcode & 0x7fU) << 18U) |
         (offen ? (1U << 12U) : 0U) | (idxen ? (1U << 13U) : 0U) |
         (offset & 0xfffU);
}

constexpr std::uint32_t EncodeMubuf1(std::uint32_t vdata, std::uint32_t srsrc,
                                     std::uint32_t vaddr,
                                     std::uint32_t soffset = 128) {
  return ((soffset & 0xffU) << 24U) | ((srsrc & 0x1fU) << 16U) |
         ((vdata & 0xffU) << 8U) | (vaddr & 0xffU);
}

constexpr std::uint32_t EndProgram() { return EncodeSopp(0x01U); }

bool HasDiagnostic(const vk::VulkanInitializationResult &initialization,
                   vk::VulkanDiagnosticCode code,
                   std::int32_t api_result = VK_SUCCESS,
                   const char *message = nullptr) {
  return std::any_of(
      initialization.diagnostics.begin(), initialization.diagnostics.end(),
      [&](const vk::VulkanDiagnostic &diagnostic) {
        return diagnostic.code == code && diagnostic.api_result == api_result &&
               (message == nullptr || diagnostic.message == message);
      });
}

bool IsUnavailableOrUnsupportedHost(
    const vk::VulkanInitializationResult &initialization) {
  switch (initialization.status) {
  case vk::VulkanContextStatus::kLoaderUnavailable:
  case vk::VulkanContextStatus::kLoaderApiVersionUnsupported:
    return true;
  case vk::VulkanContextStatus::kInstanceCreationFailed:
    return HasDiagnostic(
        initialization, vk::VulkanDiagnosticCode::kInstanceCreationFailed,
        static_cast<std::int32_t>(VK_ERROR_INCOMPATIBLE_DRIVER));
  case vk::VulkanContextStatus::kPhysicalDeviceEnumerationFailed:
    return HasDiagnostic(
        initialization,
        vk::VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed, VK_SUCCESS,
        "Vulkan instance reported no physical devices");
  case vk::VulkanContextStatus::kNoSuitableDevice:
    return HasDiagnostic(
        initialization, vk::VulkanDiagnosticCode::kNoSuitableDevice, VK_SUCCESS,
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
    const vk::VulkanInitializationResult &initialization,
    std::ostream &output) {
  for (const auto &diagnostic : initialization.diagnostics) {
    output << "  " << vk::VulkanDiagnosticCodeName(diagnostic.code) << ": "
           << diagnostic.message << '\n';
  }
}

void PrintExecutionDiagnostics(const vk::VulkanComputeResult &result,
                               std::ostream &output) {
  for (const auto &diagnostic : result.diagnostics) {
    output << "  " << vk::VulkanComputeDiagnosticCodeName(diagnostic.code)
           << ": " << diagnostic.message << '\n';
  }
}

void SetBufferDescriptor(std::array<std::uint32_t, 64> &user_data,
                         std::size_t sgpr, std::uint32_t address) {
  kajps5::gpu::ShaderBufferResource descriptor;
  descriptor.UpdateAddress48(address);
  descriptor.fields[1] |= 4U << 16U;
  descriptor.fields[2] = 4;
  std::copy(std::begin(descriptor.fields), std::end(descriptor.fields),
            user_data.begin() + sgpr);
}

template <typename T, std::size_t N>
std::span<const std::byte> AsBytes(const std::array<T, N> &values) {
  return std::as_bytes(std::span(values));
}

template <typename T, std::size_t N>
std::span<std::byte> AsWritableBytes(std::array<T, N> &values) {
  return std::as_writable_bytes(std::span(values));
}

bool IsExpectedGuestBufferProgram(const recomp::CompileResult &compiled) {
  if (compiled.spirv.empty() || !compiled.program.resource_tracking_complete ||
      !compiled.program.shader_info_complete ||
      !compiled.program.binding_layout_complete ||
      compiled.resources.buffers.size() != 2 ||
      compiled.program.info.buffers.size() != 2 ||
      compiled.program.bindings.descriptors.size() != 1) {
    return false;
  }
  const auto readable = std::count_if(
      compiled.program.info.buffers.begin(),
      compiled.program.info.buffers.end(),
      [](const auto &buffer) { return buffer.read && !buffer.written; });
  const auto writable =
      std::count_if(compiled.program.info.buffers.begin(),
                    compiled.program.info.buffers.end(),
                    [](const auto &buffer) { return buffer.written; });
  return readable == 1 && writable == 1;
}

} // namespace

int main() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  constexpr std::uint32_t kInputAddress = 0x700104U;
  constexpr std::uint32_t kOutputAddress = 0x700204U;
  constexpr std::uint32_t kExpectedValue = 0x13579bdfU;
  const std::array<std::uint32_t, 4> input = {kExpectedValue, 0x11223344U,
                                              0x55667788U, 0x99aabbccU};
  const std::array<std::uint32_t, 4> output = {0U, 0x2468ace0U, 0xdeadbeefU,
                                               0x0badf00dU};
  const std::array<std::uint32_t, 1> before_input = {0xa1a2a3a4U};
  const std::array<std::uint32_t, 1> after_input = {0xb1b2b3b4U};
  const std::array<std::uint32_t, 1> before_output = {0xc1c2c3c4U};
  const std::array<std::uint32_t, 1> after_output = {0xd1d2d3d4U};

  kajps5::memory::GuestMemory memory{0x700000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite};
  if (!memory.Initialize(kInputAddress - sizeof(before_input),
                         AsBytes(before_input)) ||
      !memory.Initialize(kInputAddress, AsBytes(input)) ||
      !memory.Initialize(kInputAddress + sizeof(input), AsBytes(after_input)) ||
      !memory.Initialize(kOutputAddress - sizeof(before_output),
                         AsBytes(before_output)) ||
      !memory.Initialize(kOutputAddress, AsBytes(output)) ||
      !memory.Initialize(kOutputAddress + sizeof(output),
                         AsBytes(after_output))) {
    std::cerr << "FAIL: guest-buffer smoke fixture initialization failed\n";
    return 1;
  }

  std::array<std::uint32_t, 64> user_data{};
  SetBufferDescriptor(user_data, 0, kInputAddress);
  // MUBUF SRSRC encodes the four-SGPR tuple index: srsrc=12 resolves S48:S51.
  SetBufferDescriptor(user_data, 48, kOutputAddress);
  const std::vector<std::uint32_t> program = {
      EncodeVop1(0x01U, 30, InlineU32(0)),
      EncodeMubuf0(0x0cU),
      EncodeMubuf1(0, 0, 30),
      EncodeVop1(0x01U, 31, InlineU32(0)),
      EncodeMubuf0(0x1cU),
      EncodeMubuf1(0, 12, 31),
      EndProgram()};
  recomp::CompileOptions options;
  options.stage = kajps5::gpu::ShaderType::Compute;
  options.dump_ir = false;
  options.user_data = user_data.data();
  recomp::CompileResult compiled;
  std::string compile_error;
  if (!recomp::TryRecompile(program, options, compiled, &compile_error)) {
    std::cerr << "FAIL: public MUBUF load/store recompilation failed: "
              << compile_error << '\n';
    return 1;
  }
  if (!IsExpectedGuestBufferProgram(compiled)) {
    std::cerr << "FAIL: public MUBUF load/store did not produce two complete "
                 "guest-buffer bindings\n";
    return 1;
  }

  kajps5::gpu::GpuRuntime runtime{memory};
  const auto initialization = runtime.InitializeVulkan();
  if (!initialization) {
    const bool unavailable_or_unsupported =
        IsUnavailableOrUnsupportedHost(initialization);
    std::ostream &stream = unavailable_or_unsupported ? std::cout : std::cerr;
    stream << (unavailable_or_unsupported ? "SKIP" : "FAIL")
           << ": Vulkan guest-buffer smoke selection status="
           << vk::VulkanContextStatusName(initialization.status) << '\n';
    PrintInitializationDiagnostics(initialization, stream);
    return unavailable_or_unsupported ? 77 : 1;
  }

  const auto submitted = runtime.SubmitVulkanTranslatedCompute(
      compiled, 1, 1, 1, vk::kDefaultVulkanComputeFenceWaitNanoseconds);
  if (!submitted) {
    std::cerr << "FAIL: translated Vulkan guest-buffer submission status="
              << vk::VulkanComputeStatusName(submitted.status) << '\n';
    PrintExecutionDiagnostics(submitted, std::cerr);
    return 1;
  }

  std::array<std::uint32_t, 4> read_input{};
  std::array<std::uint32_t, 4> read_output{};
  std::array<std::uint32_t, 1> read_before_input{};
  std::array<std::uint32_t, 1> read_after_input{};
  std::array<std::uint32_t, 1> read_before_output{};
  std::array<std::uint32_t, 1> read_after_output{};
  if (!memory.Read(kInputAddress, AsWritableBytes(read_input)) ||
      !memory.Read(kOutputAddress, AsWritableBytes(read_output)) ||
      !memory.Read(kInputAddress - sizeof(read_before_input),
                   AsWritableBytes(read_before_input)) ||
      !memory.Read(kInputAddress + sizeof(input),
                   AsWritableBytes(read_after_input)) ||
      !memory.Read(kOutputAddress - sizeof(read_before_output),
                   AsWritableBytes(read_before_output)) ||
      !memory.Read(kOutputAddress + sizeof(output),
                   AsWritableBytes(read_after_output)) ||
      read_input != input || read_output[0] != kExpectedValue ||
      !std::equal(output.begin() + 1, output.end(), read_output.begin() + 1) ||
      read_before_input != before_input || read_after_input != after_input ||
      read_before_output != before_output ||
      read_after_output != after_output) {
    std::cerr << "FAIL: translated guest-buffer copy/readback mismatch input=0x"
              << std::hex << read_input[0] << " output=0x" << read_output[0]
              << std::dec << '\n';
    return 1;
  }

  const auto *context = runtime.vulkan_context();
  if (context == nullptr || !runtime.has_vulkan_compute_execution()) {
    std::cerr << "FAIL: guest-buffer smoke did not retain the Vulkan execution "
                 "context\n";
    return 1;
  }
  const auto &properties = context->properties();
  std::cout << "Vulkan guest-buffer smoke selected device=\"" << properties.name
            << "\" api=" << VK_VERSION_MAJOR(properties.api_version) << '.'
            << VK_VERSION_MINOR(properties.api_version) << '.'
            << VK_VERSION_PATCH(properties.api_version)
            << " queue_family=" << context->queue_family_index()
            << " dispatch=1/1/1 timeline=" << submitted.timeline
            << " bindings=" << compiled.program.bindings.descriptors.size()
            << " views=" << compiled.resources.buffers.size() << " input=0x"
            << std::hex << read_input[0] << " output=0x" << read_output[0]
            << std::dec << '\n';
  return 0;
}
