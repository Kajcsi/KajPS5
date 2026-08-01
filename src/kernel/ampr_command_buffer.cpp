// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/ampr_command_buffer.h"

#include <array>
#include <limits>
#include <utility>

#include "core/memory/guest_memory.h"

namespace kajps5::kernel {
namespace {

constexpr std::size_t kSelfOffset = 0x00;
constexpr std::size_t kDataOffset = 0x08;
constexpr std::size_t kSizeOffset = 0x10;
constexpr std::size_t kAuxiliary0Offset = 0x18;
constexpr std::size_t kAuxiliary1Offset = 0x20;
constexpr std::uint32_t kReadFileRecordType = 1;
constexpr std::uint32_t kEventQueueRecordType = 2;
constexpr std::uint32_t kWriteAddressRecordType = 3;

void Write64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::uint32_t Read32(std::span<const std::byte> bytes,
                     std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes,
                     std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

std::array<std::byte, sizeof(std::uint64_t) * 3> VisiblePointers(
    std::uint64_t command_buffer, std::uint64_t buffer,
    std::uint64_t size) noexcept {
  std::array<std::byte, sizeof(std::uint64_t) * 3> pointers{};
  Write64(pointers, kSelfOffset, command_buffer);
  Write64(pointers, kDataOffset, buffer);
  Write64(pointers, kSizeOffset, size);
  return pointers;
}

}  // namespace

AmprCommandBufferStatus AmprCommandBufferService::Construct(
    memory::GuestMemory& memory, std::uint64_t command_buffer,
    std::uint64_t buffer, std::uint64_t size, std::uint64_t auxiliary_0,
    std::uint64_t auxiliary_1, bool preserve_buffer) {
  if (command_buffer == 0) {
    return AmprCommandBufferStatus::kInvalidArgument;
  }

  std::array<std::byte, kAmprCommandBufferHeaderSize> header{};
  if (preserve_buffer) {
    if (!memory.Read(command_buffer, header)) {
      return AmprCommandBufferStatus::kMemoryFault;
    }
    buffer = Read64(header, kDataOffset);
    size = Read64(header, kSizeOffset);
  }
  Write64(header, kSelfOffset, command_buffer);
  Write64(header, kDataOffset, buffer);
  Write64(header, kSizeOffset, size);
  Write64(header, kAuxiliary0Offset, auxiliary_0);
  Write64(header, kAuxiliary1Offset, auxiliary_1);

  std::lock_guard lock(mutex_);
  auto existing = states_.find(command_buffer);
  const bool inserted = existing == states_.end();
  if (inserted && states_.size() >= kMaximumAmprCommandBuffers) {
    return AmprCommandBufferStatus::kNoResources;
  }
  try {
    if (inserted) {
      existing = states_.try_emplace(command_buffer).first;
    }
  } catch (...) {
    return AmprCommandBufferStatus::kNoResources;
  }
  if (!memory.Write(command_buffer, header)) {
    if (inserted) {
      states_.erase(existing);
    }
    return AmprCommandBufferStatus::kMemoryFault;
  }
  existing->second = {buffer, size, 0, 0};
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferStatus AmprCommandBufferService::Destroy(
    memory::GuestMemory& memory, std::uint64_t command_buffer) {
  if (command_buffer == 0) {
    return AmprCommandBufferStatus::kOk;
  }
  const auto pointers = VisiblePointers(command_buffer, 0, 0);
  if (!memory.Write(command_buffer, pointers)) {
    return AmprCommandBufferStatus::kMemoryFault;
  }
  std::lock_guard lock(mutex_);
  states_.erase(command_buffer);
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferStatus AmprCommandBufferService::DestroyApr(
    memory::GuestMemory& memory, std::uint64_t command_buffer) {
  if (command_buffer == 0) {
    return AmprCommandBufferStatus::kOk;
  }
  std::array<std::byte, sizeof(std::uint64_t) * 2> zeros{};
  std::uint64_t address = 0;
  if (!Add(command_buffer, kAuxiliary0Offset, address) ||
      !memory.Write(address, zeros)) {
    return AmprCommandBufferStatus::kMemoryFault;
  }
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferStatus AmprCommandBufferService::SetBuffer(
    memory::GuestMemory& memory, std::uint64_t command_buffer,
    std::uint64_t buffer, std::uint64_t size) {
  if (command_buffer == 0) {
    return AmprCommandBufferStatus::kInvalidArgument;
  }
  const auto pointers = VisiblePointers(command_buffer, buffer, size);

  std::lock_guard lock(mutex_);
  auto existing = states_.find(command_buffer);
  const bool inserted = existing == states_.end();
  if (inserted && states_.size() >= kMaximumAmprCommandBuffers) {
    return AmprCommandBufferStatus::kNoResources;
  }
  try {
    if (inserted) {
      existing = states_.try_emplace(command_buffer).first;
    }
  } catch (...) {
    return AmprCommandBufferStatus::kNoResources;
  }
  if (!memory.Write(command_buffer, pointers)) {
    if (inserted) {
      states_.erase(existing);
    }
    return AmprCommandBufferStatus::kMemoryFault;
  }
  existing->second = {buffer, size, 0, 0};
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferStatus AmprCommandBufferService::LoadStateLocked(
    memory::GuestMemory& memory, std::uint64_t command_buffer, State*& state) {
  const auto found = states_.find(command_buffer);
  if (found != states_.end()) {
    state = &found->second;
    return AmprCommandBufferStatus::kOk;
  }
  if (states_.size() >= kMaximumAmprCommandBuffers) {
    state = nullptr;
    return AmprCommandBufferStatus::kNoResources;
  }

  std::uint64_t address = 0;
  if (!Add(command_buffer, kDataOffset, address)) {
    state = nullptr;
    return AmprCommandBufferStatus::kMemoryFault;
  }
  std::array<std::byte, sizeof(std::uint64_t) * 2> pointers{};
  if (!memory.Read(address, pointers)) {
    state = nullptr;
    return AmprCommandBufferStatus::kMemoryFault;
  }
  try {
    const auto [entry, inserted] = states_.try_emplace(
        command_buffer, State{Read64(pointers, 0),
                              Read64(pointers, sizeof(std::uint64_t)), 0, 0});
    (void)inserted;
    state = &entry->second;
  } catch (...) {
    state = nullptr;
    return AmprCommandBufferStatus::kNoResources;
  }
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferStatus AmprCommandBufferService::Reset(
    memory::GuestMemory& memory, std::uint64_t command_buffer) {
  if (command_buffer == 0) {
    return AmprCommandBufferStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  State* state = nullptr;
  const auto loaded = LoadStateLocked(memory, command_buffer, state);
  if (loaded != AmprCommandBufferStatus::kOk) {
    return loaded;
  }
  const auto pointers =
      VisiblePointers(command_buffer, state->buffer, state->size);
  if (!memory.Write(command_buffer, pointers)) {
    return AmprCommandBufferStatus::kMemoryFault;
  }
  state->write_offset = 0;
  state->command_count = 0;
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferValueResult AmprCommandBufferService::Clear(
    memory::GuestMemory& memory, std::uint64_t command_buffer) {
  if (command_buffer == 0) {
    return {AmprCommandBufferStatus::kInvalidArgument, 0};
  }
  std::lock_guard lock(mutex_);
  State* state = nullptr;
  const auto loaded = LoadStateLocked(memory, command_buffer, state);
  if (loaded != AmprCommandBufferStatus::kOk) {
    return {loaded, 0};
  }
  const auto buffer = state->buffer;
  const auto pointers = VisiblePointers(command_buffer, 0, 0);
  if (!memory.Write(command_buffer, pointers)) {
    return {AmprCommandBufferStatus::kMemoryFault, 0};
  }
  states_.erase(command_buffer);
  return {AmprCommandBufferStatus::kOk, buffer};
}

AmprCommandBufferSnapshot AmprCommandBufferService::Snapshot(
    memory::GuestMemory& memory, std::uint64_t command_buffer) {
  if (command_buffer == 0) {
    return {AmprCommandBufferStatus::kInvalidArgument};
  }
  std::lock_guard lock(mutex_);
  State* state = nullptr;
  const auto loaded = LoadStateLocked(memory, command_buffer, state);
  if (loaded != AmprCommandBufferStatus::kOk) {
    return {loaded};
  }
  return {AmprCommandBufferStatus::kOk, state->buffer, state->size,
          state->write_offset, state->command_count};
}

AmprCommandBufferStatus AmprCommandBufferService::Append(
    memory::GuestMemory& memory, std::uint64_t command_buffer,
    std::span<const std::byte> record) {
  if (command_buffer == 0 || record.empty()) {
    return AmprCommandBufferStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  State* state = nullptr;
  const auto loaded = LoadStateLocked(memory, command_buffer, state);
  if (loaded != AmprCommandBufferStatus::kOk) {
    return loaded;
  }
  const auto record_size = static_cast<std::uint64_t>(record.size());
  if (state->buffer == 0 || state->write_offset > state->size ||
      record_size > state->size - state->write_offset) {
    return AmprCommandBufferStatus::kBufferTooSmall;
  }
  std::uint64_t write_address = 0;
  if (!Add(state->buffer, state->write_offset, write_address) ||
      !memory.Write(write_address, record)) {
    return AmprCommandBufferStatus::kMemoryFault;
  }
  state->write_offset += record_size;
  ++state->command_count;
  return AmprCommandBufferStatus::kOk;
}

AmprCommandBufferStatus AmprCommandBufferService::Complete(
    memory::GuestMemory& memory, std::uint64_t command_buffer) {
  if (command_buffer == 0) {
    return AmprCommandBufferStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  State* state = nullptr;
  const auto loaded = LoadStateLocked(memory, command_buffer, state);
  if (loaded != AmprCommandBufferStatus::kOk) {
    return loaded;
  }

  std::uint64_t offset = 0;
  while (offset < state->write_offset) {
    std::uint64_t record_address = 0;
    if (!Add(state->buffer, offset, record_address)) {
      return AmprCommandBufferStatus::kMemoryFault;
    }
    std::array<std::byte, sizeof(std::uint32_t)> type_bytes{};
    if (!memory.Read(record_address, type_bytes)) {
      return AmprCommandBufferStatus::kMemoryFault;
    }
    const auto type = Read32(type_bytes, 0);
    std::uint64_t record_size = 0;
    if (type == kReadFileRecordType) {
      record_size = kAmprReadFileRecordSize;
    } else if (type == kEventQueueRecordType) {
      return AmprCommandBufferStatus::kUnsupportedRecord;
    } else if (type == kWriteAddressRecordType) {
      record_size = kAmprWriteAddressRecordSize;
      if (record_size > state->write_offset - offset) {
        return AmprCommandBufferStatus::kMemoryFault;
      }
      std::array<std::byte, kAmprWriteAddressRecordSize> record{};
      if (!memory.Read(record_address, record)) {
        return AmprCommandBufferStatus::kMemoryFault;
      }
      const auto destination = Read64(record, 0x08);
      const auto value = Read64(record, 0x10);
      std::array<std::byte, sizeof(value)> bytes{};
      Write64(bytes, 0, value);
      if (destination == 0 || !memory.Write(destination, bytes)) {
        return AmprCommandBufferStatus::kMemoryFault;
      }
    } else {
      return AmprCommandBufferStatus::kUnsupportedRecord;
    }
    if (record_size > state->write_offset - offset) {
      return AmprCommandBufferStatus::kMemoryFault;
    }
    offset += record_size;
  }
  return AmprCommandBufferStatus::kOk;
}

std::size_t AmprCommandBufferService::size() const {
  std::lock_guard lock(mutex_);
  return states_.size();
}

const char* AmprCommandBufferStatusName(
    AmprCommandBufferStatus status) noexcept {
  switch (status) {
    case AmprCommandBufferStatus::kOk:
      return "ok";
    case AmprCommandBufferStatus::kInvalidArgument:
      return "invalid-argument";
    case AmprCommandBufferStatus::kMemoryFault:
      return "memory-fault";
    case AmprCommandBufferStatus::kNoResources:
      return "no-resources";
    case AmprCommandBufferStatus::kBufferTooSmall:
      return "buffer-too-small";
    case AmprCommandBufferStatus::kUnsupportedRecord:
      return "unsupported-record";
  }
  return "unknown";
}

}  // namespace kajps5::kernel
