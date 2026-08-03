// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 renderer/pipeline descriptors and renderCompute.
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project,
// Vulkan guest-image write tracking.
// SPDX-License-Identifier: GPL-2.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace {
namespace vk = kajps5::gpu::vulkan;
namespace ir = kajps5::gpu::shader::recompiler::IR;

bool IsUnavailable(const vk::VulkanInitializationResult& result) {
  using S = vk::VulkanContextStatus;
  if (result.status == S::kLoaderUnavailable ||
      result.status == S::kLoaderApiVersionUnsupported) return true;
  if (result.status == S::kPhysicalDeviceEnumerationFailed) {
    for (const auto& diagnostic : result.diagnostics) {
      if (diagnostic.code == vk::VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed &&
          diagnostic.api_result == VK_SUCCESS) return true;
    }
  }
  if (result.status == S::kNoSuitableDevice) return true;
  if (result.status == S::kInstanceCreationFailed) {
    for (const auto& diagnostic : result.diagnostics) {
      if (diagnostic.api_result == VK_ERROR_INCOMPATIBLE_DRIVER) return true;
    }
  }
  return false;
}

// A compact, deterministic GLSL450 compute module which stores one red UNORM
// texel at (0, 0) through set 0/binding 0. CompileResult metadata mirrors the
// module exactly, so the public runtime entry remains the only binding path.
kajps5::gpu::shader::recompiler::CompileResult StorageImageProgram() {
  kajps5::gpu::shader::recompiler::CompileResult result;
  result.spirv = {
      0x07230203, 0x00010300, 0, 18, 0,
      0x00020011, 1,                         // OpCapability Shader
      0x00020011, 56,                        // StorageImageWriteWithoutFormat
      0x0003000e, 0, 1,                      // OpMemoryModel Logical GLSL450
      0x0005000f, 5, 15, 0x6e69616d, 0,      // OpEntryPoint GLCompute main
      0x00060010, 15, 17, 1, 1, 1,           // OpExecutionMode LocalSize
      0x00040047, 9, 34, 0,                  // DescriptorSet 0
      0x00040047, 9, 33, 0,                  // Binding 0
      0x00020013, 1,                         // void
      0x00030021, 2, 1,                      // function void()
      0x00030016, 3, 32,                     // float
      0x00040015, 4, 32, 1,                  // signed int
      0x00040017, 5, 4, 2,                   // ivec2
      0x00040017, 6, 3, 4,                   // vec4
      0x00090019, 7, 3, 1, 0, 0, 0, 2, 0,    // image2D storage, unknown format
      0x00040020, 8, 0, 7,                   // ptr UniformConstant image
      0x0004003b, 8, 9, 0,                   // image variable
      0x0004002b, 4, 10, 0,                  // int 0
      0x0005002c, 5, 11, 10, 10,             // ivec2(0,0)
      0x0004002b, 3, 12, 0,                  // 0.0
      0x0004002b, 3, 13, 0x3f800000,         // 1.0
      0x0007002c, 6, 14, 13, 12, 12, 13,     // vec4(1,0,0,1)
      0x00050036, 1, 15, 0, 2,               // main
      0x000200f8, 16,
      0x0004003d, 7, 17, 9,                  // OpLoad image
      0x00040063, 17, 11, 14,                // OpImageWrite image, coord, texel
      0x000100fd,
      0x00010038,
  };
  auto& program = result.program;
  program.stage = kajps5::gpu::ShaderType::Compute;
  program.resource_tracking_complete = true;
  program.shader_info_complete = true;
  program.binding_layout_complete = true;
  program.bindings.descriptor_set = 0;
  program.info.images.push_back({
      .source = 0,
      .kind = ir::ResourceKind::StorageImage,
      .dimension = kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2D,
      .storage_swizzle = 0,
      .read = false,
      .written = true,
  });
  program.bindings.descriptors.push_back(
      {ir::DescriptorBindingKind::Storage2D, 0, {0}});
  ir::DescriptorValue image;
  image.dword_count = 8;
  image.dwords[0] = 0x80;  // guest address 0x8000 in 256-byte units
  image.dwords[1] = kajps5::gpu::Prospero::GpuEnumValue(
      kajps5::gpu::Prospero::BufferFormat::k8_8_8_8UNorm) << 20u;
  image.dwords[2] = 1u | (1u << 14u);         // 2x2, one mip
  image.dwords[3] = static_cast<std::uint32_t>(
      kajps5::gpu::Prospero::ImageType::kColor2D) << 28u;
  result.resources.images.push_back(image);
  return result;
}
}  // namespace

int main() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  constexpr std::uint64_t kAddress = 0x8000;
  const std::array<std::byte, 4> before = {
      std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3}, std::byte{0xa4}};
  const std::array<std::byte, 16> pixels{};
  const std::array<std::byte, 4> after = {
      std::byte{0xb1}, std::byte{0xb2}, std::byte{0xb3}, std::byte{0xb4}};
  kajps5::memory::GuestMemory memory(
      0x7000, 0x2000, Protection::kRead | Protection::kWrite |
          Protection::kGpuRead | Protection::kGpuWrite);
  if (!memory.Initialize(kAddress - before.size(), std::as_bytes(std::span(before))) ||
      !memory.Initialize(kAddress, std::as_bytes(std::span(pixels))) ||
      !memory.Initialize(kAddress + pixels.size(), std::as_bytes(std::span(after)))) {
    std::cerr << "FAIL: guest-image smoke fixture initialization failed\n";
    return 1;
  }

  kajps5::gpu::GpuRuntime runtime(memory);
  const auto initialization = runtime.InitializeVulkan();
  if (!initialization) {
    const bool skip = IsUnavailable(initialization);
    std::ostream& output = skip ? std::cout : std::cerr;
    output << (skip ? "SKIP" : "FAIL") << ": Vulkan guest-image initialization\n";
    return skip ? 77 : 1;
  }
  const auto submitted = runtime.SubmitVulkanTranslatedCompute(
      StorageImageProgram(), 1, 1, 1,
      vk::kDefaultVulkanComputeFenceWaitNanoseconds);
  if (!submitted) {
    std::cerr << "FAIL: Vulkan guest-image submission status="
              << vk::VulkanComputeStatusName(submitted.status) << '\n';
    for (const auto& diagnostic : submitted.diagnostics) {
      std::cerr << "  " << vk::VulkanComputeDiagnosticCodeName(diagnostic.code)
                << ": " << diagnostic.message << '\n';
    }
    return 1;
  }
  std::array<std::byte, 16> actual{};
  std::array<std::byte, 4> read_before{};
  std::array<std::byte, 4> read_after{};
  const std::array<std::byte, 4> expected = {
      std::byte{0xff}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}};
  if (!memory.Read(kAddress, actual) ||
      !memory.Read(kAddress - before.size(), read_before) ||
      !memory.Read(kAddress + actual.size(), read_after) ||
      !std::equal(expected.begin(), expected.end(), actual.begin()) ||
      !std::equal(actual.begin() + expected.size(), actual.end(), pixels.begin() + expected.size()) ||
      read_before != before || read_after != after) {
    std::cerr << "FAIL: Vulkan guest-image readback or guard bytes differ\n";
    return 1;
  }
  std::cout << "Vulkan guest-image smoke timeline=" << submitted.timeline << '\n';
  return 0;
}
