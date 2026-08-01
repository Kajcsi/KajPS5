// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
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
#include "hle/ampr_exports.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "kernel/ampr_command_buffer.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "hle_ampr_exports_test: " << message << '\n';
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

std::uint64_t Read64(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
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

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::kernel::AmprCommandBufferService;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x200000;
  constexpr std::uint64_t kCommandBuffer = kBase + 0x100;
  constexpr std::uint64_t kRecords = kBase + 0x400;
  constexpr std::uint64_t kWatcher = kBase + 0x1000;
  GuestMemory memory(
      kBase, 0x3000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  AmprCommandBufferService buffers;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterAmprExports(registry, buffers, memory) ==
                ExportRegistryStatus::kOk &&
            registry.size() == kajps5::hle::kRegisteredAmprFunctionCount * 2,
        "AMPR export registration failed");
  Check(registry.Lookup(kajps5::hle::kAmprAprCommandBufferReadFileNid).status ==
            ExportRegistryStatus::kNotFound,
        "unimplemented APR file streaming was registered as a stub");

  HleCallContext context(memory);
  const std::array constructor_arguments = {kCommandBuffer, kRecords,
                                            std::uint64_t{0x100}};
  SetArguments(context, constructor_arguments);
  const auto constructed =
      registry.Dispatch(kajps5::hle::kAmprCommandBufferConstructorNid, context);
  Check(constructed && ReturnValue(context) == kCommandBuffer,
        "command-buffer constructor dispatch failed");

  const std::array get_arguments = {kCommandBuffer};
  SetArguments(context, get_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferGetSizeNid, context) &&
            ReturnValue(context) == 0x100,
        "get-size returned the wrong value");
  Check(registry.Dispatch(
            kajps5::hle::kAmprMeasureCommandSizeWriteAddress0400Nid, context) &&
            ReturnValue(context) == kajps5::kernel::kAmprWriteAddressRecordSize,
        "write-address measurement is incorrect");

  constexpr std::uint64_t kWatcherValue = 0x8877665544332211ULL;
  const std::array write_arguments = {kCommandBuffer, kWatcher, kWatcherValue};
  SetArguments(context, write_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferWriteAddress0400Nid,
                          context) &&
            ReturnValue(context) == 0,
        "write-address command dispatch failed");
  SetArguments(context, get_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferGetCurrentOffsetNid,
                          context) &&
            ReturnValue(context) == kajps5::kernel::kAmprWriteAddressRecordSize,
        "current offset did not advance");
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferGetNumCommandsNid,
                          context) &&
            ReturnValue(context) == 1,
        "command count did not advance");
  Check(buffers.Complete(memory, kCommandBuffer) ==
            kajps5::kernel::AmprCommandBufferStatus::kOk,
        "write-address completion failed");
  std::array<std::byte, sizeof(std::uint64_t)> watcher{};
  Check(memory.Read(kWatcher, watcher) && Read64(watcher, 0) == kWatcherValue,
        "write-address completion stored the wrong value");

  SetArguments(context, get_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferResetNid, context) &&
            ReturnValue(context) == 0,
        "reset dispatch failed");
  const std::array event_arguments = {kCommandBuffer, std::uint64_t{0x44},
                                      std::uint64_t{0x55}, std::uint64_t{0x66},
                                      std::uint64_t{0x77}};
  SetArguments(context, event_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferWriteEventQueue0400Nid,
                          context) &&
            ReturnValue(context) == 0,
        "event-queue command dispatch failed");
  std::array<std::byte, kajps5::kernel::kAmprEventQueueRecordSize> event{};
  Check(memory.Read(kRecords, event) && Read32(event, 0x00) == 2 &&
            static_cast<std::int16_t>(Read32(event, 0x04)) == -16 &&
            Read64(event, 0x08) == 0x44 && Read64(event, 0x10) == 0x55 &&
            Read64(event, 0x18) == 0x77 && Read64(event, 0x20) == 0x66,
        "event-queue record layout is incorrect");

  const std::array clear_arguments = {kCommandBuffer};
  SetArguments(context, clear_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprCommandBufferClearBufferNid,
                          context) &&
            ReturnValue(context) == kRecords,
        "clear-buffer did not return the record pointer");

  const std::array invalid_arguments = {std::uint64_t{0}};
  SetArguments(context, invalid_arguments);
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kAmprCommandBufferGetSizeNid, context)),
        "invalid get-size dispatch failed");
  constexpr auto kInvalidArgument = std::bit_cast<std::int32_t>(0x80020003U);
  Check(
      ReturnValue(context) == static_cast<std::uint64_t>(
                                  static_cast<std::int64_t>(kInvalidArgument)),
      "invalid command buffer returned the wrong guest error");

  std::array<std::byte, kajps5::kernel::kAmprCommandBufferHeaderSize>
      apr_header{};
  constexpr auto kAprCommandBuffer = kCommandBuffer + 0x80;
  for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
    apr_header[0x08 + index] =
        static_cast<std::byte>((kRecords >> (index * 8U)) & 0xffU);
    apr_header[0x10 + index] =
        static_cast<std::byte>((0x100ULL >> (index * 8U)) & 0xffU);
  }
  Check(memory.Write(kAprCommandBuffer, apr_header), "APR header setup failed");
  const std::array apr_arguments = {kAprCommandBuffer, std::uint64_t{0xaaaa},
                                    std::uint64_t{0xbbbb}};
  SetArguments(context, apr_arguments);
  Check(registry.Dispatch(kajps5::hle::kAmprAprCommandBufferConstructorNid,
                          context) &&
            ReturnValue(context) == kAprCommandBuffer &&
            memory.Read(kAprCommandBuffer, apr_header) &&
            Read64(apr_header, 0x08) == kRecords &&
            Read64(apr_header, 0x10) == 0x100 &&
            Read64(apr_header, 0x18) == 0xaaaa &&
            Read64(apr_header, 0x20) == 0xbbbb,
        "APR constructor did not preserve visible buffer state");
  return 0;
}
