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
#include <vector>

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
  const auto stack_arguments = arguments.size() > registers.size()
                                   ? arguments.subspan(registers.size())
                                   : std::span<const std::uint64_t>{};
  Check(context.SetCapturedStackArguments(stack_arguments),
        "stack argument setup failed");
}

std::uint64_t ReturnValue(const kajps5::hle::HleCallContext& context) {
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

std::uint32_t Pm4(std::uint32_t dwords, std::uint32_t opcode,
                  std::uint32_t packet_register = 0) {
  return 0xc0000000U | (((dwords - 2U) & 0x3fffU) << 16U) |
         ((opcode & 0xffU) << 8U) | ((packet_register & 0x3fU) << 2U);
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

void WriteDwords(kajps5::memory::GuestMemory& memory,
                 std::uint64_t address,
                 std::span<const std::uint32_t> words) {
  std::vector<std::byte> bytes(words.size() * sizeof(std::uint32_t));
  for (std::size_t index = 0; index < words.size(); ++index) {
    Write32(bytes, index * sizeof(std::uint32_t), words[index]);
  }
  Check(memory.Write(address, bytes), "driver command write failed");
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
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

  Write64(command_buffer, kajps5::gpu::kAgcCommandBufferCursorUpOffset,
          kPackets);
  Write32(command_buffer,
          kajps5::gpu::kAgcCommandBufferReservedDwordsOffset, 0);
  Write64(command_buffer, kajps5::gpu::kAgcCommandBufferCallbackOffset, 0);
  Check(memory.Write(kCommandBuffer, command_buffer),
        "extended packet setup failed");

  constexpr std::uint64_t kPackedRegister = 0x11223344abcd1234ULL;
  const std::array register_arguments = {kCommandBuffer, kPackedRegister};
  SetArguments(context, register_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbSetCxRegisterDirectNid,
                          context) &&
            ReturnValue(context) == kPackets,
        "context-register packet call failed");
  std::array<std::byte, 12> register_packet{};
  Check(memory.Read(kPackets, register_packet) &&
            Read32(register_packet, 0) == 0xc0016900U &&
            Read32(register_packet, 4) == 0x1234U &&
            Read32(register_packet, 8) == 0x11223344U,
        "context-register packet words are incorrect");

  constexpr auto kDrawPacket = kPackets + register_packet.size();
  const std::array draw_arguments = {
      kCommandBuffer, std::uint64_t{9}, std::uint64_t{0x123456780ULL},
      std::uint64_t{0}};
  SetArguments(context, draw_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbDrawIndexNid, context) &&
            ReturnValue(context) == kDrawPacket,
        "indexed-draw packet call failed");
  std::array<std::byte, 24> draw_packet{};
  Check(memory.Read(kDrawPacket, draw_packet) &&
            Read32(draw_packet, 0) == 0xc0042700U &&
            Read32(draw_packet, 4) == 9 &&
            Read32(draw_packet, 8) == 0x23456780U &&
            Read32(draw_packet, 12) == 1 &&
            Read32(draw_packet, 16) == 9 && Read32(draw_packet, 20) == 0,
        "indexed-draw packet words are incorrect");

  constexpr auto kJumpPacket = kDrawPacket + draw_packet.size();
  const std::array jump_arguments = {
      kCommandBuffer, std::uint64_t{1}, std::uint64_t{2},
      std::uint64_t{0x234567880ULL}, std::uint64_t{0x12345}};
  SetArguments(context, jump_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbJumpNid, context) &&
            ReturnValue(context) == kJumpPacket,
        "jump packet call failed");
  std::array<std::byte, 16> jump_packet{};
  Check(memory.Read(kJumpPacket, jump_packet) &&
            Read32(jump_packet, 0) == 0xc0023f00U &&
            Read32(jump_packet, 4) == 0x34567880U &&
            Read32(jump_packet, 8) == 2 &&
            Read32(jump_packet, 12) == 0x2f312345U,
        "jump packet words are incorrect");

  constexpr std::uint64_t kWriteSource = kBase + 0x800;
  std::array<std::byte, 8> write_source{};
  Write32(write_source, 0, 0x55667788U);
  Write32(write_source, 4, 0x99aabbccU);
  Check(memory.Write(kWriteSource, write_source), "write-data setup failed");
  constexpr auto kWritePacket = kJumpPacket + jump_packet.size();
  const std::array write_arguments = {
      kCommandBuffer, std::uint64_t{2}, std::uint64_t{1},
      std::uint64_t{0x345678980ULL}, kWriteSource, std::uint64_t{2},
      std::uint64_t{1}, std::uint64_t{1}};
  SetArguments(context, write_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbWriteDataNid, context) &&
            ReturnValue(context) == kWritePacket,
        "write-data packet call failed");
  std::array<std::byte, 24> write_packet{};
  Check(memory.Read(kWritePacket, write_packet) &&
            Read32(write_packet, 0) == 0xc0043700U &&
            Read32(write_packet, 4) == 0x02110100U &&
            Read32(write_packet, 8) == 0x45678980U &&
            Read32(write_packet, 12) == 3 &&
            Read32(write_packet, 16) == 0x55667788U &&
            Read32(write_packet, 20) == 0x99aabbccU,
        "write-data packet words are incorrect");

  constexpr auto kWaitPacket = kWritePacket + write_packet.size();
  const std::array wait_arguments = {
      kCommandBuffer, std::uint64_t{0}, std::uint64_t{3},
      std::uint64_t{2}, std::uint64_t{1}, std::uint64_t{0x456789a83ULL},
      std::uint64_t{0x55}, std::uint64_t{0xff}, std::uint64_t{0x120}};
  SetArguments(context, wait_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbWaitRegMemNid, context) &&
            ReturnValue(context) == kWaitPacket,
        "wait-memory packet call failed");
  std::array<std::byte, 28> wait_packet{};
  Check(memory.Read(kWaitPacket, wait_packet) &&
            Read32(wait_packet, 0) == 0xc0051028U &&
            Read32(wait_packet, 4) == 0x56789a80U &&
            Read32(wait_packet, 8) == 4 &&
            Read32(wait_packet, 12) == 0xffU &&
            Read32(wait_packet, 16) == 0x55U &&
            Read32(wait_packet, 20) == 0x02000213U &&
            Read32(wait_packet, 24) == 0x12U,
        "wait-memory packet words are incorrect");

  const std::array write_size_argument = {std::uint64_t{5}};
  SetArguments(context, write_size_argument);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbWriteDataGetSizeNid, context) &&
            ReturnValue(context) == 36,
        "write-data size query is incorrect");
  const std::array wait_size_argument = {std::uint64_t{1}};
  SetArguments(context, wait_size_argument);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbWaitOnAddressGetSizeNid,
                          context) &&
            ReturnValue(context) == 64,
        "wait-memory size query is incorrect");

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

  constexpr std::uint64_t kDriverPacket = kBase + 0x300;
  constexpr std::uint64_t kDriverCommands = kBase + 0x1c00;
  constexpr std::uint64_t kDriverWaitCommands = kBase + 0x1d00;
  constexpr std::uint64_t kDriverLabel = kBase + 0x1f00;
  const std::array driver_commands = {
      Pm4(5, 0x15), 2U, 3U, 4U, 0x41U,
  };
  const std::array driver_wait_commands = {
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kDriverLabel),
      static_cast<std::uint32_t>(kDriverLabel >> 32U), 0xffU, 0x66U,
      0x13U, 1U,
      Pm4(3, 0x2d), 7U, 2U,
  };
  WriteDwords(memory, kDriverCommands, driver_commands);
  WriteDwords(memory, kDriverWaitCommands, driver_wait_commands);
  Check(context.WriteUInt32(kDriverLabel, 0) == HleContextStatus::kOk,
        "driver wait label setup failed");

  std::array<std::byte, 16> driver_packet{};
  Write64(driver_packet, 0, kDriverWaitCommands);
  Write32(driver_packet, 8,
          static_cast<std::uint32_t>(driver_wait_commands.size()));
  Check(memory.Write(kDriverPacket, driver_packet),
        "driver packet setup failed");
  const std::array submit_dcb_arguments = {kDriverPacket};
  SetArguments(context, submit_dcb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == 0 &&
            gpu_runtime.submissions().PendingSubmissionCount() == 1 &&
            gpu_runtime.submission_history().size() == 1 &&
            gpu_runtime.submission_history().At(0).has_value() &&
            gpu_runtime.submission_history().At(0)->type ==
                kajps5::gpu::GpuActionType::kWaitMemory,
        "DCB submit did not retain its blocked queue position");

  Check(context.WriteUInt32(kDriverLabel, 0x66U) == HleContextStatus::kOk,
        "driver wait label update failed");
  Write64(driver_packet, 0, kDriverCommands);
  Write32(driver_packet, 8,
          static_cast<std::uint32_t>(driver_commands.size()));
  Check(memory.Write(kDriverPacket, driver_packet),
        "second driver packet setup failed");
  SetArguments(context, submit_dcb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == 0 &&
            gpu_runtime.submissions().PendingSubmissionCount() == 0 &&
            gpu_runtime.submission_history().size() == 3 &&
            gpu_runtime.submission_history().At(1)->type ==
                kajps5::gpu::GpuActionType::kDraw &&
            gpu_runtime.submission_history().At(2)->type ==
                kajps5::gpu::GpuActionType::kDispatch,
        "later DCB submit did not resume earlier work before new work");

  const std::array submit_acb_arguments = {std::uint64_t{7}, kDriverPacket};
  SetArguments(context, submit_acb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitAcbNid, context) &&
            ReturnValue(context) == 0 &&
            gpu_runtime.submission_history().size() == 4 &&
            gpu_runtime.submission_history().At(3)->type ==
                kajps5::gpu::GpuActionType::kDispatch,
        "ACB submit did not use its owned compute queue");

  const std::array invalid_submit_arguments = {std::uint64_t{0}};
  SetArguments(context, invalid_submit_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == static_cast<std::uint64_t>(
                                        static_cast<std::int64_t>(
                                            kInvalidArgument)),
        "null driver packet returned the wrong error");
  constexpr auto kMemoryFault = std::bit_cast<std::int32_t>(0x80020101U);
  const std::array faulting_submit_arguments = {kBase + 0x2ff8};
  SetArguments(context, faulting_submit_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == static_cast<std::uint64_t>(
                                        static_cast<std::int64_t>(
                                            kMemoryFault)),
        "faulting driver packet returned the wrong error");
  return 0;
}
