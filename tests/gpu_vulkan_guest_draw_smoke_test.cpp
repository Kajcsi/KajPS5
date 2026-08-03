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
#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace {

namespace vk = kajps5::gpu::vulkan;
using kajps5::gpu::ShaderType;
using kajps5::gpu::shader::SpirvBuilder;
namespace ir = kajps5::gpu::shader::recompiler::IR;

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

// A deterministic fragment module with separate image/sampler descriptors at
// set 1 bindings 2/3. It reads texel (0, 0), making a self-feedback snapshot
// result unambiguous: every destination pixel must become that original texel.
std::vector<std::uint32_t> SampledPixelModule() {
  SpirvBuilder builder;
  const auto void_type = builder.AllocateId();
  const auto function_type = builder.AllocateId();
  const auto float_type = builder.AllocateId();
  const auto vector2_type = builder.AllocateId();
  const auto vector4_type = builder.AllocateId();
  const auto image_type = builder.AllocateId();
  const auto sampler_type = builder.AllocateId();
  const auto sampled_image_type = builder.AllocateId();
  const auto image_pointer = builder.AllocateId();
  const auto sampler_pointer = builder.AllocateId();
  const auto output_pointer = builder.AllocateId();
  const auto image = builder.AllocateId();
  const auto sampler = builder.AllocateId();
  const auto color = builder.AllocateId();
  const auto loaded_image = builder.AllocateId();
  const auto loaded_sampler = builder.AllocateId();
  const auto sampled_image = builder.AllocateId();
  const auto sampled_color = builder.AllocateId();
  const auto fraction = builder.AllocateId();
  const auto coordinate_value = builder.AllocateId();
  const auto entry = builder.AllocateId();
  const auto label = builder.AllocateId();

  builder.AddCapability({1});
  builder.AddMemoryModel({0, 1});
  builder.AddEntryPoint(4, entry, "main", {color});
  builder.AddExecutionMode({entry, 7});  // OriginUpperLeft
  builder.AddAnnotation({71, image, 34, 1});   // DescriptorSet 1
  builder.AddAnnotation({71, image, 33, 2});   // Binding 2
  builder.AddAnnotation({71, sampler, 34, 1}); // DescriptorSet 1
  builder.AddAnnotation({71, sampler, 33, 3}); // Binding 3
  builder.AddAnnotation({71, color, 30, 0});   // Location 0
  builder.AddType({19, void_type});
  builder.AddType({33, function_type, void_type});
  builder.AddType({22, float_type, 32});
  builder.AddType({23, vector2_type, float_type, 2});
  builder.AddType({23, vector4_type, float_type, 4});
  builder.AddType({25, image_type, float_type, 1, 0, 0, 0, 1, 0});
  builder.AddType({26, sampler_type});
  builder.AddType({27, sampled_image_type, image_type});
  builder.AddType({32, image_pointer, 0, image_type});
  builder.AddType({32, sampler_pointer, 0, sampler_type});
  builder.AddType({32, output_pointer, 3, vector4_type});
  builder.AddType({59, image_pointer, image, 0});
  builder.AddType({59, sampler_pointer, sampler, 0});
  builder.AddType({59, output_pointer, color, 3});
  builder.AddType({43, float_type, fraction, 0x3e000000}); // 0.125
  builder.AddType({44, vector2_type, coordinate_value, fraction, fraction});
  builder.AddFunction({54, void_type, entry, 0, function_type});
  builder.AddFunction({248, label});
  builder.AddFunction({61, image_type, loaded_image, image});
  builder.AddFunction({61, sampler_type, loaded_sampler, sampler});
  builder.AddFunction({86, sampled_image_type, sampled_image, loaded_image,
                       loaded_sampler});
  builder.AddFunction({87, vector4_type, sampled_color, sampled_image,
                       coordinate_value});
  builder.AddFunction({62, color, sampled_color});
  builder.AddFunction({253});
  builder.AddFunction({56});
  return builder.Build();
}

kajps5::gpu::shader::recompiler::CompileResult Program(
    ShaderType stage, std::vector<std::uint32_t> spirv) {
  kajps5::gpu::shader::recompiler::CompileResult result;
  result.program.stage = stage;
  result.program.resource_tracking_complete = true;
  result.program.shader_info_complete = true;
  result.program.binding_layout_complete = true;
  result.program.bindings.descriptor_set = stage == ShaderType::Pixel ? 1U : 0U;
  result.spirv = std::move(spirv);
  return result;
}

kajps5::gpu::shader::recompiler::CompileResult SampledPixelProgram(
    std::uint64_t guest_address) {
  auto result = Program(ShaderType::Pixel, SampledPixelModule());
  result.program.info.images.push_back({
      .source = 0,
      .kind = ir::ResourceKind::Image,
      .dimension = kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2D,
      .storage_swizzle = ir::StorageImageIdentitySwizzle,
      .read = true,
  });
  result.program.info.samplers.push_back({.source = 0});
  result.program.bindings.descriptors.push_back(
      {ir::DescriptorBindingKind::Sampled2D, 2, {0}});
  result.program.bindings.descriptors.push_back(
      {ir::DescriptorBindingKind::Samplers, 3, {0}});
  ir::DescriptorValue image;
  image.dword_count = 8;
  image.dwords[0] = static_cast<std::uint32_t>(guest_address >> 8U);
  image.dwords[1] = kajps5::gpu::Prospero::GpuEnumValue(
      kajps5::gpu::Prospero::BufferFormat::k8_8_8_8UNorm) << 20U;
  image.dwords[1] |= 3U << 30U;
  image.dwords[2] = 3U << 14U;
  image.dwords[3] = static_cast<std::uint32_t>(
      kajps5::gpu::Prospero::ImageType::kColor2D) << 28U;
  result.resources.images.push_back(image);
  ir::DescriptorValue sampler;
  sampler.dword_count = 4;
  result.resources.samplers.push_back(sampler);
  return result;
}

}  // namespace

int main() {
  constexpr std::uint64_t kSourceAddress = 0x8000;
  constexpr std::uint64_t kDestinationAddress = 0x8400;
  constexpr std::array<std::byte, 4> kBefore = {
      std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3}, std::byte{0xa4}};
  constexpr std::array<std::byte, 4> kAfter = {
      std::byte{0xb1}, std::byte{0xb2}, std::byte{0xb3}, std::byte{0xb4}};
  std::array<std::byte, 64> source{};
  constexpr std::array<std::byte, 4> kSampledTexel = {
      std::byte{0x21}, std::byte{0x43}, std::byte{0x65}, std::byte{0x87}};
  std::copy(kSampledTexel.begin(), kSampledTexel.end(), source.begin());
  const std::array<std::byte, 64> destination{};
  kajps5::memory::GuestMemory memory(
      0x7000, 0x2000, kajps5::memory::GuestMemoryProtection::kRead |
                         kajps5::memory::GuestMemoryProtection::kWrite |
                         kajps5::memory::GuestMemoryProtection::kGpuRead |
                         kajps5::memory::GuestMemoryProtection::kGpuWrite);
  if (!memory.Initialize(kSourceAddress - kBefore.size(), std::as_bytes(std::span(kBefore))) ||
      !memory.Initialize(kSourceAddress, std::as_bytes(std::span(source))) ||
      !memory.Initialize(kSourceAddress + source.size(), std::as_bytes(std::span(kAfter))) ||
      !memory.Initialize(kDestinationAddress - kBefore.size(), std::as_bytes(std::span(kBefore))) ||
      !memory.Initialize(kDestinationAddress, std::as_bytes(std::span(destination))) ||
      !memory.Initialize(kDestinationAddress + destination.size(), std::as_bytes(std::span(kAfter)))) {
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
  const auto pixel = SampledPixelProgram(kSourceAddress);
  const auto MakeRequest = [&](std::uint64_t target_address) {
    vk::VulkanTranslatedDrawRequest request;
    request.vertex = &vertex;
    request.pixel = &pixel;
    request.color_target = {target_address,
        kajps5::gpu::Prospero::GpuEnumValue(
            kajps5::gpu::Prospero::BufferFormat::k8_8_8_8UNorm),
        4, 4, 1, 1, 1, 0, 0, kajps5::gpu::Prospero::ImageType::kColor2D,
        kajps5::gpu::Prospero::TileMode::kLinear, true};
    request.viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f, {{0, 0}, {4, 4}}};
    request.vertex_count = 3;
    return request;
  };
  // The first draw samples its own pre-draw attachment contents. The public
  // runtime must bind the separately prepared immutable snapshot, not the
  // live attachment image/view being rendered to.
  const auto aliased = runtime.SubmitVulkanTranslatedDraw(MakeRequest(kSourceAddress));
  if (!aliased) {
    std::cerr << "FAIL: Vulkan aliased guest-draw submission status="
              << static_cast<int>(aliased.status) << '\n';
    return 1;
  }
  std::array<std::byte, 64> actual_source{};
  std::array<std::byte, 4> source_before{};
  std::array<std::byte, 4> source_after{};
  if (!memory.Read(kSourceAddress, actual_source) ||
      !memory.Read(kSourceAddress - source_before.size(), source_before) ||
      !memory.Read(kSourceAddress + actual_source.size(), source_after) ||
      source_before != kBefore || source_after != kAfter) {
    std::cerr << "FAIL: aliased guest-draw guard bytes differ\n";
    return 1;
  }
  for (std::size_t offset = 0; offset < actual_source.size(); ++offset) {
    if (actual_source[offset] != kSampledTexel[offset % kSampledTexel.size()]) {
      std::cerr << "FAIL: aliased guest-draw snapshot color differs\n";
      return 1;
    }
  }
  // The completed aliased attachment is subsequently sampled into a distinct
  // guest-backed destination, proving writeback visibility and guard safety.
  const auto copied = runtime.SubmitVulkanTranslatedDraw(
      MakeRequest(kDestinationAddress));
  if (!copied) {
    std::cerr << "FAIL: Vulkan destination guest-draw submission status="
              << static_cast<int>(copied.status) << '\n';
    return 1;
  }
  std::array<std::byte, 64> actual_destination{};
  std::array<std::byte, 4> destination_before{};
  std::array<std::byte, 4> destination_after{};
  if (!memory.Read(kDestinationAddress, actual_destination) ||
      !memory.Read(kDestinationAddress - destination_before.size(), destination_before) ||
      !memory.Read(kDestinationAddress + actual_destination.size(), destination_after) ||
      destination_before != kBefore || destination_after != kAfter) {
    std::cerr << "FAIL: destination guest-draw guard bytes differ\n";
    return 1;
  }
  for (std::size_t offset = 0; offset < actual_destination.size(); ++offset) {
    if (actual_destination[offset] != kSampledTexel[offset % kSampledTexel.size()]) {
      std::cerr << "FAIL: destination guest-draw sampled color differs\n";
      return 1;
    }
  }
  std::cout << "Vulkan guest-draw alias snapshot source_timeline="
            << aliased.timeline << " destination_timeline=" << copied.timeline
            << " pixel=21 43 65 87 guards=ok\n";
  return 0;
}
