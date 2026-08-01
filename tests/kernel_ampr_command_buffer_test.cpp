// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include "core/memory/guest_memory.h"
#include "kernel/ampr_command_buffer.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "kernel_ampr_command_buffer_test: " << message << '\n';
    std::exit(1);
  }
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
  using kajps5::kernel::AmprCommandBufferService;
  using kajps5::kernel::AmprCommandBufferStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x100000;
  constexpr std::uint64_t kCommandBuffer = kBase + 0x100;
  constexpr std::uint64_t kRecords = kBase + 0x400;
  constexpr std::uint64_t kWatcher = kBase + 0x1000;
  GuestMemory memory(
      kBase, 0x3000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  AmprCommandBufferService buffers;

  Check(buffers.Construct(memory, kCommandBuffer, kRecords, 0x100) ==
            AmprCommandBufferStatus::kOk,
        "constructor failed");
  std::array<std::byte, kajps5::kernel::kAmprCommandBufferHeaderSize> header{};
  Check(memory.Read(kCommandBuffer, header) &&
            Read64(header, 0x00) == kCommandBuffer &&
            Read64(header, 0x08) == kRecords && Read64(header, 0x10) == 0x100 &&
            Read64(header, 0x18) == 0 && Read64(header, 0x20) == 0,
        "constructor header is incorrect");

  std::array<std::byte, kajps5::kernel::kAmprReadFileRecordSize> read_record{};
  Write32(read_record, 0, 1);
  Check(buffers.Append(memory, kCommandBuffer, read_record) ==
            AmprCommandBufferStatus::kOk,
        "read record append failed");

  constexpr std::uint64_t kWatcherValue = 0x123456789abcdef0ULL;
  std::array<std::byte, kajps5::kernel::kAmprWriteAddressRecordSize>
      write_record{};
  Write32(write_record, 0, 3);
  Write64(write_record, 0x08, kWatcher);
  Write64(write_record, 0x10, kWatcherValue);
  Check(buffers.Append(memory, kCommandBuffer, write_record) ==
            AmprCommandBufferStatus::kOk,
        "write-address record append failed");
  const auto appended = buffers.Snapshot(memory, kCommandBuffer);
  Check(
      appended && appended.write_offset == 0x50 && appended.command_count == 2,
      "append accounting is incorrect");
  Check(
      buffers.Complete(memory, kCommandBuffer) == AmprCommandBufferStatus::kOk,
      "command completion failed");
  std::array<std::byte, sizeof(std::uint64_t)> watcher{};
  Check(memory.Read(kWatcher, watcher) && Read64(watcher, 0) == kWatcherValue,
        "write-address completion did not update guest memory");

  Check(buffers.Reset(memory, kCommandBuffer) == AmprCommandBufferStatus::kOk,
        "reset failed");
  const auto reset = buffers.Snapshot(memory, kCommandBuffer);
  Check(reset && reset.buffer == kRecords && reset.size == 0x100 &&
            reset.write_offset == 0 && reset.command_count == 0,
        "reset did not preserve the buffer");

  Check(buffers.SetBuffer(memory, kCommandBuffer, kRecords, 0x10) ==
            AmprCommandBufferStatus::kOk,
        "set-buffer failed");
  Check(buffers.Append(memory, kCommandBuffer, write_record) ==
            AmprCommandBufferStatus::kBufferTooSmall,
        "undersized command buffer accepted a record");
  const auto rejected = buffers.Snapshot(memory, kCommandBuffer);
  Check(rejected && rejected.write_offset == 0 && rejected.command_count == 0,
        "rejected append changed command-buffer state");

  const auto cleared = buffers.Clear(memory, kCommandBuffer);
  Check(cleared && cleared.value == kRecords && buffers.size() == 0,
        "clear did not return and release the record buffer");
  std::array<std::byte, sizeof(std::uint64_t) * 3> visible{};
  Check(memory.Read(kCommandBuffer, visible) &&
            Read64(visible, 0) == kCommandBuffer && Read64(visible, 8) == 0 &&
            Read64(visible, 16) == 0,
        "clear did not reset visible pointers");

  Write64(header, 0x08, kRecords);
  Write64(header, 0x10, 0x100);
  Check(memory.Write(kCommandBuffer, header), "APR setup header write failed");
  Check(buffers.Construct(memory, kCommandBuffer, 0, 0, 0x1111, 0x2222, true) ==
                AmprCommandBufferStatus::kOk &&
            memory.Read(kCommandBuffer, header) &&
            Read64(header, 0x08) == kRecords && Read64(header, 0x10) == 0x100 &&
            Read64(header, 0x18) == 0x1111 && Read64(header, 0x20) == 0x2222,
        "APR constructor did not preserve record pointers");
  Check(buffers.DestroyApr(memory, kCommandBuffer) ==
                AmprCommandBufferStatus::kOk &&
            memory.Read(kCommandBuffer, header) &&
            Read64(header, 0x08) == kRecords && Read64(header, 0x18) == 0 &&
            Read64(header, 0x20) == 0,
        "APR destructor changed the wrong fields");
  Check(
      buffers.Destroy(memory, kCommandBuffer) == AmprCommandBufferStatus::kOk &&
          buffers.size() == 0,
      "destructor failed");

  Check(buffers.Construct(memory, kBase + 0x4000, kRecords, 0x100) ==
            AmprCommandBufferStatus::kMemoryFault,
        "unmapped command-buffer memory was accepted");
  Check(
      std::string_view(kajps5::kernel::AmprCommandBufferStatusName(
          AmprCommandBufferStatus::kUnsupportedRecord)) == "unsupported-record",
      "status name is unstable");
  return 0;
}
