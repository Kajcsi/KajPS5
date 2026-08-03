// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/vulkan/buffer_cache.h"

namespace {

using kajps5::gpu::GpuRuntime;
using kajps5::gpu::ShaderType;
namespace ir = kajps5::gpu::shader::recompiler::IR;
namespace vk = kajps5::gpu::vulkan;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_vulkan_buffer_cache_test: " << message << '\n';
    std::exit(1);
  }
}

kajps5::gpu::shader::recompiler::CompileResult LegalBufferCompile() {
  kajps5::gpu::shader::recompiler::CompileResult compile;
  auto &program = compile.program;
  program.stage = ShaderType::Compute;
  program.resource_tracking_complete = true;
  program.shader_info_complete = true;
  program.binding_layout_complete = true;
  program.info.buffers.push_back({.source = 0,
                                  .max_byte_extent = 16,
                                  .packed_stride = 4,
                                  .descriptor_format = 0,
                                  .read = true});
  program.bindings.descriptor_set = 0;
  program.bindings.push_constant_size = 4;
  program.bindings.buffer_offset_dword = 0;
  program.bindings.buffer_offset_count = 1;
  program.bindings.descriptors.push_back(
      {ir::DescriptorBindingKind::Buffers, 3, {0}});
  ir::DescriptorValue descriptor;
  descriptor.dword_count = 4;
  descriptor.dwords[0] = 0x1000;
  descriptor.dwords[1] = 4U << 16U;
  descriptor.dwords[2] = 4;
  compile.resources.buffers.push_back(descriptor);
  return compile;
}

} // namespace

int main() {
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(0x1000, 0x1000,
                                     Protection::kRead | Protection::kWrite |
                                         Protection::kGpuRead |
                                         Protection::kGpuWrite);
  const std::array<std::byte, 16> source = {
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
      std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd},
      std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88},
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
  Check(memory.Initialize(0x1000, source), "guest initialization failed");
  GpuRuntime runtime(memory);
  vk::VulkanGuestBufferCache cache(memory, runtime.resource_coherence());

  auto legal = LegalBufferCompile();
  auto zero = LegalBufferCompile();
  zero.program.info.buffers[0].max_byte_extent = 0;
  zero.resources.buffers[0].dwords[1] = 0;
  zero.resources.buffers[0].dwords[2] = 0;
  zero.program.info.buffers[0].packed_stride = 0;
  const auto zero_result = cache.Prepare(zero);
  Check(zero_result.status == vk::VulkanGuestBufferStatus::kZeroFootprint,
        "zero descriptor footprint was not rejected before Vulkan work");

  auto topology = LegalBufferCompile();
  topology.program.stage = ShaderType::Vertex;
  const auto topology_result = cache.Prepare(topology);
  Check(topology_result.status ==
            vk::VulkanGuestBufferStatus::kUnsupportedTopology,
        "non-compute topology was not rejected before Vulkan work");
  return 0;
}
