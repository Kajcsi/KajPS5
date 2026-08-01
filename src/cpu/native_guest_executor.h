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
#include "hle/call_context.h"

namespace kajps5::cpu {

inline constexpr std::uint64_t kMinimumNativeGuestStackSize = 4096;

class NativeHleTrampoline;

class NativeGuestExecutionContext final {
 public:
  NativeGuestExecutionContext() = default;
  NativeGuestExecutionContext(const NativeGuestExecutionContext&) = delete;
  NativeGuestExecutionContext& operator=(const NativeGuestExecutionContext&) =
      delete;
  NativeGuestExecutionContext(NativeGuestExecutionContext&&) = delete;
  NativeGuestExecutionContext& operator=(NativeGuestExecutionContext&&) =
      delete;

  [[nodiscard]] bool active() const noexcept {
    return host_stack_pointer_ != 0;
  }
  [[nodiscard]] bool suspended() const noexcept {
    return control_request_ == kControlBlocked;
  }

 private:
  friend class NativeGuestExecutor;
  friend class NativeHleTrampoline;

  using ResumeHleDispatch = std::uint64_t (*)(void*, const std::uint64_t*,
                                              std::byte*,
                                              const std::uint64_t*) noexcept;

  static constexpr std::uint64_t kControlNone = 0;
  static constexpr std::uint64_t kControlBlocked = 1;
  static constexpr std::uint64_t kControlStopped = 2;

  volatile std::uint64_t host_stack_pointer_ = 0;
  volatile std::uint64_t recovery_address_ = 0;
  volatile std::uint64_t control_request_ = kControlNone;
  bool retrying_hle_dispatch_ = false;
  hle::HleContextStatus hle_status_ = hle::HleContextStatus::kOk;
  ResumeHleDispatch resume_hle_dispatch_ = nullptr;
  void* resume_hle_state_ = nullptr;
  const std::uint64_t* resume_arguments_ = nullptr;
  std::uint64_t resume_instruction_pointer_ = 0;
  std::uint64_t resume_stack_pointer_ = 0;
  std::uint64_t root_return_slot_ = 0;
  std::array<std::uint64_t, 6> nonvolatile_registers_{};
  alignas(16) std::array<std::byte, 512> floating_state_{};
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
  kFaultBoundaryUnavailable,
  kGuestMemoryFault,
  kGuestInstructionFault,
  kGuestFault,
  kHleBlocked,
  kHleDispatchFailed,
  kGuestExit,
  kHostAllocationFailed,
  kHostProtectionFailed,
};

struct NativeGuestExecutionResult {
  NativeGuestExecutionStatus status = NativeGuestExecutionStatus::kOk;
  std::uint64_t return_value = 0;
  std::uint32_t host_exception_code = 0;
  std::uint64_t fault_instruction_pointer = 0;
  std::uint64_t fault_address = 0;
  hle::HleContextStatus hle_status = hle::HleContextStatus::kOk;

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
  [[nodiscard]] NativeGuestExecutionResult Resume(
      memory::GuestMemory& memory,
      NativeGuestExecutionContext& execution_context) const;

 private:
  [[nodiscard]] static NativeGuestExecutionResult RunGuestEntry(
      memory::GuestMemory& memory, std::uint64_t entry_point,
      std::uint64_t stack_top, std::uint64_t root_frame,
      std::uint64_t parameters_address, std::uint64_t exit_handler_address,
      NativeGuestExecutionContext& execution_context);
  [[nodiscard]] static NativeGuestExecutionResult RunGuestContinuation(
      memory::GuestMemory& memory,
      NativeGuestExecutionContext& execution_context,
      std::uint64_t hle_return_value);
  static void ResetExecutionContext(
      NativeGuestExecutionContext& execution_context) noexcept;
};

[[nodiscard]] std::string_view NativeGuestExecutionStatusName(
    NativeGuestExecutionStatus status) noexcept;

}  // namespace kajps5::cpu
