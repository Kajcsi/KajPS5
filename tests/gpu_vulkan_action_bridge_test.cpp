// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/command_processor.h"
#include "gpu/runtime.h"
#include "gpu/vulkan/device.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_vulkan_action_bridge_test: " << message << '\n';
    std::exit(1);
  }
}

std::uint32_t Pm4(std::uint32_t dwords, std::uint32_t opcode) {
  return 0xc0000000U | (((dwords - 2U) & 0x3fffU) << 16U) |
         ((opcode & 0xffU) << 8U);
}

void WriteDwords(kajps5::memory::GuestMemory &memory, std::uint64_t address,
                 std::span<const std::uint32_t> words) {
  std::vector<std::byte> bytes(words.size() * sizeof(std::uint32_t));
  for (std::size_t index = 0; index < words.size(); ++index) {
    for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
      bytes[index * sizeof(std::uint32_t) + byte] =
          static_cast<std::byte>((words[index] >> (byte * 8U)) & 0xffU);
    }
  }
  Check(memory.Write(address, bytes), "PM4 fixture write failed");
}

void AppendContext(std::vector<std::uint32_t> &words, std::uint32_t reg,
                   std::uint32_t value) {
  words.insert(words.end(), {Pm4(3, 0x69), reg, value});
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

namespace vk = kajps5::gpu::vulkan;

bool HasDiagnostic(const vk::VulkanInitializationResult& initialization,
                   vk::VulkanDiagnosticCode code,
                   std::int32_t api_result = VK_SUCCESS,
                   const char* message = nullptr) {
  return std::any_of(initialization.diagnostics.begin(), initialization.diagnostics.end(),
                     [&](const vk::VulkanDiagnostic& diagnostic) {
                       return diagnostic.code == code && diagnostic.api_result == api_result &&
                              (message == nullptr || diagnostic.message == message);
                     });
}

bool IsUnavailableOrUnsupportedHost(const vk::VulkanInitializationResult& initialization) {
  switch (initialization.status) {
    case vk::VulkanContextStatus::kLoaderUnavailable:
    case vk::VulkanContextStatus::kLoaderApiVersionUnsupported:
      return true;
    case vk::VulkanContextStatus::kInstanceCreationFailed:
      return HasDiagnostic(initialization, vk::VulkanDiagnosticCode::kInstanceCreationFailed,
                           static_cast<std::int32_t>(VK_ERROR_INCOMPATIBLE_DRIVER));
    case vk::VulkanContextStatus::kPhysicalDeviceEnumerationFailed:
      return HasDiagnostic(initialization,
                           vk::VulkanDiagnosticCode::kPhysicalDeviceEnumerationFailed,
                           VK_SUCCESS, "Vulkan instance reported no physical devices");
    case vk::VulkanContextStatus::kNoSuitableDevice:
      return HasDiagnostic(initialization, vk::VulkanDiagnosticCode::kNoSuitableDevice,
                           VK_SUCCESS, "no Vulkan physical device passed the required gates");
    default:
      return false;
  }
}

void PrintInitializationDiagnostics(const vk::VulkanInitializationResult& initialization,
                                    std::ostream& output) {
  for (const auto& diagnostic : initialization.diagnostics) {
    output << "  " << vk::VulkanDiagnosticCodeName(diagnostic.code) << ": "
           << diagnostic.message << '\n';
  }
}

void ConfigureMinimalComputeShader(kajps5::memory::GuestMemory &memory,
                                   std::uint64_t header_address,
                                   std::uint64_t code_address,
                                   std::uint64_t register_address) {
  constexpr std::uint32_t kShaderFileHeader = 0x34333231U;
  constexpr std::uint32_t kShaderVersion = 0x18U;
  constexpr std::uint32_t kEndProgram = 0xbf810000U;
  std::array<std::byte, 0x60> header{};
  Write32(header, 0, kShaderFileHeader);
  Write32(header, 4, kShaderVersion);
  Write64(header, 0x20, register_address - (header_address + 0x20));
  Write32(header, 0x44, sizeof(std::uint32_t));
  header[0x5a] = std::byte{0}; // compute shader
  header[0x5c] = std::byte{2};
  std::array<std::byte, 16> registers{};
  Write32(registers, 0, 0x20cU);
  Write32(registers, 8, 0x20dU);
  std::array<std::byte, 4> code{};
  Write32(code, 0, kEndProgram);
  Check(memory.Write(header_address, header) &&
            memory.Write(register_address, registers) &&
            memory.Write(code_address, code),
        "compute shader fixture write failed");
}

// Narrow encoders copied from gpu_shader_emitter_test.cpp.  The EXP decoder
// identifies the 0x3e family; v5 is seeded with VertexIndex by the emitter.
constexpr std::uint32_t Vop1(std::uint32_t opcode, std::uint32_t dst,
                             std::uint32_t src) {
  return (0x3fU << 25U) | ((dst & 0xffU) << 17U) | ((opcode & 0xffU) << 9U) |
         (src & 0x1ffU);
}
constexpr std::uint32_t Vop2(std::uint32_t opcode, std::uint32_t dst,
                             std::uint32_t src0, std::uint32_t src1) {
  return ((opcode & 0x3fU) << 25U) | ((dst & 0xffU) << 17U) |
         ((src1 & 0xffU) << 9U) | (src0 & 0x1ffU);
}
constexpr std::uint32_t Exp(std::uint32_t target) {
  return (0x3eU << 26U) | ((target & 0x3fU) << 4U) | 0x0fU | (1U << 11U);
}
constexpr std::array<std::uint32_t, 2>
Vop3(std::uint32_t opcode, std::uint32_t dst, std::uint32_t src0,
     std::uint32_t src1, std::uint32_t src2 = 0) {
  return {(0x35U << 26U) | ((opcode & 0x3ffU) << 16U) | (dst & 0xffU),
          (src0 & 0x1ffU) | ((src1 & 0x1ffU) << 9U) | ((src2 & 0x1ffU) << 18U)};
}

void ConfigureShader(kajps5::memory::GuestMemory &memory,
                     std::uint64_t header_address, std::uint64_t code_address,
                     std::uint64_t register_address, std::uint8_t type,
                     std::uint32_t lo_register,
                     std::span<const std::uint32_t> code) {
  std::array<std::byte, 0x60> header{};
  Write32(header, 0, 0x34333231U);
  Write32(header, 4, 0x18U);
  Write64(header, 0x20, register_address - (header_address + 0x20));
  Write32(header, 0x44, static_cast<std::uint32_t>(code.size() * 4U));
  header[0x5a] = static_cast<std::byte>(type);
  header[0x5c] = std::byte{2};
  std::array<std::byte, 16> registers{};
  Write32(registers, 0, lo_register);
  Write32(registers, 8, lo_register + 1U);
  Check(memory.Write(header_address, header) &&
            memory.Write(register_address, registers),
        "graphics shader header write failed");
  WriteDwords(memory, code_address, code);
}

} // namespace

int main() {
  using kajps5::gpu::GpuActionTrace;
  using kajps5::gpu::GpuActionType;
  using kajps5::gpu::GpuCommandStatus;
  using kajps5::gpu::GpuRenderSnapshotStatus;
  using kajps5::gpu::GpuRuntime;
  using kajps5::gpu::VulkanActionBridgeStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x400000;
  constexpr std::uint64_t kCommands = kBase + 0x1000;
  constexpr std::uint64_t kComputeCommands = kBase + 0x2000;
  constexpr std::uint64_t kDrawCommands = kBase + 0x3000;
  constexpr std::uint64_t kColor = kBase + 0x4000;
  constexpr std::uint64_t kComputeHeader = kBase + 0x5000;
  constexpr std::uint64_t kComputeCode = kBase + 0x6000;
  constexpr std::uint64_t kComputeRegisters = kBase + 0x7000;
  constexpr std::uint64_t kVertexHeader = kBase + 0x8000;
  constexpr std::uint64_t kVertexCode = kBase + 0x9000;
  constexpr std::uint64_t kVertexRegisters = kBase + 0xa000;
  constexpr std::uint64_t kPixelHeader = kBase + 0xb000;
  constexpr std::uint64_t kPixelCode = kBase + 0xc000;
  constexpr std::uint64_t kPixelRegisters = kBase + 0xd000;
  GuestMemory memory(
      kBase, 0x10000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite |
          GuestMemoryProtection::kGpuRead | GuestMemoryProtection::kGpuWrite);
  GpuRuntime runtime(memory);
  std::vector<std::uint32_t> commands;
  // These are the Kyty PM4 context offsets and fields for the deliberately
  // narrow RGBA8-linear, one-sample, one-target bridge subset.
  AppendContext(commands, 0x318, static_cast<std::uint32_t>(kColor >> 8U));
  AppendContext(commands, 0x319, 1U); // (16 / 8) - 1
  AppendContext(commands, 0x31a, 0U);
  AppendContext(commands, 0x31b, 0U);
  AppendContext(commands, 0x31c, 10U << 2U);        // 8_8_8_8 + UNORM
  AppendContext(commands, 0x31d, 0U);               // linear, one sample
  AppendContext(commands, 0x3b0, 3U | (3U << 14U)); // 4x4 mip zero
  AppendContext(commands, 0x3b8, 0U);
  AppendContext(commands, 0x08e, 0xfU);
  AppendContext(commands, 0x1e0, 1U | (1U << 16U)); // disabled, ONE/ZERO
  AppendContext(commands, 0x205, 0U);
  AppendContext(commands, 0x094, 0U);
  AppendContext(commands, 0x095, 4U | (4U << 16U));
  AppendContext(commands, 0x10f, 0x40000000U); // x scale = 2
  AppendContext(commands, 0x110, 0x40000000U); // x offset = 2
  AppendContext(commands, 0x111, 0x40000000U); // y scale = 2
  AppendContext(commands, 0x112, 0x40000000U); // y offset = 2
  AppendContext(commands, 0x0b4, 0U);
  AppendContext(commands, 0x0b5, 0x3f800000U);
  AppendContext(commands, 0x200, 0U);
  commands.insert(commands.end(), {Pm4(3, 0x79), 0x242, 4U});
  commands.insert(commands.end(), {Pm4(2, 0x2f), 1U});
  commands.insert(commands.end(), {Pm4(3, 0x2d), 3U, 2U});
  WriteDwords(memory, kCommands, commands);

  GpuActionTrace trace(64);
  const auto decoded = runtime.ProcessCommandBuffer(
      kCommands, static_cast<std::uint32_t>(commands.size()), trace);
  Check(decoded.status == GpuCommandStatus::kComplete,
        "synthetic DrawIndexAuto command buffer did not decode");
  const auto found = std::find_if(
      trace.actions().begin(), trace.actions().end(),
      [](const auto &action) { return action.type == GpuActionType::kDraw; });
  Check(found != trace.actions().end(),
        "synthetic DrawIndexAuto was not emitted");
  const auto &draw = *found;
  Check(draw.type == GpuActionType::kDraw &&
            draw.render.status == GpuRenderSnapshotStatus::kReady &&
            draw.render.color_base == kColor && draw.render.color_width == 4U &&
            draw.render.color_height == 4U && draw.render.vertex_count == 3U &&
            draw.render.instance_count == 1U,
        "draw did not retain the immutable Kyty state subset");

  runtime.EnableVulkanActionExecution(true);
  Check(static_cast<bool>(runtime.submissions().EnqueueGraphics(
            kCommands, static_cast<std::uint32_t>(commands.size()))),
        "draw submission was not queued");
  const auto drain = runtime.DrainSubmissions();
  Check(drain.failed_submissions == 1 &&
            runtime.last_vulkan_action_result().status ==
                VulkanActionBridgeStatus::kRendererUnavailable,
        "enabled bridge did not execute through the runtime sink chain");

  // Register a public, minimal compute fixture and submit PM4 directly through
  // enqueue/drain. No caller invokes either translated-compute entry point.
  ConfigureMinimalComputeShader(memory, kComputeHeader, kComputeCode,
                                kComputeRegisters);
  const auto compute = runtime.CreateShader(0, kComputeHeader, kComputeCode);
  Check(static_cast<bool>(compute), "compute shader registration failed");
  const auto initialized = runtime.InitializeVulkan();
  if (!initialized) {
    const bool unavailable_or_unsupported = IsUnavailableOrUnsupportedHost(initialized);
    std::ostream& output = unavailable_or_unsupported ? std::cout : std::cerr;
    output << (unavailable_or_unsupported ? "SKIP" : "FAIL")
           << ": Vulkan action bridge initialization status="
           << vk::VulkanContextStatusName(initialized.status) << '\n';
    PrintInitializationDiagnostics(initialized, output);
    return unavailable_or_unsupported ? 77 : 1;
  }
  // v5 is VertexIndex. The VOP1/VOP2 sequences make the three full-screen
  // vertices (-1,-1), (3,-1), (-1,3); the PS exports solid RGBA red.
  const std::array vertex_code = {Vop1(5, 0, 261),
                                  Vop3(0x141, 1, 256, 256, 128)[0],
                                  Vop3(0x141, 1, 256, 256, 128)[1],
                                  Vop3(0x141, 3, 244, 256, 128)[0],
                                  Vop3(0x141, 3, 244, 256, 128)[1],
                                  Vop3(0x141, 3, 244, 259, 128)[0],
                                  Vop3(0x141, 3, 244, 259, 128)[1],
                                  Vop3(0x141, 3, 244, 259, 128)[0],
                                  Vop3(0x141, 3, 244, 259, 128)[1],
                                  Vop3(0x141, 3, 242, 259, 243)[0],
                                  Vop3(0x141, 3, 242, 259, 243)[1],
                                  Vop3(0x141, 2, 247, 257, 259)[0],
                                  Vop3(0x141, 2, 247, 257, 259)[1],
                                  Vop3(0x141, 4, 245, 256, 243)[0],
                                  Vop3(0x141, 4, 245, 256, 243)[1],
                                  Vop3(0x141, 3, 244, 257, 260)[0],
                                  Vop3(0x141, 3, 244, 257, 260)[1],
                                  Vop1(1, 4, 128),
                                  Vop1(1, 5, 242),
                                  Exp(0x0c),
                                  0x05040302U,
                                  0xbf810000U};
  const std::array pixel_code = {
      Vop1(1, 0, 242), Vop1(1, 1, 128), Vop1(1, 2, 128), Vop1(1, 3, 242),
      Exp(0),          0x03020100U,     0xbf810000U};
  ConfigureShader(memory, kVertexHeader, kVertexCode, kVertexRegisters, 2,
                  0x0c8, vertex_code);
  ConfigureShader(memory, kPixelHeader, kPixelCode, kPixelRegisters, 1, 0x008,
                  pixel_code);
  const auto vertex = runtime.CreateShader(0, kVertexHeader, kVertexCode);
  const auto pixel = runtime.CreateShader(0, kPixelHeader, kPixelCode);
  Check(vertex && pixel, "graphics shader registration failed");
  std::array<std::byte, 4> guard = {std::byte{0xa1}, std::byte{0xa2},
                                    std::byte{0xa3}, std::byte{0xa4}};
  std::array<std::byte, 64> blank{};
  Check(memory.Initialize(kColor - 4, guard) &&
            memory.Initialize(kColor, blank) &&
            memory.Initialize(kColor + 64, guard),
        "draw target guard initialization failed");
  auto graphics_commands = commands;
  graphics_commands.insert(
      graphics_commands.end() - 3,
      {Pm4(4, 0x76), 0x008U,
       static_cast<std::uint32_t>(pixel.record.program_address >> 8U),
       static_cast<std::uint32_t>(pixel.record.program_address >> 40U),
       Pm4(4, 0x76), 0x0c8U,
       static_cast<std::uint32_t>(vertex.record.program_address >> 8U),
       static_cast<std::uint32_t>(vertex.record.program_address >> 40U)});
  WriteDwords(memory, kDrawCommands, graphics_commands);
  Check(
      static_cast<bool>(runtime.submissions().EnqueueGraphics(
          kDrawCommands, static_cast<std::uint32_t>(graphics_commands.size()))),
      "draw bridge was not queued");
  const auto draw_drain = runtime.DrainSubmissions();
  std::array<std::byte, 64> pixels{};
  std::array<std::byte, 4> before{}, after{};
  if (draw_drain.completed_submissions != 1)
    std::cerr << "draw status="
              << static_cast<int>(runtime.last_vulkan_action_result().status)
              << " msg=" << runtime.last_vulkan_action_result().message << '\n';
  Check(draw_drain.completed_submissions == 1 &&
            runtime.last_vulkan_action_result().timeline != 0 &&
            memory.Read(kColor, pixels) && memory.Read(kColor - 4, before) &&
            memory.Read(kColor + 64, after) && before == guard &&
            after == guard,
        "automatic PM4 draw did not complete with intact guards");
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (!(pixels[i] == std::byte{0xff} && pixels[i + 1] == std::byte{0} &&
          pixels[i + 2] == std::byte{0} && pixels[i + 3] == std::byte{0xff})) {
      std::cerr << "pixel " << i / 4 << "=" << std::hex
                << std::to_integer<int>(pixels[i]) << ","
                << std::to_integer<int>(pixels[i + 1]) << ","
                << std::to_integer<int>(pixels[i + 2]) << ","
                << std::to_integer<int>(pixels[i + 3]) << std::dec << '\n';
      Check(false, "automatic PM4 draw pixel mismatch");
    }
  }
  const auto program_lo =
      static_cast<std::uint32_t>(compute.record.program_address >> 8U);
  const auto program_hi =
      static_cast<std::uint32_t>(compute.record.program_address >> 40U);
  const std::array compute_commands = {
      Pm4(4, 0x76), 0x20cU, program_lo, program_hi, Pm4(5, 0x15),
      1U,           1U,     1U,         0x41U,
  };
  WriteDwords(memory, kComputeCommands, compute_commands);
  Check(static_cast<bool>(runtime.submissions().EnqueueCompute(
            0U, kComputeCommands,
            static_cast<std::uint32_t>(compute_commands.size()))),
        "compute bridge submission was not queued");
  const auto compute_drain = runtime.DrainSubmissions();
  Check(compute_drain.completed_submissions == 1 &&
            runtime.last_vulkan_action_result().status ==
                VulkanActionBridgeStatus::kCompleted &&
            runtime.last_vulkan_action_result().timeline != 0,
        "PM4 dispatch did not execute through the Vulkan action bridge");
  return 0;
}
