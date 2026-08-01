// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/memory/guest_memory.h"

namespace kajps5::cpu {

inline constexpr std::uint64_t kMinimumNativeGuestStackSize = 4096;

class NativeHleTrampoline;

class NativeGuestExecutionContext final {
 public:
  [[nodiscard]] bool active() const noexcept {
    return host_stack_pointer_ != 0;
  }

 private:
  friend class NativeGuestExecutor;
  friend class NativeHleTrampoline;

  volatile std::uint64_t host_stack_pointer_ = 0;
};

struct NativeGuestEntryParameters {
  std::int32_t argc = 0;
  std::uint32_t padding = 0;
  std::array<std::uint64_t, 3> argv{};
};

static_assert(sizeof(NativeGuestEntryParameters) == 32);

enum class NativeGuestExecutionStatus {
  kOk,
  kUnsupportedHost,
  kInvalidArgument,
  kHostMappingRequired,
  kGuestCodeNotExecutable,
  kGuestStackNotAccessible,
  kGuestParametersNotReadable,
  kHostAllocationFailed,
  kHostProtectionFailed,
};

struct NativeGuestExecutionResult {
  NativeGuestExecutionStatus status = NativeGuestExecutionStatus::kOk;
  std::uint64_t return_value = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == NativeGuestExecutionStatus::kOk;
  }
};

class NativeGuestExecutor final {
 public:
  [[nodiscard]] NativeGuestExecutionResult Execute(
      memory::GuestMemory& memory, std::uint64_t entry_point,
      std::uint64_t stack_address, std::uint64_t stack_size,
      std::uint64_t parameters_address, std::uint64_t exit_handler_address,
      NativeGuestExecutionContext* execution_context = nullptr) const;
};

[[nodiscard]] std::string_view NativeGuestExecutionStatusName(
    NativeGuestExecutionStatus status) noexcept;

}  // namespace kajps5::cpu
