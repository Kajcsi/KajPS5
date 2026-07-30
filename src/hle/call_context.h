// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/memory/guest_memory.h"

namespace kajps5::hle {

inline constexpr std::size_t kMaximumHleStringBytes = 4096;

enum class HleRegister : std::uint8_t {
  kRax,
  kRcx,
  kRdx,
  kRbx,
  kRsp,
  kRbp,
  kRsi,
  kRdi,
  kR8,
  kR9,
  kR10,
  kR11,
  kR12,
  kR13,
  kR14,
  kR15,
  kCount,
};

enum class HleContextStatus {
  kOk,
  kInvalidArgument,
  kMemoryFault,
  kUnterminatedString,
};

struct HleStringResult {
  HleContextStatus status = HleContextStatus::kOk;
  std::string value;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == HleContextStatus::kOk;
  }
};

class HleCallContext final {
 public:
  explicit HleCallContext(memory::GuestMemory& memory) noexcept;

  [[nodiscard]] bool SetRegister(HleRegister reg,
                                 std::uint64_t value) noexcept;
  [[nodiscard]] std::optional<std::uint64_t> GetRegister(
      HleRegister reg) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> Argument(
      std::size_t index) const noexcept;

  void SetReturn(std::uint64_t value) noexcept;
  [[nodiscard]] bool return_written() const noexcept;

  [[nodiscard]] HleContextStatus ReadUInt32(
      std::uint64_t address, std::uint32_t& value) const noexcept;
  [[nodiscard]] HleContextStatus ReadUInt64(
      std::uint64_t address, std::uint64_t& value) const noexcept;
  [[nodiscard]] HleContextStatus WriteUInt32(std::uint64_t address,
                                             std::uint32_t value) noexcept;
  [[nodiscard]] HleContextStatus WriteUInt64(std::uint64_t address,
                                             std::uint64_t value) noexcept;
  [[nodiscard]] HleContextStatus WriteMemory(
      std::uint64_t address, std::span<const std::byte> value) noexcept;
  [[nodiscard]] bool CanWriteMemory(std::uint64_t address,
                                    std::uint64_t length) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> FindUnmappedMemory(
      std::uint64_t search_start, std::uint64_t length,
      std::uint64_t alignment) const noexcept;
  [[nodiscard]] bool MapMemory(
      std::uint64_t address, std::uint64_t length,
      memory::GuestMemoryProtection protection);
  [[nodiscard]] bool MapSharedMemory(
      std::uint64_t address, std::uint64_t length,
      memory::GuestMemoryProtection protection,
      std::shared_ptr<memory::SharedMemoryBacking> backing,
      std::uint64_t backing_offset);
  [[nodiscard]] bool ProtectMemory(
      std::uint64_t address, std::uint64_t length,
      memory::GuestMemoryProtection protection);
  [[nodiscard]] bool UnmapMemory(std::uint64_t address,
                                 std::uint64_t length);
  [[nodiscard]] std::optional<memory::GuestMemoryRegion> QueryMemoryRegion(
      std::uint64_t address) const noexcept;
  [[nodiscard]] HleStringResult ReadNullTerminatedString(
      std::uint64_t address, std::size_t maximum_bytes) const;

 private:
  [[nodiscard]] static std::optional<std::size_t> RegisterIndex(
      HleRegister reg) noexcept;

  memory::GuestMemory& memory_;
  std::array<std::uint64_t,
             static_cast<std::size_t>(HleRegister::kCount)>
      registers_{};
  bool return_written_ = false;
};

[[nodiscard]] std::string_view HleContextStatusName(
    HleContextStatus status) noexcept;

}  // namespace kajps5::hle
