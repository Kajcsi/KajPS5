// Copyright (C) 2026 KajPS5 contributors
// Architecture and behavior reference: KytyPS5
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "hle/agc_exports.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "hle_agc_exports_test: " << message << '\n';
    std::exit(1);
  }
}

void SetArguments(kajps5::hle::HleCallContext& context,
                  std::span<const std::uint64_t> arguments) {
  constexpr std::array registers = {
      kajps5::hle::HleRegister::kRdi, kajps5::hle::HleRegister::kRsi,
      kajps5::hle::HleRegister::kRdx, kajps5::hle::HleRegister::kRcx,
      kajps5::hle::HleRegister::kR8,  kajps5::hle::HleRegister::kR9};
  for (std::size_t index = 0; index < registers.size(); ++index) {
    Check(context.SetRegister(registers[index],
                              index < arguments.size() ? arguments[index] : 0),
          "argument register setup failed");
  }
}

std::uint64_t ReturnValue(const kajps5::hle::HleCallContext& context) {
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
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

std::uint32_t Read32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x400000;
  constexpr std::uint64_t kCommandBuffer = kBase + 0x100;
  constexpr std::uint64_t kPackets = kBase + 0x1000;
  constexpr std::uint64_t kPacketEnd = kPackets + 0x100;
  GuestMemory memory(
      kBase, 0x3000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  kajps5::gpu::GpuRuntime gpu_runtime(memory);
  ExportRegistry registry;
  Check(kajps5::hle::RegisterAgcExports(registry, gpu_runtime) ==
                ExportRegistryStatus::kOk &&
            registry.size() == kajps5::hle::kRegisteredAgcFunctionCount * 2,
        "AGC export registration failed");

  std::array<std::byte, kajps5::gpu::kAgcCommandBufferSize> command_buffer{};
  Write64(command_buffer, kajps5::gpu::kAgcCommandBufferCursorUpOffset,
          kPackets);
  Write64(command_buffer, kajps5::gpu::kAgcCommandBufferCursorDownOffset,
          kPacketEnd);
  Check(memory.Write(kCommandBuffer, command_buffer),
        "command-buffer setup failed");

  HleCallContext context(memory);
  const std::array nop_arguments = {kCommandBuffer, std::uint64_t{4}};
  SetArguments(context, nop_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcCbNopNid, context) &&
            ReturnValue(context) == kPackets,
        "NOP packet dispatch failed");
  std::array<std::byte, 16> nop{};
  Check(memory.Read(kPackets, nop) && Read32(nop, 0) == 0xc0021000U &&
            Read32(nop, 4) == 0 && Read32(nop, 8) == 0 &&
            Read32(nop, 12) == 0,
        "NOP packet words are incorrect");
  Check(memory.Read(kCommandBuffer, command_buffer) &&
            Read64(command_buffer,
                   kajps5::gpu::kAgcCommandBufferCursorUpOffset) ==
                kPackets + nop.size(),
        "NOP did not advance the command cursor");

  const std::array dispatch_arguments = {
      kCommandBuffer, std::uint64_t{2}, std::uint64_t{3}, std::uint64_t{4},
      std::uint64_t{0xffffffffU}};
  SetArguments(context, dispatch_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcCbDispatchNid, context) &&
            ReturnValue(context) == kPackets + nop.size(),
        "dispatch packet call failed");
  std::array<std::byte, 20> dispatch{};
  Check(memory.Read(kPackets + nop.size(), dispatch) &&
            Read32(dispatch, 0) == 0xc0031500U &&
            Read32(dispatch, 4) == 2 && Read32(dispatch, 8) == 3 &&
            Read32(dispatch, 12) == 4 && Read32(dispatch, 16) == 0xa079U,
        "dispatch packet words are incorrect");

  const std::array size_argument = {std::uint64_t{7}};
  SetArguments(context, size_argument);
  Check(registry.Dispatch(kajps5::hle::kAgcCbNopGetSizeNid, context) &&
            ReturnValue(context) == 28,
        "NOP size query is incorrect");
  Check(registry.Dispatch(kajps5::hle::kAgcCbDispatchGetSizeNid, context) &&
            ReturnValue(context) == 20,
        "dispatch size query is incorrect");

  const std::array packet_argument = {kPackets};
  SetArguments(context, packet_argument);
  Check(registry.Dispatch(kajps5::hle::kAgcGetPacketSizeNid, context) &&
            ReturnValue(context) == 4,
        "packet size decoder is incorrect");
  const std::array predication_arguments = {kPackets, std::uint64_t{1}};
  SetArguments(context, predication_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcSetPacketPredicationNid, context) &&
            ReturnValue(context) == 0 && memory.Read(kPackets, nop) &&
            Read32(nop, 0) == 0xc0021001U,
        "packet predication was not applied");

  std::array<std::byte, sizeof(std::uint32_t)> custom_header{};
  Write32(custom_header, 0, 0x3fff1000U);
  constexpr std::uint64_t kCustomPacket = kPackets + 0x80;
  Check(memory.Write(kCustomPacket, custom_header),
        "custom packet setup failed");
  const std::array custom_packet_argument = {kCustomPacket};
  SetArguments(context, custom_packet_argument);
  Check(registry.Dispatch(kajps5::hle::kAgcGetPacketSizeNid, context) &&
            ReturnValue(context) == 1,
        "single-dword custom packet size is incorrect");

  Write64(command_buffer, kajps5::gpu::kAgcCommandBufferCursorUpOffset,
          kPacketEnd - 4);
  Write32(command_buffer,
          kajps5::gpu::kAgcCommandBufferReservedDwordsOffset, 1);
  Check(memory.Write(kCommandBuffer, command_buffer),
        "reserved-space setup failed");
  SetArguments(context, nop_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcCbNopNid, context) &&
            ReturnValue(context) == 0 &&
            memory.Read(kCommandBuffer, command_buffer) &&
            Read64(command_buffer,
                   kajps5::gpu::kAgcCommandBufferCursorUpOffset) ==
                kPacketEnd - 4,
        "reserved command-buffer space was consumed");

  Write64(command_buffer, kajps5::gpu::kAgcCommandBufferCallbackOffset,
          0x12345678);
  Check(memory.Write(kCommandBuffer, command_buffer),
        "callback setup failed");
  SetArguments(context, nop_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcCbNopNid, context) &&
            ReturnValue(context) == 0,
        "unavailable callback path returned a fake packet");

  const std::array invalid_predication_arguments = {std::uint64_t{0},
                                                     std::uint64_t{1}};
  SetArguments(context, invalid_predication_arguments);
  constexpr auto kInvalidArgument = std::bit_cast<std::int32_t>(0x80020003U);
  Check(registry.Dispatch(kajps5::hle::kAgcSetPacketPredicationNid, context) &&
            ReturnValue(context) == static_cast<std::uint64_t>(
                                        static_cast<std::int64_t>(
                                            kInvalidArgument)),
        "invalid packet pointer returned the wrong error");
  return 0;
}
