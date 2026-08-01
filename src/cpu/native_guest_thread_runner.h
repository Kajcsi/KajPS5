// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>

#include "cpu/native_guest_executor.h"
#include "kernel/handle_table.h"

namespace kajps5::kernel {
class GuestScheduler;
class PthreadService;
}  // namespace kajps5::kernel

namespace kajps5::cpu {

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
  kGuestStackAllocationFailed,
};

struct NativeGuestThreadAllocationResult {
  NativeGuestThreadRegistrationStatus status =
      NativeGuestThreadRegistrationStatus::kOk;
  std::uint64_t stack_address = 0;
  std::uint64_t stack_size = 0;
  std::uint64_t guard_address = 0;
  std::uint64_t guard_size = 0;

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
  [[nodiscard]] NativeGuestThreadAllocationResult AllocateAndRegisterThread(
      kernel::KernelHandle handle, std::uint64_t search_start);
  [[nodiscard]] NativeGuestThreadRunResult RunNext();
  [[nodiscard]] NativeGuestThreadRunResult RunUntilIdle(
      std::size_t maximum_slices);
  [[nodiscard]] std::size_t registered_thread_count() const noexcept;

 private:
  struct ThreadState {
    std::uint64_t stack_address = 0;
    std::uint64_t stack_size = 0;
    bool started = false;
    bool owns_stack = false;
    std::uint64_t allocation_address = 0;
    std::uint64_t allocation_size = 0;
    std::unique_ptr<NativeGuestContinuation> continuation;
  };

  [[nodiscard]] bool ReleaseThread(
      std::map<kernel::KernelHandle, ThreadState>::iterator thread) noexcept;

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
