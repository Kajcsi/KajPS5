// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/shader/spirv_builder.h"

namespace {

namespace vk = kajps5::gpu::vulkan;
using kajps5::gpu::ShaderType;
using kajps5::gpu::shader::SpirvBuilder;

bool IsUnavailable(const vk::VulkanInitializationResult& result) {
  using S = vk::VulkanContextStatus;
  return result.status == S::kLoaderUnavailable ||
         result.status == S::kLoaderApiVersionUnsupported ||
         result.status == S::kNoSuitableDevice ||
         (result.status == S::kInstanceCreationFailed &&
          std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const vk::VulkanDiagnostic& diagnostic) {
                        return diagnostic.api_result == VK_ERROR_INCOMPATIBLE_DRIVER;
                      })) ||
         (result.status == S::kPhysicalDeviceEnumerationFailed &&
          std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const vk::VulkanDiagnostic& diagnostic) {
                        return diagnostic.api_result == VK_SUCCESS;
                      }));
}

std::vector<std::uint32_t> VertexModule() {
  SpirvBuilder builder;
  const auto void_type = builder.AllocateId();
  const auto function_type = builder.AllocateId();
  const auto integer_type = builder.AllocateId();
  const auto float_type = builder.AllocateId();
  const auto vector_type = builder.AllocateId();
  const auto array_length = builder.AllocateId();
  const auto array_type = builder.AllocateId();
  const auto input_pointer = builder.AllocateId();
  const auto vertex_index = builder.AllocateId();
  const auto private_array_pointer = builder.AllocateId();
  const auto positions = builder.AllocateId();
  const auto private_vector_pointer = builder.AllocateId();
  const auto output_pointer = builder.AllocateId();
  const auto position = builder.AllocateId();
  const auto minus_one = builder.AllocateId();
  const auto zero = builder.AllocateId();
  const auto one = builder.AllocateId();
  const auto three = builder.AllocateId();
  const auto lower_left = builder.AllocateId();
  const auto lower_right = builder.AllocateId();
  const auto upper_left = builder.AllocateId();
  const auto position_array = builder.AllocateId();
  const auto entry = builder.AllocateId();
  const auto label = builder.AllocateId();
  const auto index_value = builder.AllocateId();
  const auto element_pointer = builder.AllocateId();
  const auto element = builder.AllocateId();

  builder.AddCapability({1});
  builder.AddMemoryModel({0, 1});
  builder.AddEntryPoint(0, entry, "main", {vertex_index, position});
  builder.AddAnnotation({71, vertex_index, 11, 42});  // BuiltIn VertexIndex
  builder.AddAnnotation({71, position, 11, 0});       // BuiltIn Position
  builder.AddType({19, void_type});
  builder.AddType({33, function_type, void_type});
  builder.AddType({21, integer_type, 32, 1});
  builder.AddType({22, float_type, 32});
  builder.AddType({23, vector_type, float_type, 4});
  builder.AddType({43, integer_type, array_length, 3});
  builder.AddType({28, array_type, vector_type, array_length});
  builder.AddType({32, input_pointer, 1, integer_type});
  builder.AddType({59, input_pointer, vertex_index, 1});
  builder.AddType({32, private_array_pointer, 6, array_type});
  builder.AddType({32, private_vector_pointer, 6, vector_type});
  builder.AddType({32, output_pointer, 3, vector_type});
  builder.AddType({43, float_type, minus_one, 0xbf800000});
  builder.AddType({43, float_type, zero, 0});
  builder.AddType({43, float_type, one, 0x3f800000});
  builder.AddType({43, float_type, three, 0x40400000});
  builder.AddType({44, vector_type, lower_left, minus_one, minus_one, zero, one});
  builder.AddType({44, vector_type, lower_right, three, minus_one, zero, one});
  builder.AddType({44, vector_type, upper_left, minus_one, three, zero, one});
  builder.AddType({44, array_type, position_array, lower_left, lower_right, upper_left});
  builder.AddType({59, private_array_pointer, positions, 6, position_array});
  builder.AddType({59, output_pointer, position, 3});
  builder.AddFunction({54, void_type, entry, 0, function_type});
  builder.AddFunction({248, label});
  builder.AddFunction({61, integer_type, index_value, vertex_index});
  builder.AddFunction({65, private_vector_pointer, element_pointer, positions, index_value});
  builder.AddFunction({61, vector_type, element, element_pointer});
  builder.AddFunction({62, position, element});
  builder.AddFunction({253});
  builder.AddFunction({56});
  return builder.Build();
}

std::vector<std::uint32_t> PixelModule() {
  SpirvBuilder builder;
  const auto void_type = builder.AllocateId();
  const auto function_type = builder.AllocateId();
  const auto float_type = builder.AllocateId();
  const auto vector_type = builder.AllocateId();
  const auto output_pointer = builder.AllocateId();
  const auto color = builder.AllocateId();
  const auto zero = builder.AllocateId();
  const auto one = builder.AllocateId();
  const auto red = builder.AllocateId();
  const auto entry = builder.AllocateId();
  const auto label = builder.AllocateId();

  builder.AddCapability({1});
  builder.AddMemoryModel({0, 1});
  builder.AddEntryPoint(4, entry, "main", {color});
  builder.AddExecutionMode({entry, 7});  // OriginUpperLeft
  builder.AddAnnotation({71, color, 30, 0});
  builder.AddType({19, void_type});
  builder.AddType({33, function_type, void_type});
  builder.AddType({22, float_type, 32});
  builder.AddType({23, vector_type, float_type, 4});
  builder.AddType({32, output_pointer, 3, vector_type});
  builder.AddType({43, float_type, zero, 0});
  builder.AddType({43, float_type, one, 0x3f800000});
  builder.AddType({44, vector_type, red, one, zero, zero, one});
  builder.AddType({59, output_pointer, color, 3});
  builder.AddFunction({54, void_type, entry, 0, function_type});
  builder.AddFunction({248, label});
  builder.AddFunction({62, color, red});
  builder.AddFunction({253});
  builder.AddFunction({56});
  return builder.Build();
}

kajps5::gpu::shader::recompiler::CompileResult Program(
    ShaderType stage, std::vector<std::uint32_t> spirv) {
  kajps5::gpu::shader::recompiler::CompileResult result;
  result.program.stage = stage;
  result.spirv = std::move(spirv);
  return result;
}

}  // namespace

int main() {
  constexpr std::uint64_t kAddress = 0x8000;
  constexpr std::array<std::byte, 4> kBefore = {
      std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3}, std::byte{0xa4}};
  constexpr std::array<std::byte, 4> kAfter = {
      std::byte{0xb1}, std::byte{0xb2}, std::byte{0xb3}, std::byte{0xb4}};
  std::array<std::byte, 64> pixels{};
  kajps5::memory::GuestMemory memory(
      0x7000, 0x2000, kajps5::memory::GuestMemoryProtection::kRead |
                         kajps5::memory::GuestMemoryProtection::kWrite |
                         kajps5::memory::GuestMemoryProtection::kGpuRead |
                         kajps5::memory::GuestMemoryProtection::kGpuWrite);
  if (!memory.Initialize(kAddress - kBefore.size(), std::as_bytes(std::span(kBefore))) ||
      !memory.Initialize(kAddress, std::as_bytes(std::span(pixels))) ||
      !memory.Initialize(kAddress + pixels.size(), std::as_bytes(std::span(kAfter)))) {
    std::cerr << "FAIL: guest-draw fixture initialization failed\n";
    return 1;
  }
  kajps5::gpu::GpuRuntime runtime(memory);
  const auto initialization = runtime.InitializeVulkan();
  if (!initialization) {
    const bool skip = IsUnavailable(initialization);
    std::cerr << (skip ? "SKIP" : "FAIL") << ": Vulkan guest-draw initialization\n";
    return skip ? 77 : 1;
  }
  const auto vertex = Program(ShaderType::Vertex, VertexModule());
  const auto pixel = Program(ShaderType::Pixel, PixelModule());
  vk::VulkanTranslatedDrawRequest request;
  request.vertex = &vertex;
  request.pixel = &pixel;
  request.color_target = {kAddress,
      kajps5::gpu::Prospero::GpuEnumValue(
          kajps5::gpu::Prospero::BufferFormat::k8_8_8_8UNorm),
      4, 4, 1, 1, 1, 0, 0, kajps5::gpu::Prospero::ImageType::kColor2D,
      kajps5::gpu::Prospero::TileMode::kLinear, true};
  request.viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f, {{0, 0}, {4, 4}}};
  request.vertex_count = 3;
  const auto submitted = runtime.SubmitVulkanTranslatedDraw(request);
  if (!submitted) {
    std::cerr << "FAIL: Vulkan guest-draw submission status="
              << static_cast<int>(submitted.status) << '\n';
    return 1;
  }
  std::array<std::byte, 64> actual{};
  std::array<std::byte, 4> before{};
  std::array<std::byte, 4> after{};
  const std::array<std::byte, 4> expected = {
      std::byte{0xff}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}};
  if (!memory.Read(kAddress, actual) || !memory.Read(kAddress - before.size(), before) ||
      !memory.Read(kAddress + actual.size(), after) || before != kBefore || after != kAfter) {
    std::cerr << "FAIL: guest-draw guard bytes differ\n";
    return 1;
  }
  for (std::size_t offset = 0; offset < actual.size(); ++offset) {
    if (actual[offset] != expected[offset % expected.size()]) {
      std::cerr << "FAIL: guest-draw color differs\n";
      return 1;
    }
  }
  std::cout << "Vulkan guest-draw smoke timeline=" << submitted.timeline << '\n';
  return 0;
}
