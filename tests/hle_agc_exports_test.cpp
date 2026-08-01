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
#include "kernel/runtime.h"

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
  kajps5::kernel::KernelRuntime kernel_runtime;
  kajps5::gpu::GpuRuntime gpu_runtime(
      memory, nullptr, &kernel_runtime.event_queues());
  ExportRegistry registry;
  Check(kajps5::hle::RegisterAgcExports(
            registry, gpu_runtime, kernel_runtime.event_queues()) ==
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
            Read32(write_packet, 0) == 0xc0041054U &&
            Read32(write_packet, 4) == 0x01010102U &&
            Read32(write_packet, 8) == 0x45678980U &&
            Read32(write_packet, 12) == 3 &&
            Read32(write_packet, 16) == 0x55667788U &&
            Read32(write_packet, 20) == 0x99aabbccU,
        "write-data packet words are incorrect");

  constexpr auto kReleasePacket = kWritePacket + write_packet.size();
  const std::array release_arguments = {
      kCommandBuffer, std::uint64_t{0x2f}, std::uint64_t{0x100},
      std::uint64_t{0}, std::uint64_t{2}, std::uint64_t{0x456789a80ULL},
      std::uint64_t{2}, std::uint64_t{0x1122334455667788ULL},
      std::uint64_t{0}, std::uint64_t{0}, std::uint64_t{2},
      std::uint64_t{0x89abcdef}};
  SetArguments(context, release_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcCbReleaseMemNid, context) &&
            ReturnValue(context) == kReleasePacket,
        "release-memory packet call failed");
  std::array<std::byte, 32> release_packet{};
  Check(memory.Read(kReleasePacket, release_packet) &&
            Read32(release_packet, 0) == 0xc0061060U &&
            Read32(release_packet, 4) == 0x0430062fU &&
            Read32(release_packet, 8) == 0x42000000U &&
            Read32(release_packet, 12) == 0x56789a80U &&
            Read32(release_packet, 16) == 4U &&
            Read32(release_packet, 20) == 0x55667788U &&
            Read32(release_packet, 24) == 0x11223344U &&
            Read32(release_packet, 28) == 0x01abcdefU,
        "release-memory packet words are incorrect");

  constexpr auto kEventPacket = kReleasePacket + release_packet.size();
  const std::array event_arguments = {
      kCommandBuffer, std::uint64_t{7}, std::uint64_t{0}};
  SetArguments(context, event_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDcbEventWriteNid, context) &&
            ReturnValue(context) == kEventPacket,
        "event-write packet call failed");
  std::array<std::byte, 8> event_packet{};
  Check(memory.Read(kEventPacket, event_packet) &&
            Read32(event_packet, 0) == 0xc0004600U &&
            Read32(event_packet, 4) == 0x407U,
        "event-write packet words are incorrect");

  constexpr auto kWaitPacket = kEventPacket + event_packet.size();
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
  Check(registry.Dispatch(
            kajps5::hle::kAgcCbQueueEndOfPipeActionGetSizeNid, context) &&
            ReturnValue(context) == 32,
        "release-memory size query is incorrect");
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
  constexpr std::uint64_t kDriverWriteWaitCommands = kBase + 0x1e00;
  constexpr std::uint64_t kDriverReleaseWaitCommands = kBase + 0x1a00;
  constexpr std::uint64_t kDriverEventCommands = kBase + 0x1900;
  constexpr std::uint64_t kDriverLabel = kBase + 0x1f00;
  constexpr std::uint64_t kBatchAddresses = kBase + 0x500;
  constexpr std::uint64_t kBatchSizes = kBase + 0x520;
  constexpr std::uint64_t kBatchFirst = kBase + 0x1500;
  constexpr std::uint64_t kBatchSecond = kBase + 0x1600;
  const std::array driver_commands = {
      Pm4(5, 0x15), 2U, 3U, 4U, 0x41U,
  };
  const std::array driver_wait_commands = {
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kDriverLabel),
      static_cast<std::uint32_t>(kDriverLabel >> 32U), 0xffU, 0x66U,
      0x13U, 1U,
      Pm4(3, 0x2d), 7U, 2U,
  };
  const std::array driver_release_wait_commands = {
      Pm4(8, 0x10, 0x18), 0x62fU, 1U << 29U,
      static_cast<std::uint32_t>(kDriverLabel),
      static_cast<std::uint32_t>(kDriverLabel >> 32U), 0x99U, 0U, 0U,
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kDriverLabel),
      static_cast<std::uint32_t>(kDriverLabel >> 32U), 0xffU, 0x99U,
      0x13U, 1U,
      Pm4(3, 0x2d), 13U, 2U,
  };
  const std::array driver_event_commands = {
      Pm4(2, 0x46), 0x407U,
      Pm4(3, 0x2d), 17U, 2U,
  };
  const std::array batch_first = {Pm4(3, 0x2d), 18U, 2U};
  const std::array batch_second = {
      Pm4(5, 0x15), 4U, 3U, 2U, 0x41U};
  WriteDwords(memory, kDriverCommands, driver_commands);
  WriteDwords(memory, kDriverWaitCommands, driver_wait_commands);
  WriteDwords(memory, kDriverReleaseWaitCommands,
              driver_release_wait_commands);
  WriteDwords(memory, kDriverEventCommands, driver_event_commands);
  WriteDwords(memory, kBatchFirst, batch_first);
  WriteDwords(memory, kBatchSecond, batch_second);
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
            gpu_runtime.submission_history().At(0) != nullptr &&
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

  const std::array driver_write_wait_commands = {
      Pm4(5, 0x10, 0x15), 5U | (2U << 8U) | (1U << 24U),
      static_cast<std::uint32_t>(kDriverLabel),
      static_cast<std::uint32_t>(kDriverLabel >> 32U), 0x88U,
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kDriverLabel),
      static_cast<std::uint32_t>(kDriverLabel >> 32U), 0xffU, 0x88U,
      0x13U, 1U,
      Pm4(3, 0x2d), 11U, 2U,
  };
  WriteDwords(memory, kDriverWriteWaitCommands,
              driver_write_wait_commands);
  Check(context.WriteUInt32(kDriverLabel, 0) == HleContextStatus::kOk,
        "driver write label reset failed");
  Write64(driver_packet, 0, kDriverWriteWaitCommands);
  Write32(driver_packet, 8,
          static_cast<std::uint32_t>(driver_write_wait_commands.size()));
  Check(memory.Write(kDriverPacket, driver_packet),
        "write-wait driver packet setup failed");
  SetArguments(context, submit_dcb_arguments);
  std::uint32_t written_label = 0;
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == 0 &&
            context.ReadUInt32(kDriverLabel, written_label) ==
                HleContextStatus::kOk &&
            written_label == 0x88U &&
            gpu_runtime.submissions().PendingSubmissionCount() == 0 &&
            gpu_runtime.submission_history().size() == 7 &&
            gpu_runtime.submission_history().At(4)->type ==
                kajps5::gpu::GpuActionType::kWriteData &&
            gpu_runtime.submission_history().At(5)->type ==
                kajps5::gpu::GpuActionType::kWaitMemory &&
            gpu_runtime.submission_history().At(6)->type ==
                kajps5::gpu::GpuActionType::kDraw,
        "ordered write-data did not satisfy the following GPU wait");

  Check(context.WriteUInt32(kDriverLabel, 0) == HleContextStatus::kOk,
        "driver release label reset failed");
  Write64(driver_packet, 0, kDriverReleaseWaitCommands);
  Write32(driver_packet, 8,
          static_cast<std::uint32_t>(driver_release_wait_commands.size()));
  Check(memory.Write(kDriverPacket, driver_packet),
        "release-wait driver packet setup failed");
  SetArguments(context, submit_dcb_arguments);
  written_label = 0;
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == 0 &&
            context.ReadUInt32(kDriverLabel, written_label) ==
                HleContextStatus::kOk &&
            written_label == 0x99U &&
            gpu_runtime.submissions().PendingSubmissionCount() == 0 &&
            gpu_runtime.submission_history().size() == 10 &&
            gpu_runtime.submission_history().At(7)->type ==
                kajps5::gpu::GpuActionType::kReleaseMemory &&
            gpu_runtime.submission_history().At(8)->type ==
                kajps5::gpu::GpuActionType::kWaitMemory &&
            gpu_runtime.submission_history().At(9)->type ==
                kajps5::gpu::GpuActionType::kDraw,
        "ordered release-memory did not satisfy the following GPU wait");

  const auto graphics_queue =
      kernel_runtime.event_queues().Create("graphics");
  const std::array add_event_arguments = {
      graphics_queue.handle, std::uint64_t{0x20},
      std::uint64_t{0xdeadbeef}};
  SetArguments(context, add_event_arguments);
  Check(graphics_queue &&
            registry.Dispatch(kajps5::hle::kAgcDriverAddEqEventNid,
                              context) &&
            ReturnValue(context) == 0,
        "graphics event registration failed");
  Write64(driver_packet, 0, kDriverEventCommands);
  Write32(driver_packet, 8,
          static_cast<std::uint32_t>(driver_event_commands.size()));
  Check(memory.Write(kDriverPacket, driver_packet),
        "event driver packet setup failed");
  SetArguments(context, submit_dcb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == 0,
        "event command-buffer submission failed");
  auto graphics_event =
      kernel_runtime.event_queues().Poll(graphics_queue.handle, 1);
  Check(graphics_event && graphics_event.events.size() == 1 &&
            graphics_event.events[0].ident == 0x20 &&
            graphics_event.events[0].filter ==
                kajps5::kernel::kEventFilterGraphics &&
            graphics_event.events[0].fflags == 1 &&
            graphics_event.events[0].data == 7 &&
            graphics_event.events[0].user_data == 0xdeadbeef,
        "event-write did not publish the registered graphics event");
  const std::array delete_event_arguments = {
      graphics_queue.handle, std::uint64_t{0x20}};
  SetArguments(context, delete_event_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverDeleteEqEventNid,
                          context) &&
            ReturnValue(context) == 0,
        "graphics event deletion failed");
  SetArguments(context, submit_dcb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == 0 &&
            kernel_runtime.event_queues().Poll(graphics_queue.handle, 1)
                    .status == kajps5::kernel::KernelStatus::kBusy,
        "deleted graphics registration still received events");

  std::array<std::byte, 16> batch_addresses{};
  Write64(batch_addresses, 0, kBatchFirst);
  Write64(batch_addresses, 8, kBatchSecond);
  std::array<std::byte, 8> batch_sizes{};
  Write32(batch_sizes, 0,
          static_cast<std::uint32_t>(batch_first.size()));
  Write32(batch_sizes, 4,
          static_cast<std::uint32_t>(batch_second.size()));
  Check(memory.Write(kBatchAddresses, batch_addresses) &&
            memory.Write(kBatchSizes, batch_sizes),
        "driver batch array setup failed");
  const auto history_before_batches = gpu_runtime.submission_history().size();
  const std::array multi_dcb_arguments = {
      kBatchAddresses, kBatchSizes, std::uint64_t{2}};
  SetArguments(context, multi_dcb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitMultiDcbsNid,
                          context) &&
            ReturnValue(context) == 0,
        "multi-DCB submission failed");
  SetArguments(context, multi_dcb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverAgrSubmitMultiDcbsNid,
                          context) &&
            ReturnValue(context) == 0,
        "AGR multi-DCB submission failed");
  const std::array direct_arguments = {
      std::uint64_t{4}, kBatchFirst,
      static_cast<std::uint64_t>(batch_first.size())};
  SetArguments(context, direct_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitCommandBufferNid,
                          context) &&
            ReturnValue(context) == 0,
        "direct command-buffer submission failed");
  const std::array multi_command_arguments = {
      std::uint64_t{5}, kBatchAddresses, kBatchSizes,
      std::uint64_t{2}};
  SetArguments(context, multi_command_arguments);
  Check(registry.Dispatch(
            kajps5::hle::kAgcDriverSubmitMultiCommandBuffersNid,
            context) &&
            ReturnValue(context) == 0,
        "multi-command-buffer submission failed");
  const std::array multi_acb_arguments = {
      std::uint64_t{6}, kBatchAddresses, kBatchSizes,
      std::uint64_t{2}};
  SetArguments(context, multi_acb_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitMultiAcbsNid,
                          context) &&
            ReturnValue(context) == 0 &&
            gpu_runtime.submission_history().size() ==
                history_before_batches + 9 &&
            gpu_runtime.submissions().PendingSubmissionCount() == 0,
        "driver batch exports did not share the ordered queues");

  Write64(batch_addresses, 8, kBase + 0x4000);
  Check(memory.Write(kBatchAddresses, batch_addresses),
        "invalid driver batch setup failed");
  const auto history_before_invalid_batch =
      gpu_runtime.submission_history().size();
  SetArguments(context, multi_dcb_arguments);
  constexpr auto kMemoryFault = std::bit_cast<std::int32_t>(0x80020101U);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitMultiDcbsNid,
                          context) &&
            ReturnValue(context) == static_cast<std::uint64_t>(
                                        static_cast<std::int64_t>(
                                            kMemoryFault)) &&
            gpu_runtime.submission_history().size() ==
                history_before_invalid_batch &&
            gpu_runtime.submissions().PendingSubmissionCount() == 0,
        "invalid multi-DCB batch partly changed GPU state");

  const std::array invalid_submit_arguments = {std::uint64_t{0}};
  SetArguments(context, invalid_submit_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == static_cast<std::uint64_t>(
                                        static_cast<std::int64_t>(
                                            kInvalidArgument)),
        "null driver packet returned the wrong error");
  const std::array faulting_submit_arguments = {kBase + 0x2ff8};
  SetArguments(context, faulting_submit_arguments);
  Check(registry.Dispatch(kajps5::hle::kAgcDriverSubmitDcbNid, context) &&
            ReturnValue(context) == static_cast<std::uint64_t>(
                                        static_cast<std::int64_t>(
                                            kMemoryFault)),
        "faulting driver packet returned the wrong error");
  return 0;
}
