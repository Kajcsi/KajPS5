// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_thread_runner.h"

#include <utility>

#include "kernel/guest_scheduler.h"
#include "kernel/pthread.h"

namespace kajps5::cpu {
namespace {

bool IsCompletedSlice(NativeGuestThreadRunStatus status) noexcept {
  return status == NativeGuestThreadRunStatus::kThreadExited ||
         status == NativeGuestThreadRunStatus::kThreadBlocked ||
         status == NativeGuestThreadRunStatus::kThreadYielded;
}

}  // namespace

NativeGuestThreadRunner::NativeGuestThreadRunner(
    memory::GuestMemory& memory, kernel::GuestScheduler& scheduler,
    kernel::PthreadService& pthreads,
    NativeGuestExecutionContext& execution_context) noexcept
    : memory_(memory),
      scheduler_(scheduler),
      pthreads_(pthreads),
      execution_context_(execution_context) {}

NativeGuestThreadRegistrationStatus NativeGuestThreadRunner::RegisterThread(
    kernel::KernelHandle handle, std::uint64_t stack_address,
    std::uint64_t stack_size) {
  if (handle == kernel::kInvalidKernelHandle ||
      stack_size < kMinimumNativeGuestStackSize ||
      stack_size > memory_.size() || stack_address > memory_.end_address() ||
      stack_size > memory_.end_address() - stack_address) {
    return NativeGuestThreadRegistrationStatus::kInvalidArgument;
  }
  if (!memory_.host_mapped()) {
    return NativeGuestThreadRegistrationStatus::kHostMappingRequired;
  }
  if (threads_.contains(handle)) {
    return NativeGuestThreadRegistrationStatus::kThreadAlreadyRegistered;
  }

  const auto thread = scheduler_.Snapshot(handle);
  if (!thread) {
    return NativeGuestThreadRegistrationStatus::kThreadNotFound;
  }
  if (thread->state != kernel::GuestThreadState::kReady) {
    return NativeGuestThreadRegistrationStatus::kThreadNotReady;
  }
  if (!memory_.CanExecute(thread->entry_address, 1)) {
    return NativeGuestThreadRegistrationStatus::kGuestCodeNotExecutable;
  }
  constexpr auto stack_access = memory::GuestMemoryProtection::kRead |
                                memory::GuestMemoryProtection::kWrite;
  if (!memory_.CanAccess(stack_address, stack_size, stack_access)) {
    return NativeGuestThreadRegistrationStatus::kGuestStackNotAccessible;
  }
  const auto stack_end = stack_address + stack_size;
  for (const auto& [registered_handle, state] : threads_) {
    (void)registered_handle;
    const auto registered_end = state.stack_address + state.stack_size;
    if (stack_address < registered_end && state.stack_address < stack_end) {
      return NativeGuestThreadRegistrationStatus::kGuestStackAlreadyRegistered;
    }
  }

  ThreadState state;
  state.stack_address = stack_address;
  state.stack_size = stack_size;
  state.continuation = std::make_unique<NativeGuestContinuation>();
  threads_.emplace(handle, std::move(state));
  return NativeGuestThreadRegistrationStatus::kOk;
}

NativeGuestThreadRunResult NativeGuestThreadRunner::RunNext() {
  if (execution_context_.active() || execution_context_.suspended()) {
    return {NativeGuestThreadRunStatus::kExecutionLaneBusy};
  }

  const auto selected = scheduler_.SelectNext();
  if (!selected) {
    return {NativeGuestThreadRunStatus::kIdle};
  }

  const auto thread = scheduler_.Snapshot(*selected);
  if (!thread || thread->state != kernel::GuestThreadState::kRunning) {
    return {NativeGuestThreadRunStatus::kThreadStateInvalid, *selected};
  }
  const auto registered = threads_.find(*selected);
  if (registered == threads_.end()) {
    if (!scheduler_.YieldCurrent()) {
      return {NativeGuestThreadRunStatus::kSchedulerUpdateFailed, *selected};
    }
    return {NativeGuestThreadRunStatus::kThreadNotRegistered, *selected};
  }

  auto& state = registered->second;
  NativeGuestExecutionResult execution;
  if (state.continuation->valid()) {
    execution =
        executor_.Resume(memory_, *state.continuation, execution_context_);
  } else if (!state.started) {
    state.started = true;
    execution = executor_.ExecuteThread(memory_, thread->entry_address,
                                        state.stack_address, state.stack_size,
                                        thread->argument, &execution_context_);
  } else {
    return {NativeGuestThreadRunStatus::kThreadStateInvalid, *selected};
  }

  if (execution.status == NativeGuestExecutionStatus::kHleBlocked ||
      execution.status == NativeGuestExecutionStatus::kHleYielded) {
    if (!executor_.TakeContinuation(execution_context_, *state.continuation)) {
      return {NativeGuestThreadRunStatus::kContinuationCaptureFailed, *selected,
              execution};
    }
    const auto suspended = scheduler_.Snapshot(*selected);
    const auto expected_state =
        execution.status == NativeGuestExecutionStatus::kHleBlocked
            ? kernel::GuestThreadState::kBlocked
            : kernel::GuestThreadState::kReady;
    if (!suspended || suspended->state != expected_state) {
      return {NativeGuestThreadRunStatus::kThreadStateInvalid, *selected,
              execution};
    }
    return {execution.status == NativeGuestExecutionStatus::kHleBlocked
                ? NativeGuestThreadRunStatus::kThreadBlocked
                : NativeGuestThreadRunStatus::kThreadYielded,
            *selected, execution, 1};
  }

  if (execution.status == NativeGuestExecutionStatus::kOk) {
    if (!pthreads_.ExitCurrent(execution.return_value)) {
      return {NativeGuestThreadRunStatus::kSchedulerUpdateFailed, *selected,
              execution};
    }
    threads_.erase(registered);
    return {NativeGuestThreadRunStatus::kThreadExited, *selected, execution, 1};
  }

  if (execution.status == NativeGuestExecutionStatus::kGuestExit) {
    const auto exited = scheduler_.Snapshot(*selected);
    if (!exited || exited->state != kernel::GuestThreadState::kExited) {
      return {NativeGuestThreadRunStatus::kSchedulerUpdateFailed, *selected,
              execution};
    }
    threads_.erase(registered);
    return {NativeGuestThreadRunStatus::kThreadExited, *selected, execution, 1};
  }

  const auto cleaned = pthreads_.ExitCurrent(0);
  threads_.erase(registered);
  return {cleaned ? NativeGuestThreadRunStatus::kGuestExecutionFailed
                  : NativeGuestThreadRunStatus::kSchedulerUpdateFailed,
          *selected, execution};
}

NativeGuestThreadRunResult NativeGuestThreadRunner::RunUntilIdle(
    std::size_t maximum_slices) {
  NativeGuestThreadRunResult last;
  for (std::size_t slice = 0; slice < maximum_slices; ++slice) {
    last = RunNext();
    if (last.status == NativeGuestThreadRunStatus::kIdle) {
      last.slices = slice;
      return last;
    }
    if (!IsCompletedSlice(last.status)) {
      last.slices = slice + last.slices;
      return last;
    }
  }
  last.status = NativeGuestThreadRunStatus::kSliceLimitReached;
  last.slices = maximum_slices;
  return last;
}

std::size_t NativeGuestThreadRunner::registered_thread_count() const noexcept {
  return threads_.size();
}

std::string_view NativeGuestThreadRegistrationStatusName(
    NativeGuestThreadRegistrationStatus status) noexcept {
  switch (status) {
    case NativeGuestThreadRegistrationStatus::kOk:
      return "ok";
    case NativeGuestThreadRegistrationStatus::kInvalidArgument:
      return "invalid-argument";
    case NativeGuestThreadRegistrationStatus::kHostMappingRequired:
      return "host-mapping-required";
    case NativeGuestThreadRegistrationStatus::kThreadNotFound:
      return "thread-not-found";
    case NativeGuestThreadRegistrationStatus::kThreadNotReady:
      return "thread-not-ready";
    case NativeGuestThreadRegistrationStatus::kThreadAlreadyRegistered:
      return "thread-already-registered";
    case NativeGuestThreadRegistrationStatus::kGuestCodeNotExecutable:
      return "guest-code-not-executable";
    case NativeGuestThreadRegistrationStatus::kGuestStackNotAccessible:
      return "guest-stack-not-accessible";
    case NativeGuestThreadRegistrationStatus::kGuestStackAlreadyRegistered:
      return "guest-stack-already-registered";
  }
  return "unknown";
}

std::string_view NativeGuestThreadRunStatusName(
    NativeGuestThreadRunStatus status) noexcept {
  switch (status) {
    case NativeGuestThreadRunStatus::kIdle:
      return "idle";
    case NativeGuestThreadRunStatus::kSliceLimitReached:
      return "slice-limit-reached";
    case NativeGuestThreadRunStatus::kThreadExited:
      return "thread-exited";
    case NativeGuestThreadRunStatus::kThreadBlocked:
      return "thread-blocked";
    case NativeGuestThreadRunStatus::kThreadYielded:
      return "thread-yielded";
    case NativeGuestThreadRunStatus::kExecutionLaneBusy:
      return "execution-lane-busy";
    case NativeGuestThreadRunStatus::kThreadNotRegistered:
      return "thread-not-registered";
    case NativeGuestThreadRunStatus::kThreadStateInvalid:
      return "thread-state-invalid";
    case NativeGuestThreadRunStatus::kContinuationCaptureFailed:
      return "continuation-capture-failed";
    case NativeGuestThreadRunStatus::kSchedulerUpdateFailed:
      return "scheduler-update-failed";
    case NativeGuestThreadRunStatus::kGuestExecutionFailed:
      return "guest-execution-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
