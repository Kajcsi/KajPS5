// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/memory/guest_memory.h"

namespace kajps5::cpu {

inline constexpr std::size_t kMaximumNativeLeafCodeSize = 4096;

enum class NativeExecutionStatus {
  kOk,
  kUnsupportedHost,
  kInvalidArgument,
  kGuestCodeNotExecutable,
  kGuestCodeNotReadable,
  kHostAllocationFailed,
  kHostProtectionFailed,
};

struct NativeExecutionResult {
  NativeExecutionStatus status = NativeExecutionStatus::kOk;
  std::uint64_t return_value = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == NativeExecutionStatus::kOk;
  }
};

class NativeLeafExecutor final {
 public:
  [[nodiscard]] NativeExecutionResult Execute(
      const memory::GuestMemory& memory, std::uint64_t entry_point,
      std::size_t code_size) const;
};

[[nodiscard]] std::string_view NativeExecutionStatusName(
    NativeExecutionStatus status) noexcept;

}  // namespace kajps5::cpu
