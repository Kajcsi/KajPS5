// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cpu/native_guest_executor.h"
#include "kernel/handle_table.h"

namespace kajps5::kernel {
class GuestScheduler;
class PthreadService;
}  // namespace kajps5::kernel

namespace kajps5::cpu {

inline constexpr std::uint64_t kDefaultNativeGuestProcessStackSize = 0x200000;

enum class NativeGuestThreadRegistrationStatus {
  kOk,
  kInvalidArgument,
  kHostMappingRequired,
  kThreadNotFound,
  kThreadAttributesNotFound,
  kThreadNotReady,
  kThreadAlreadyRegistered,
  kGuestCodeNotExecutable,
  kGuestStackNotAccessible,
  kGuestStackAlreadyRegistered,
  kGuestParametersNotReadable,
  kGuestStackAllocationFailed,
};

struct NativeGuestThreadAllocationResult {
  NativeGuestThreadRegistrationStatus status =
      NativeGuestThreadRegistrationStatus::kOk;
  std::uint64_t stack_address = 0;
  std::uint64_t stack_size = 0;
  std::uint64_t guard_address = 0;
  std::uint64_t guard_size = 0;
  std::uint64_t parameters_address = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == NativeGuestThreadRegistrationStatus::kOk;
  }
};

enum class NativeGuestThreadRunStatus {
  kIdle,
  kSliceLimitReached,
  kThreadExited,
  kThreadBlocked,
  kThreadYielded,
  kExecutionLaneBusy,
  kThreadNotRegistered,
  kThreadStackRegistrationFailed,
  kThreadStateInvalid,
  kContinuationCaptureFailed,
  kSchedulerUpdateFailed,
  kGuestStackReleaseFailed,
  kGuestExecutionFailed,
};

struct NativeGuestThreadRunResult {
  NativeGuestThreadRunStatus status = NativeGuestThreadRunStatus::kIdle;
  kernel::KernelHandle thread = kernel::kInvalidKernelHandle;
  NativeGuestExecutionResult execution;
  std::size_t slices = 0;
  NativeGuestThreadRegistrationStatus registration_status =
      NativeGuestThreadRegistrationStatus::kOk;
};

class NativeGuestThreadRunner final {
 public:
  NativeGuestThreadRunner(
      memory::GuestMemory& memory, kernel::GuestScheduler& scheduler,
      kernel::PthreadService& pthreads,
      NativeGuestExecutionContext& execution_context) noexcept;
  ~NativeGuestThreadRunner();

  NativeGuestThreadRunner(const NativeGuestThreadRunner&) = delete;
  NativeGuestThreadRunner& operator=(const NativeGuestThreadRunner&) = delete;

  [[nodiscard]] NativeGuestThreadRegistrationStatus RegisterThread(
      kernel::KernelHandle handle, std::uint64_t stack_address,
      std::uint64_t stack_size);
  [[nodiscard]] NativeGuestThreadRegistrationStatus RegisterProcessThread(
      kernel::KernelHandle handle, std::uint64_t stack_address,
      std::uint64_t stack_size, std::uint64_t parameters_address,
      std::uint64_t exit_handler_address);
  [[nodiscard]] NativeGuestThreadAllocationResult AllocateAndRegisterThread(
      kernel::KernelHandle handle, std::uint64_t search_start);
  [[nodiscard]] NativeGuestThreadAllocationResult
  AllocateAndRegisterProcessThread(
      kernel::KernelHandle handle, std::uint64_t search_start,
      std::span<const std::string_view> arguments,
      std::uint64_t exit_handler_address,
      std::uint64_t stack_size = kDefaultNativeGuestProcessStackSize);
  [[nodiscard]] NativeGuestThreadRunResult RunNext();
  [[nodiscard]] NativeGuestThreadRunResult RunUntilIdle(
      std::size_t maximum_slices);
  [[nodiscard]] std::size_t registered_thread_count() const noexcept;

 private:
  enum class EntryKind {
    kPthread,
    kProcess,
  };

  struct ThreadState {
    std::uint64_t stack_address = 0;
    std::uint64_t stack_size = 0;
    std::uint64_t parameters_address = 0;
    std::uint64_t exit_handler_address = 0;
    EntryKind entry_kind = EntryKind::kPthread;
    bool started = false;
    bool owns_stack = false;
    std::uint64_t allocation_address = 0;
    std::uint64_t allocation_size = 0;
    std::unique_ptr<NativeGuestContinuation> continuation;
  };

  [[nodiscard]] bool ReleaseThread(
      std::map<kernel::KernelHandle, ThreadState>::iterator thread) noexcept;
  [[nodiscard]] NativeGuestThreadRegistrationStatus RegisterThreadEntry(
      kernel::KernelHandle handle, std::uint64_t stack_address,
      std::uint64_t stack_size, EntryKind entry_kind,
      std::uint64_t parameters_address, std::uint64_t exit_handler_address);
  [[nodiscard]] std::optional<NativeGuestThreadRunResult>
  PrepareReadyPthreads();

  memory::GuestMemory& memory_;
  kernel::GuestScheduler& scheduler_;
  kernel::PthreadService& pthreads_;
  NativeGuestExecutionContext& execution_context_;
  NativeGuestExecutor executor_;
  std::map<kernel::KernelHandle, ThreadState> threads_;
};

[[nodiscard]] std::string_view NativeGuestThreadRegistrationStatusName(
    NativeGuestThreadRegistrationStatus status) noexcept;
[[nodiscard]] std::string_view NativeGuestThreadRunStatusName(
    NativeGuestThreadRunStatus status) noexcept;

}  // namespace kajps5::cpu
