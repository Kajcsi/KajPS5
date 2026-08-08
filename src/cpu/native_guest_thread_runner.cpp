// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_thread_runner.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
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

std::optional<std::uint64_t> AlignUp(std::uint64_t value,
                                     std::uint64_t alignment) noexcept {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    return std::nullopt;
  }
  const auto mask = alignment - 1;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
    return std::nullopt;
  }
  return (value + mask) & ~mask;
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

NativeGuestThreadRunner::~NativeGuestThreadRunner() {
  for (const auto& [handle, state] : threads_) {
    (void)handle;
    if (state.owns_stack) {
      (void)memory_.Unmap(state.allocation_address, state.allocation_size);
    }
    if (state.owns_tls) {
      (void)memory_.Unmap(state.tls_allocation_address,
                          state.tls_allocation_size);
    }
  }
}

NativeGuestThreadRegistrationStatus NativeGuestThreadRunner::RegisterThread(
    kernel::KernelHandle handle, std::uint64_t stack_address,
    std::uint64_t stack_size) {
  return RegisterThreadEntry(handle, stack_address, stack_size,
                             EntryKind::kPthread, 0, 0);
}

NativeGuestThreadRegistrationStatus
NativeGuestThreadRunner::RegisterProcessThread(
    kernel::KernelHandle handle, std::uint64_t stack_address,
    std::uint64_t stack_size, std::uint64_t parameters_address,
    std::uint64_t exit_handler_address) {
  return RegisterThreadEntry(handle, stack_address, stack_size,
                             EntryKind::kProcess, parameters_address,
                             exit_handler_address);
}

NativeGuestThreadRegistrationStatus
NativeGuestThreadRunner::RegisterFunctionThread(
    kernel::KernelHandle handle, std::uint64_t stack_address,
    std::uint64_t stack_size, std::span<const std::uint64_t> arguments) {
  if (arguments.size() > kMaximumFunctionArguments) {
    return NativeGuestThreadRegistrationStatus::kInvalidArgument;
  }
  const auto status = RegisterThreadEntry(handle, stack_address, stack_size,
                                          EntryKind::kFunction, 0, 0);
  if (status != NativeGuestThreadRegistrationStatus::kOk) {
    return status;
  }
  auto& state = threads_.at(handle);
  std::copy(arguments.begin(), arguments.end(), state.arguments.begin());
  state.argument_count = arguments.size();
  return status;
}

NativeGuestThreadRegistrationStatus
NativeGuestThreadRunner::RegisterThreadEntry(
    kernel::KernelHandle handle, std::uint64_t stack_address,
    std::uint64_t stack_size, EntryKind entry_kind,
    std::uint64_t parameters_address, std::uint64_t exit_handler_address) {
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
  if (entry_kind == EntryKind::kProcess &&
      !memory_.CanAccess(parameters_address, sizeof(NativeGuestEntryParameters),
                         memory::GuestMemoryProtection::kRead)) {
    return NativeGuestThreadRegistrationStatus::kGuestParametersNotReadable;
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
  state.parameters_address = parameters_address;
  state.exit_handler_address = exit_handler_address;
  state.entry_kind = entry_kind;
  if (tls_templates_installed_) {
    const auto tls = loader::CreateStaticTlsInstance(
        memory_, tls_layout_, tls_modules_, stack_address + stack_size);
    if (!tls) {
      return NativeGuestThreadRegistrationStatus::kGuestTlsAllocationFailed;
    }
    state.thread_pointer = tls.instance.thread_pointer;
    state.owns_tls = true;
    state.tls_allocation_address = tls.instance.allocation_address;
    state.tls_allocation_size = tls.instance.allocation_size;
  }
  state.continuation = std::make_unique<NativeGuestContinuation>();
  threads_.emplace(handle, std::move(state));
  return NativeGuestThreadRegistrationStatus::kOk;
}

NativeGuestThreadAllocationResult
NativeGuestThreadRunner::AllocateAndRegisterThread(kernel::KernelHandle handle,
                                                   std::uint64_t search_start) {
  if (!memory_.host_mapped()) {
    return {NativeGuestThreadRegistrationStatus::kHostMappingRequired};
  }
  const auto thread = pthreads_.GetThread(handle);
  if (!thread) {
    return {NativeGuestThreadRegistrationStatus::kThreadAttributesNotFound};
  }
  if (thread->attributes.stack_address != 0) {
    const auto status = RegisterThread(handle, thread->attributes.stack_address,
                                       thread->attributes.stack_size);
    return {status, thread->attributes.stack_address,
            thread->attributes.stack_size};
  }

  const auto granularity = memory_.mapping_granularity();
  const auto stack_size = AlignUp(thread->attributes.stack_size, granularity);
  const auto guard_size = AlignUp(thread->attributes.guard_size, granularity);
  if (!stack_size || !guard_size ||
      *stack_size > std::numeric_limits<std::uint64_t>::max() - *guard_size) {
    return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
  }
  const auto allocation_size = *guard_size + *stack_size;
  const auto allocation =
      memory_.FindUnmappedRange(search_start, allocation_size, granularity);
  if (!allocation) {
    return {NativeGuestThreadRegistrationStatus::kGuestStackAllocationFailed};
  }

  constexpr auto read_write = memory::GuestMemoryProtection::kRead |
                              memory::GuestMemoryProtection::kWrite;
  if (!memory_.Map(*allocation, allocation_size, read_write) ||
      (*guard_size != 0 &&
       !memory_.Protect(*allocation, *guard_size,
                        memory::GuestMemoryProtection::kNone)) ||
      !memory_.Fill(*allocation + *guard_size, *stack_size, std::byte{0})) {
    if (memory_.IsMapped(*allocation, allocation_size)) {
      (void)memory_.Unmap(*allocation, allocation_size);
    }
    return {NativeGuestThreadRegistrationStatus::kGuestStackAllocationFailed};
  }

  const auto stack_address = *allocation + *guard_size;
  const auto status = RegisterThread(handle, stack_address, *stack_size);
  if (status != NativeGuestThreadRegistrationStatus::kOk) {
    (void)memory_.Unmap(*allocation, allocation_size);
    return {status};
  }
  auto& state = threads_.at(handle);
  state.owns_stack = true;
  state.allocation_address = *allocation;
  state.allocation_size = allocation_size;
  return {status, stack_address, *stack_size, *allocation, *guard_size};
}

NativeGuestThreadAllocationResult
NativeGuestThreadRunner::AllocateAndRegisterProcessThread(
    kernel::KernelHandle handle, std::uint64_t search_start,
    std::span<const std::string_view> arguments,
    std::uint64_t exit_handler_address, std::uint64_t requested_stack_size) {
  if (!memory_.host_mapped()) {
    return {NativeGuestThreadRegistrationStatus::kHostMappingRequired};
  }
  if (arguments.empty() ||
      arguments.size() > NativeGuestEntryParameters{}.argv.size()) {
    return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
  }

  const auto granularity = memory_.mapping_granularity();
  const auto stack_size = AlignUp(requested_stack_size, granularity);
  if (!stack_size || *stack_size < kMinimumNativeGuestStackSize ||
      granularity > std::numeric_limits<std::uint64_t>::max() / 2 ||
      *stack_size >
          std::numeric_limits<std::uint64_t>::max() - 2 * granularity) {
    return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
  }

  std::uint64_t required_parameter_bytes = sizeof(NativeGuestEntryParameters);
  for (const auto argument : arguments) {
    if (argument.empty() || argument.find('\0') != std::string_view::npos ||
        argument.size() > std::numeric_limits<std::uint64_t>::max() - 16 ||
        required_parameter_bytes >
            std::numeric_limits<std::uint64_t>::max() - argument.size() - 16) {
      return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
    }
    required_parameter_bytes += argument.size() + 16;
  }
  if (required_parameter_bytes > granularity) {
    return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
  }

  const auto allocation_size = *stack_size + 2 * granularity;
  const auto allocation =
      memory_.FindUnmappedRange(search_start, allocation_size, granularity);
  if (!allocation) {
    return {NativeGuestThreadRegistrationStatus::kGuestStackAllocationFailed};
  }

  constexpr auto read_write = memory::GuestMemoryProtection::kRead |
                              memory::GuestMemoryProtection::kWrite;
  if (!memory_.Map(*allocation, allocation_size, read_write) ||
      !memory_.Protect(*allocation, granularity,
                       memory::GuestMemoryProtection::kNone) ||
      !memory_.Fill(*allocation + granularity, allocation_size - granularity,
                    std::byte{0})) {
    if (memory_.IsMapped(*allocation, allocation_size)) {
      (void)memory_.Unmap(*allocation, allocation_size);
    }
    return {NativeGuestThreadRegistrationStatus::kGuestStackAllocationFailed};
  }

  const auto stack_address = *allocation + granularity;
  const auto parameter_page = stack_address + *stack_size;
  auto cursor = parameter_page + granularity;
  NativeGuestEntryParameters parameters;
  parameters.argc = static_cast<std::int32_t>(arguments.size());
  for (std::size_t index = arguments.size(); index-- != 0;) {
    const auto argument = arguments[index];
    cursor = (cursor - argument.size() - 1) & ~std::uint64_t{0xf};
    if (cursor < parameter_page ||
        !memory_.Write(cursor, std::as_bytes(std::span<const char>(
                                   argument.data(), argument.size())))) {
      (void)memory_.Unmap(*allocation, allocation_size);
      return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
    }
    parameters.argv[index] = cursor;
  }
  const auto parameters_address =
      (cursor - sizeof(parameters)) & ~std::uint64_t{0xf};
  if (parameters_address < parameter_page ||
      !memory_.Write(parameters_address,
                     std::as_bytes(std::span{&parameters, 1}))) {
    (void)memory_.Unmap(*allocation, allocation_size);
    return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
  }

  const auto status =
      RegisterProcessThread(handle, stack_address, *stack_size,
                            parameters_address, exit_handler_address);
  if (status != NativeGuestThreadRegistrationStatus::kOk) {
    (void)memory_.Unmap(*allocation, allocation_size);
    return {status};
  }
  auto& state = threads_.at(handle);
  state.owns_stack = true;
  state.allocation_address = *allocation;
  state.allocation_size = allocation_size;
  return {status,      stack_address, *stack_size,
          *allocation, granularity,   parameters_address};
}

NativeGuestThreadAllocationResult
NativeGuestThreadRunner::AllocateAndRegisterFunctionThread(
    kernel::KernelHandle handle, std::uint64_t search_start,
    std::span<const std::uint64_t> arguments) {
  if (arguments.size() > kMaximumFunctionArguments) {
    return {NativeGuestThreadRegistrationStatus::kInvalidArgument};
  }
  auto allocation = AllocateAndRegisterThread(handle, search_start);
  if (!allocation) {
    return allocation;
  }
  auto& state = threads_.at(handle);
  state.entry_kind = EntryKind::kFunction;
  std::copy(arguments.begin(), arguments.end(), state.arguments.begin());
  state.argument_count = arguments.size();
  return allocation;
}

NativeGuestThreadRunResult NativeGuestThreadRunner::RunNext() {
  if (execution_context_.active() || execution_context_.suspended()) {
    return {NativeGuestThreadRunStatus::kExecutionLaneBusy};
  }
  if (const auto preparation_failure = PrepareReadyPthreads()) {
    return *preparation_failure;
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
  last_guest_instruction_pointer_ = thread->entry_address;
  NativeGuestExecutionResult execution;
  if (state.continuation->valid()) {
    execution = executor_.Resume(memory_, *state.continuation,
                                 execution_context_, state.thread_pointer);
  } else if (!state.started) {
    state.started = true;
    if (state.entry_kind == EntryKind::kProcess) {
      execution =
          executor_.Execute(memory_, thread->entry_address, state.stack_address,
                            state.stack_size, state.parameters_address,
                            state.exit_handler_address, &execution_context_,
                            state.thread_pointer);
    } else if (state.entry_kind == EntryKind::kFunction) {
      execution = executor_.ExecuteFunction(
          memory_, thread->entry_address, state.stack_address, state.stack_size,
          std::span<const std::uint64_t>(state.arguments.data(),
                                         state.argument_count),
          &execution_context_, state.thread_pointer);
    } else {
      execution = executor_.ExecuteThread(
          memory_, thread->entry_address, state.stack_address, state.stack_size,
          thread->argument, &execution_context_, state.thread_pointer);
    }
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
    const auto released = ReleaseThread(registered);
    return {released ? NativeGuestThreadRunStatus::kThreadExited
                     : NativeGuestThreadRunStatus::kGuestStackReleaseFailed,
            *selected, execution, 1};
  }

  if (execution.status == NativeGuestExecutionStatus::kGuestExit) {
    const auto exited = scheduler_.Snapshot(*selected);
    if (!exited || exited->state != kernel::GuestThreadState::kExited) {
      return {NativeGuestThreadRunStatus::kSchedulerUpdateFailed, *selected,
              execution};
    }
    const auto released = ReleaseThread(registered);
    return {released ? NativeGuestThreadRunStatus::kThreadExited
                     : NativeGuestThreadRunStatus::kGuestStackReleaseFailed,
            *selected, execution, 1};
  }

  const auto cleaned = pthreads_.ExitCurrent(0);
  const auto released = ReleaseThread(registered);
  return {!cleaned    ? NativeGuestThreadRunStatus::kSchedulerUpdateFailed
          : !released ? NativeGuestThreadRunStatus::kGuestStackReleaseFailed
                      : NativeGuestThreadRunStatus::kGuestExecutionFailed,
          *selected, execution};
}

bool NativeGuestThreadRunner::ReleaseThread(
    std::map<kernel::KernelHandle, ThreadState>::iterator thread) noexcept {
  const auto stack_released = !thread->second.owns_stack ||
                              memory_.Unmap(thread->second.allocation_address,
                                            thread->second.allocation_size);
  const auto tls_released =
      !thread->second.owns_tls ||
      memory_.Unmap(thread->second.tls_allocation_address,
                    thread->second.tls_allocation_size);
  threads_.erase(thread);
  return stack_released && tls_released;
}

std::optional<NativeGuestThreadRunResult>
NativeGuestThreadRunner::PrepareReadyPthreads() {
  for (const auto& thread : scheduler_.SnapshotAll()) {
    if (thread.state != kernel::GuestThreadState::kReady ||
        threads_.contains(thread.handle) ||
        !pthreads_.GetThread(thread.handle)) {
      continue;
    }

    const auto allocation =
        AllocateAndRegisterThread(thread.handle, memory_.base_address());
    if (!allocation) {
      return NativeGuestThreadRunResult{
          NativeGuestThreadRunStatus::kThreadStackRegistrationFailed,
          thread.handle,
          {},
          0,
          allocation.status,
      };
    }
  }
  return std::nullopt;
}

bool NativeGuestThreadRunner::InstallStaticTlsTemplates(
    const loader::StaticTlsLayout& layout,
    std::vector<loader::StaticTlsTemplateModule> modules) {
  if (tls_templates_installed_ || !threads_.empty() ||
      layout.module_count() == 0 || layout.module_count() != modules.size()) {
    return false;
  }
  tls_layout_ = layout;
  tls_modules_ = std::move(modules);
  tls_templates_installed_ = true;
  return true;
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

std::uint64_t NativeGuestThreadRunner::last_guest_instruction_pointer()
    const noexcept {
  return last_guest_instruction_pointer_;
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
    case NativeGuestThreadRegistrationStatus::kThreadAttributesNotFound:
      return "thread-attributes-not-found";
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
    case NativeGuestThreadRegistrationStatus::kGuestParametersNotReadable:
      return "guest-parameters-not-readable";
    case NativeGuestThreadRegistrationStatus::kGuestStackAllocationFailed:
      return "guest-stack-allocation-failed";
    case NativeGuestThreadRegistrationStatus::kGuestTlsAllocationFailed:
      return "guest-tls-allocation-failed";
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
    case NativeGuestThreadRunStatus::kThreadStackRegistrationFailed:
      return "thread-stack-registration-failed";
    case NativeGuestThreadRunStatus::kThreadStateInvalid:
      return "thread-state-invalid";
    case NativeGuestThreadRunStatus::kContinuationCaptureFailed:
      return "continuation-capture-failed";
    case NativeGuestThreadRunStatus::kSchedulerUpdateFailed:
      return "scheduler-update-failed";
    case NativeGuestThreadRunStatus::kGuestStackReleaseFailed:
      return "guest-stack-release-failed";
    case NativeGuestThreadRunStatus::kGuestExecutionFailed:
      return "guest-execution-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
