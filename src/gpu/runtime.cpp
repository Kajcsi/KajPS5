// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and
// src/graphics/guest_gpu/pm4.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/runtime.h"

#include <array>
#include <limits>
#include <new>
#include <vector>

#include "core/memory/guest_memory.h"

namespace kajps5::gpu {
namespace {

constexpr std::uint32_t kPm4Type3 = 0xc0000000U;
constexpr std::uint32_t kPm4LengthMask = 0x3fffU;
constexpr std::uint32_t kPm4RegisterMask = 0x3fU;
constexpr std::uint32_t kPm4NopOpcode = 0x10U;
constexpr std::uint32_t kPm4DispatchDirectOpcode = 0x15U;
constexpr std::uint32_t kDirectDispatchModifierMask = 0xa038U;
constexpr std::uint32_t kDirectDispatchRequiredBits = 0x41U;

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

std::uint32_t MakePm4Header(std::uint32_t dword_count,
                           std::uint32_t opcode,
                           std::uint32_t packet_register) noexcept {
  return kPm4Type3 |
         (((dword_count - 2U) & kPm4LengthMask) << 16U) |
         ((opcode & 0xffU) << 8U) |
         ((packet_register & kPm4RegisterMask) << 2U);
}

std::uint32_t Read32(std::span<const std::byte> bytes) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

}  // namespace

GpuRuntime::GpuRuntime(memory::GuestMemory& memory) noexcept : memory_(memory) {}

GpuPacketResult GpuRuntime::AppendPacket(
    std::uint64_t command_buffer, std::span<const std::uint32_t> packet) {
  if (command_buffer == 0 || packet.size() < 2 ||
      packet.size() > kMaximumPm4PacketDwords) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }

  std::lock_guard lock(mutex_);
  std::uint64_t cursor_up_address = 0;
  std::uint64_t cursor_down_address = 0;
  std::uint64_t callback_address = 0;
  std::uint64_t reserved_address = 0;
  if (!Add(command_buffer, kAgcCommandBufferCursorUpOffset,
           cursor_up_address) ||
      !Add(command_buffer, kAgcCommandBufferCursorDownOffset,
           cursor_down_address) ||
      !Add(command_buffer, kAgcCommandBufferCallbackOffset,
           callback_address) ||
      !Add(command_buffer, kAgcCommandBufferReservedDwordsOffset,
           reserved_address)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  std::array<std::byte, sizeof(std::uint64_t)> cursor_up_bytes{};
  std::array<std::byte, sizeof(std::uint64_t)> cursor_down_bytes{};
  std::array<std::byte, sizeof(std::uint64_t)> callback_bytes{};
  std::array<std::byte, sizeof(std::uint32_t)> reserved_bytes{};
  if (!memory_.Read(cursor_up_address, cursor_up_bytes) ||
      !memory_.Read(cursor_down_address, cursor_down_bytes) ||
      !memory_.Read(callback_address, callback_bytes) ||
      !memory_.Read(reserved_address, reserved_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  const auto cursor_up = Read64(cursor_up_bytes);
  const auto cursor_down = Read64(cursor_down_bytes);
  const auto callback = Read64(callback_bytes);
  const auto reserved_dwords = Read32(reserved_bytes);
  if (cursor_up == 0 || cursor_down <= cursor_up ||
      (cursor_up & 3U) != 0 || (cursor_down & 3U) != 0) {
    return {GpuRuntimeStatus::kBufferTooSmall};
  }

  const auto available_bytes = cursor_down - cursor_up;
  const auto available_dwords = available_bytes / sizeof(std::uint32_t);
  const auto usable_dwords = available_dwords > reserved_dwords
                                 ? available_dwords - reserved_dwords
                                 : 0;
  if (packet.size() > usable_dwords) {
    return {callback != 0 ? GpuRuntimeStatus::kCallbackRequired
                          : GpuRuntimeStatus::kBufferTooSmall};
  }

  const auto packet_bytes_size =
      static_cast<std::uint64_t>(packet.size() * sizeof(std::uint32_t));
  std::uint64_t next_cursor = 0;
  if (!Add(cursor_up, packet_bytes_size, next_cursor) ||
      !memory_.CanAccess(cursor_up, packet_bytes_size,
                         memory::GuestMemoryProtection::kWrite) ||
      !memory_.CanAccess(cursor_up_address, sizeof(std::uint64_t),
                         memory::GuestMemoryProtection::kWrite)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  std::vector<std::byte> packet_bytes;
  try {
    packet_bytes.resize(static_cast<std::size_t>(packet_bytes_size));
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  for (std::size_t index = 0; index < packet.size(); ++index) {
    Write32(packet_bytes, index * sizeof(std::uint32_t), packet[index]);
  }
  std::array<std::byte, sizeof(std::uint64_t)> next_cursor_bytes{};
  Write64(next_cursor_bytes, next_cursor);
  if (!memory_.Write(cursor_up, packet_bytes) ||
      !memory_.Write(cursor_up_address, next_cursor_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }
  return {GpuRuntimeStatus::kOk, cursor_up};
}

GpuPacketResult GpuRuntime::WriteNop(std::uint64_t command_buffer,
                                     std::uint32_t dword_count) {
  if (dword_count < 2 || dword_count > kMaximumPm4PacketDwords) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  std::vector<std::uint32_t> packet;
  try {
    packet.assign(dword_count, 0);
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  packet[0] = MakePm4Header(dword_count, kPm4NopOpcode, 0);
  return AppendPacket(command_buffer, packet);
}

GpuPacketResult GpuRuntime::WriteDispatch(
    std::uint64_t command_buffer, std::uint32_t group_count_x,
    std::uint32_t group_count_y, std::uint32_t group_count_z,
    std::uint32_t modifier) {
  const std::array packet = {
      MakePm4Header(5, kPm4DispatchDirectOpcode, 0), group_count_x,
      group_count_y, group_count_z,
      (modifier & kDirectDispatchModifierMask) |
          kDirectDispatchRequiredBits};
  return AppendPacket(command_buffer, packet);
}

GpuPacketSizeResult GpuRuntime::GetPacketSize(
    std::uint64_t packet_address) const noexcept {
  if (packet_address == 0) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  std::array<std::byte, sizeof(std::uint32_t)> header_bytes{};
  if (!memory_.Read(packet_address, header_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }
  const auto header = Read32(header_bytes);
  if ((header & 0x3fffff00U) == 0x3fff1000U) {
    return {GpuRuntimeStatus::kOk, 1};
  }
  return {GpuRuntimeStatus::kOk,
          static_cast<std::uint32_t>(((header >> 16U) & 0x3fffU) + 2U)};
}

GpuRuntimeStatus GpuRuntime::SetPacketPredication(
    std::uint64_t packet_address, std::uint32_t predication) noexcept {
  if (packet_address == 0) {
    return GpuRuntimeStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  std::array<std::byte, sizeof(std::uint32_t)> header_bytes{};
  if (!memory_.Read(packet_address, header_bytes)) {
    return GpuRuntimeStatus::kMemoryFault;
  }
  auto header = Read32(header_bytes);
  header = (header & ~1U) | (predication == 1 ? 1U : 0U);
  Write32(header_bytes, 0, header);
  return memory_.Write(packet_address, header_bytes)
             ? GpuRuntimeStatus::kOk
             : GpuRuntimeStatus::kMemoryFault;
}

const char* GpuRuntimeStatusName(GpuRuntimeStatus status) noexcept {
  switch (status) {
    case GpuRuntimeStatus::kOk:
      return "ok";
    case GpuRuntimeStatus::kInvalidArgument:
      return "invalid-argument";
    case GpuRuntimeStatus::kMemoryFault:
      return "memory-fault";
    case GpuRuntimeStatus::kBufferTooSmall:
      return "buffer-too-small";
    case GpuRuntimeStatus::kCallbackRequired:
      return "callback-required";
    case GpuRuntimeStatus::kResourceLimit:
      return "resource-limit";
  }
  return "unknown";
}

}  // namespace kajps5::gpu
