// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/call_context.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <utility>

namespace kajps5::hle {
namespace {

constexpr std::array kArgumentRegisters = {
    HleRegister::kRdi, HleRegister::kRsi, HleRegister::kRdx,
    HleRegister::kRcx, HleRegister::kR8,  HleRegister::kR9};
constexpr std::size_t kStringReadChunkBytes = 128;

bool AddAddressOffset(std::uint64_t address, std::size_t offset,
                      std::uint64_t& result) noexcept {
  if (offset > std::numeric_limits<std::uint64_t>::max() - address) {
    return false;
  }
  result = address + static_cast<std::uint64_t>(offset);
  return true;
}

}  // namespace

HleCallContext::HleCallContext(memory::GuestMemory& memory) noexcept
    : memory_(memory) {}

std::optional<std::size_t> HleCallContext::RegisterIndex(
    HleRegister reg) noexcept {
  const auto index = static_cast<std::size_t>(reg);
  if (index >= static_cast<std::size_t>(HleRegister::kCount)) {
    return std::nullopt;
  }
  return index;
}

bool HleCallContext::SetRegister(HleRegister reg,
                                 std::uint64_t value) noexcept {
  const auto index = RegisterIndex(reg);
  if (!index.has_value()) {
    return false;
  }
  registers_[*index] = value;
  if (reg == HleRegister::kRax) {
    return_written_ = true;
  }
  return true;
}

std::optional<std::uint64_t> HleCallContext::GetRegister(
    HleRegister reg) const noexcept {
  const auto index = RegisterIndex(reg);
  return index.has_value()
             ? std::optional<std::uint64_t>(registers_[*index])
             : std::nullopt;
}

std::optional<std::uint64_t> HleCallContext::Argument(
    std::size_t index) const noexcept {
  if (index < kArgumentRegisters.size()) {
    return GetRegister(kArgumentRegisters[index]);
  }

  const auto stack_pointer = GetRegister(HleRegister::kRsp).value_or(0);
  const auto stack_index = index - kArgumentRegisters.size();
  if (captured_stack_argument_count_ != 0) {
    return stack_index < captured_stack_argument_count_
               ? std::optional<std::uint64_t>(
                     captured_stack_arguments_[stack_index])
               : std::nullopt;
  }
  constexpr auto kStackSlotSize = sizeof(std::uint64_t);
  constexpr auto kReturnAddressSize = sizeof(std::uint64_t);
  if (stack_index >
      (std::numeric_limits<std::uint64_t>::max() - kReturnAddressSize) /
          kStackSlotSize) {
    return std::nullopt;
  }
  const auto offset = kReturnAddressSize + stack_index * kStackSlotSize;
  if (stack_pointer == 0 ||
      offset > std::numeric_limits<std::uint64_t>::max() - stack_pointer) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  return ReadUInt64(stack_pointer + offset, value) == HleContextStatus::kOk
             ? std::optional<std::uint64_t>(value)
             : std::nullopt;
}

bool HleCallContext::SetCapturedStackArguments(
    std::span<const std::uint64_t> arguments) noexcept {
  if (arguments.size() > captured_stack_arguments_.size()) {
    return false;
  }
  std::copy(arguments.begin(), arguments.end(),
            captured_stack_arguments_.begin());
  captured_stack_argument_count_ = arguments.size();
  return true;
}

void HleCallContext::SetReturn(std::uint64_t value) noexcept {
  (void)SetRegister(HleRegister::kRax, value);
}

bool HleCallContext::return_written() const noexcept {
  return return_written_;
}

HleContextStatus HleCallContext::ReadUInt32(
    std::uint64_t address, std::uint32_t& value) const noexcept {
  std::array<std::byte, sizeof(value)> bytes{};
  if (!memory_.Read(address, bytes)) {
    value = 0;
    return HleContextStatus::kMemoryFault;
  }
  value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return HleContextStatus::kOk;
}

HleContextStatus HleCallContext::ReadUInt64(
    std::uint64_t address, std::uint64_t& value) const noexcept {
  std::array<std::byte, sizeof(value)> bytes{};
  if (!memory_.Read(address, bytes)) {
    value = 0;
    return HleContextStatus::kMemoryFault;
  }
  value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return HleContextStatus::kOk;
}

HleContextStatus HleCallContext::WriteUInt32(std::uint64_t address,
                                             std::uint32_t value) noexcept {
  std::array<std::byte, sizeof(value)> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  return memory_.Write(address, bytes) ? HleContextStatus::kOk
                                       : HleContextStatus::kMemoryFault;
}

HleContextStatus HleCallContext::WriteUInt64(std::uint64_t address,
                                             std::uint64_t value) noexcept {
  std::array<std::byte, sizeof(value)> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  return memory_.Write(address, bytes) ? HleContextStatus::kOk
                                       : HleContextStatus::kMemoryFault;
}

HleContextStatus HleCallContext::WriteMemory(
    std::uint64_t address, std::span<const std::byte> value) noexcept {
  if (value.empty()) {
    return HleContextStatus::kInvalidArgument;
  }
  return memory_.Write(address, value) ? HleContextStatus::kOk
                                       : HleContextStatus::kMemoryFault;
}

bool HleCallContext::CanWriteMemory(std::uint64_t address,
                                    std::uint64_t length) const noexcept {
  return length == 0 ||
         (address != 0 &&
          memory_.CanAccess(address, length,
                            memory::GuestMemoryProtection::kWrite));
}

std::optional<std::uint64_t> HleCallContext::FindUnmappedMemory(
    std::uint64_t search_start, std::uint64_t length,
    std::uint64_t alignment) const noexcept {
  return memory_.FindUnmappedRange(search_start, length, alignment);
}

bool HleCallContext::MapMemory(
    std::uint64_t address, std::uint64_t length,
    memory::GuestMemoryProtection protection) {
  return memory_.Map(address, length, protection);
}

bool HleCallContext::MapSharedMemory(
    std::uint64_t address, std::uint64_t length,
    memory::GuestMemoryProtection protection,
    std::shared_ptr<memory::SharedMemoryBacking> backing,
    std::uint64_t backing_offset) {
  return memory_.MapShared(address, length, protection, std::move(backing),
                           backing_offset);
}

bool HleCallContext::ProtectMemory(
    std::uint64_t address, std::uint64_t length,
    memory::GuestMemoryProtection protection) {
  return memory_.Protect(address, length, protection);
}

bool HleCallContext::UnmapMemory(std::uint64_t address,
                                 std::uint64_t length) {
  return memory_.Unmap(address, length);
}

std::optional<memory::GuestMemoryRegion> HleCallContext::QueryMemoryRegion(
    std::uint64_t address) const noexcept {
  return memory_.QueryRegion(address);
}

HleStringResult HleCallContext::ReadNullTerminatedString(
    std::uint64_t address, std::size_t maximum_bytes) const {
  if (address == 0 || maximum_bytes == 0 ||
      maximum_bytes > kMaximumHleStringBytes) {
    return {HleContextStatus::kInvalidArgument, {}};
  }

  std::string value;
  value.reserve(std::min(maximum_bytes, kStringReadChunkBytes));
  std::array<std::byte, kStringReadChunkBytes> buffer{};
  std::size_t offset = 0;
  while (offset < maximum_bytes) {
    const auto chunk_size =
        std::min(kStringReadChunkBytes, maximum_bytes - offset);
    std::uint64_t chunk_address = 0;
    if (!AddAddressOffset(address, offset, chunk_address)) {
      return {HleContextStatus::kMemoryFault, {}};
    }
    auto chunk = std::span(buffer).first(chunk_size);
    if (memory_.Read(chunk_address, chunk)) {
      const auto terminator = std::find(chunk.begin(), chunk.end(),
                                        std::byte{0});
      const auto actual = static_cast<std::size_t>(terminator - chunk.begin());
      for (std::size_t index = 0; index < actual; ++index) {
        value.push_back(static_cast<char>(
            std::to_integer<unsigned char>(chunk[index])));
      }
      if (terminator != chunk.end()) {
        return {HleContextStatus::kOk, std::move(value)};
      }
      offset += chunk_size;
      continue;
    }

    for (std::size_t index = 0; index < chunk_size; ++index) {
      std::uint64_t byte_address = 0;
      if (!AddAddressOffset(chunk_address, index, byte_address)) {
        return {HleContextStatus::kMemoryFault, {}};
      }
      std::array<std::byte, 1> byte{};
      if (!memory_.Read(byte_address, byte)) {
        return {HleContextStatus::kMemoryFault, {}};
      }
      if (byte[0] == std::byte{0}) {
        return {HleContextStatus::kOk, std::move(value)};
      }
      value.push_back(
          static_cast<char>(std::to_integer<unsigned char>(byte[0])));
    }
    offset += chunk_size;
  }
  return {HleContextStatus::kUnterminatedString, std::move(value)};
}

std::string_view HleContextStatusName(HleContextStatus status) noexcept {
  switch (status) {
    case HleContextStatus::kOk: return "ok";
    case HleContextStatus::kInvalidArgument: return "invalid-argument";
    case HleContextStatus::kMemoryFault: return "memory-fault";
    case HleContextStatus::kUnterminatedString:
      return "unterminated-string";
    case HleContextStatus::kBlocked: return "blocked";
    case HleContextStatus::kFatalGuestError: return "fatal-guest-error";
    case HleContextStatus::kResourceLimit: return "resource-limit";
    case HleContextStatus::kGuestExit: return "guest-exit";
  }
  return "unknown";
}

}  // namespace kajps5::hle
