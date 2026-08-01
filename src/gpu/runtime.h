// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and
// src/graphics/guest_gpu/pm4.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::gpu {

inline constexpr std::size_t kAgcCommandBufferSize = 0x38;
inline constexpr std::size_t kAgcCommandBufferCursorUpOffset = 0x10;
inline constexpr std::size_t kAgcCommandBufferCursorDownOffset = 0x18;
inline constexpr std::size_t kAgcCommandBufferCallbackOffset = 0x20;
inline constexpr std::size_t kAgcCommandBufferUserDataOffset = 0x28;
inline constexpr std::size_t kAgcCommandBufferReservedDwordsOffset = 0x30;
inline constexpr std::uint32_t kMaximumPm4PacketDwords = 0x4001;

enum class GpuRuntimeStatus {
  kOk,
  kInvalidArgument,
  kMemoryFault,
  kBufferTooSmall,
  kCallbackRequired,
  kResourceLimit,
};

struct GpuPacketResult {
  GpuRuntimeStatus status = GpuRuntimeStatus::kOk;
  std::uint64_t address = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GpuRuntimeStatus::kOk;
  }
};

struct GpuPacketSizeResult {
  GpuRuntimeStatus status = GpuRuntimeStatus::kOk;
  std::uint32_t dwords = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GpuRuntimeStatus::kOk;
  }
};

class GpuRuntime final {
 public:
  explicit GpuRuntime(memory::GuestMemory& memory) noexcept;

  [[nodiscard]] GpuPacketResult WriteNop(std::uint64_t command_buffer,
                                         std::uint32_t dword_count);
  [[nodiscard]] GpuPacketResult WriteDispatch(
      std::uint64_t command_buffer, std::uint32_t group_count_x,
      std::uint32_t group_count_y, std::uint32_t group_count_z,
      std::uint32_t modifier);
  [[nodiscard]] GpuPacketSizeResult GetPacketSize(
      std::uint64_t packet_address) const noexcept;
  [[nodiscard]] GpuRuntimeStatus SetPacketPredication(
      std::uint64_t packet_address, std::uint32_t predication) noexcept;

 private:
  [[nodiscard]] GpuPacketResult AppendPacket(
      std::uint64_t command_buffer, std::span<const std::uint32_t> packet);

  memory::GuestMemory& memory_;
  mutable std::mutex mutex_;
};

[[nodiscard]] const char* GpuRuntimeStatusName(
    GpuRuntimeStatus status) noexcept;

}  // namespace kajps5::gpu
