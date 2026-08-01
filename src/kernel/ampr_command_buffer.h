// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::kernel {

inline constexpr std::size_t kAmprCommandBufferHeaderSize = 0x28;
inline constexpr std::size_t kAmprReadFileRecordSize = 0x30;
inline constexpr std::size_t kAmprEventQueueRecordSize = 0x30;
inline constexpr std::size_t kAmprWriteAddressRecordSize = 0x20;
inline constexpr std::size_t kMaximumAmprCommandBuffers = 4096;

enum class AmprCommandBufferStatus {
  kOk,
  kInvalidArgument,
  kMemoryFault,
  kNoResources,
  kBufferTooSmall,
  kUnsupportedRecord,
};

struct AmprCommandBufferSnapshot {
  AmprCommandBufferStatus status = AmprCommandBufferStatus::kOk;
  std::uint64_t buffer = 0;
  std::uint64_t size = 0;
  std::uint64_t write_offset = 0;
  std::uint64_t command_count = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == AmprCommandBufferStatus::kOk;
  }
};

struct AmprCommandBufferValueResult {
  AmprCommandBufferStatus status = AmprCommandBufferStatus::kOk;
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == AmprCommandBufferStatus::kOk;
  }
};

class AmprCommandBufferService final {
 public:
  [[nodiscard]] AmprCommandBufferStatus Construct(
      memory::GuestMemory& memory, std::uint64_t command_buffer,
      std::uint64_t buffer, std::uint64_t size, std::uint64_t auxiliary_0 = 0,
      std::uint64_t auxiliary_1 = 0, bool preserve_buffer = false);
  [[nodiscard]] AmprCommandBufferStatus Destroy(memory::GuestMemory& memory,
                                                std::uint64_t command_buffer);
  [[nodiscard]] AmprCommandBufferStatus DestroyApr(
      memory::GuestMemory& memory, std::uint64_t command_buffer);
  [[nodiscard]] AmprCommandBufferStatus SetBuffer(memory::GuestMemory& memory,
                                                  std::uint64_t command_buffer,
                                                  std::uint64_t buffer,
                                                  std::uint64_t size);
  [[nodiscard]] AmprCommandBufferStatus Reset(memory::GuestMemory& memory,
                                              std::uint64_t command_buffer);
  [[nodiscard]] AmprCommandBufferValueResult Clear(
      memory::GuestMemory& memory, std::uint64_t command_buffer);
  [[nodiscard]] AmprCommandBufferSnapshot Snapshot(
      memory::GuestMemory& memory, std::uint64_t command_buffer);
  [[nodiscard]] AmprCommandBufferStatus Append(
      memory::GuestMemory& memory, std::uint64_t command_buffer,
      std::span<const std::byte> record);
  [[nodiscard]] AmprCommandBufferStatus Complete(memory::GuestMemory& memory,
                                                 std::uint64_t command_buffer);
  [[nodiscard]] std::size_t size() const;

 private:
  struct State {
    std::uint64_t buffer = 0;
    std::uint64_t size = 0;
    std::uint64_t write_offset = 0;
    std::uint64_t command_count = 0;
  };

  [[nodiscard]] AmprCommandBufferStatus LoadStateLocked(
      memory::GuestMemory& memory, std::uint64_t command_buffer, State*& state);

  mutable std::mutex mutex_;
  std::map<std::uint64_t, State> states_;
};

[[nodiscard]] const char* AmprCommandBufferStatusName(
    AmprCommandBufferStatus status) noexcept;

}  // namespace kajps5::kernel
