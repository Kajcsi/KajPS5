// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_process_launcher.h"

#include <array>
#include <limits>
#include <span>
#include <utility>

#include "kernel/pthread.h"

namespace kajps5::cpu {

NativeGuestProcessLauncher::NativeGuestProcessLauncher(
    kernel::PthreadService& pthreads, NativeGuestThreadRunner& runner) noexcept
    : pthreads_(pthreads), runner_(runner) {}

NativeGuestProcessLaunchResult NativeGuestProcessLauncher::Launch(
    const loader::ExecutableLaunchMetadata& metadata,
    std::string_view process_image_name, std::uint64_t stack_search_start,
    std::span<const std::string_view> extra_arguments,
    std::uint64_t exit_handler_address, std::uint64_t stack_size) {
  if (process_image_name.empty() ||
      process_image_name.find('\0') != std::string_view::npos ||
      extra_arguments.size() > 2) {
    return {NativeGuestProcessLaunchStatus::kInvalidArgument};
  }
  if (!metadata.entry_point || *metadata.entry_point == 0) {
    return {NativeGuestProcessLaunchStatus::kEntryPointMissing};
  }

  const auto created =
      pthreads_.CreateThread("main", 0, *metadata.entry_point, 0);
  if (!created) {
    return {NativeGuestProcessLaunchStatus::kThreadCreateFailed,
            created.status};
  }

  std::array<std::string_view, 3> arguments;
  arguments[0] = process_image_name;
  for (std::size_t index = 0; index < extra_arguments.size(); ++index) {
    arguments[index + 1] = extra_arguments[index];
  }
  const auto argument_count = extra_arguments.size() + 1;
  const auto allocation = runner_.AllocateAndRegisterProcessThread(
      created.handle, stack_search_start,
      std::span<const std::string_view>(arguments.data(), argument_count),
      exit_handler_address, stack_size);
  if (!allocation) {
    if (!pthreads_.DiscardReadyThread(created.handle)) {
      return {NativeGuestProcessLaunchStatus::kThreadRollbackFailed,
              kernel::KernelStatus::kOk, allocation.status, created.handle,
              allocation};
    }
    return {NativeGuestProcessLaunchStatus::kThreadRegistrationFailed,
            kernel::KernelStatus::kOk, allocation.status,
            kernel::kInvalidKernelHandle, allocation};
  }

  return {NativeGuestProcessLaunchStatus::kOk, kernel::KernelStatus::kOk,
          NativeGuestThreadRegistrationStatus::kOk, created.handle, allocation};
}

NativeGuestProcessStartupResult NativeGuestProcessLauncher::BeginStartup(
    const loader::ExecutableLaunchMetadata& metadata,
    const loader::ExecutableLifecyclePlan& lifecycle,
    std::string_view process_image_name, std::uint64_t stack_search_start,
    std::span<const std::string_view> extra_arguments,
    std::uint64_t exit_handler_address, std::uint64_t stack_size) {
  if (startup_) {
    return {NativeGuestProcessStartupStatus::kAlreadyActive};
  }
  if (process_image_name.empty() ||
      process_image_name.find('\0') != std::string_view::npos ||
      extra_arguments.size() > 2 || !metadata.entry_point ||
      *metadata.entry_point == 0 ||
      lifecycle.preinitializers.size() >
          std::numeric_limits<std::size_t>::max() -
              lifecycle.initializers.size() ||
      lifecycle.preinitializers.size() + lifecycle.initializers.size() >
          loader::kMaximumLifecycleCalls) {
    return {NativeGuestProcessStartupStatus::kInvalidArgument};
  }
  for (const auto argument : extra_arguments) {
    if (argument.empty() || argument.find('\0') != std::string_view::npos) {
      return {NativeGuestProcessStartupStatus::kInvalidArgument};
    }
  }

  StartupState state;
  state.metadata = metadata;
  state.process_image_name = process_image_name;
  state.extra_argument_count = extra_arguments.size();
  for (std::size_t index = 0; index < extra_arguments.size(); ++index) {
    state.extra_arguments[index] = extra_arguments[index];
  }
  state.stack_search_start = stack_search_start;
  state.exit_handler_address = exit_handler_address;
  state.stack_size = stack_size;
  state.lifecycle_calls.reserve(lifecycle.preinitializers.size() +
                                lifecycle.initializers.size());
  for (std::size_t index = 0; index < lifecycle.preinitializers.size();
       ++index) {
    state.lifecycle_calls.push_back({lifecycle.preinitializers[index],
                                     loader::LifecycleCallKind::kPreinitializer,
                                     index});
  }
  for (std::size_t index = 0; index < lifecycle.initializers.size(); ++index) {
    state.lifecycle_calls.push_back({lifecycle.initializers[index],
                                     loader::LifecycleCallKind::kInitializer,
                                     index});
  }
  startup_ = std::move(state);
  if (startup_->lifecycle_calls.empty()) {
    return FinishStartup();
  }
  return StartNextInitializer();
}

NativeGuestProcessStartupResult
NativeGuestProcessLauncher::StartNextInitializer() {
  if (!startup_ ||
      startup_->next_lifecycle_call >= startup_->lifecycle_calls.size()) {
    return {NativeGuestProcessStartupStatus::kInvalidArgument};
  }
  const auto& call = startup_->lifecycle_calls[startup_->next_lifecycle_call];
  const auto created = pthreads_.CreateThread(
      call.kind == loader::LifecycleCallKind::kPreinitializer ? "preinitializer"
                                                              : "initializer",
      0, call.address, 0);
  if (!created) {
    const auto result = NativeGuestProcessStartupResult{
        NativeGuestProcessStartupStatus::kInitializerThreadCreateFailed,
        call.kind, call.index, call.address, kernel::kInvalidKernelHandle,
        created.status};
    startup_.reset();
    return result;
  }

  constexpr std::array<std::uint64_t, 3> module_arguments{};
  const auto allocation = runner_.AllocateAndRegisterFunctionThread(
      created.handle, startup_->stack_search_start, module_arguments);
  if (!allocation) {
    const auto rollback_ok = pthreads_.DiscardReadyThread(created.handle);
    const auto result = NativeGuestProcessStartupResult{
        rollback_ok
            ? NativeGuestProcessStartupStatus::
                  kInitializerThreadRegistrationFailed
            : NativeGuestProcessStartupStatus::kInitializerThreadRollbackFailed,
        call.kind,
        call.index,
        call.address,
        rollback_ok ? kernel::kInvalidKernelHandle : created.handle,
        kernel::KernelStatus::kOk,
        allocation.status};
    startup_.reset();
    return result;
  }
  if (!pthreads_.SetThreadStack(created.handle, allocation.stack_address,
                                allocation.stack_size)) {
    const auto rollback_ok = pthreads_.DiscardReadyThread(created.handle);
    const auto result = NativeGuestProcessStartupResult{
        rollback_ok
            ? NativeGuestProcessStartupStatus::kInitializerThreadRegistrationFailed
            : NativeGuestProcessStartupStatus::kInitializerThreadRollbackFailed,
        call.kind, call.index, call.address,
        rollback_ok ? kernel::kInvalidKernelHandle : created.handle};
    startup_.reset();
    return result;
  }

  startup_->active_initializer = created.handle;
  return {NativeGuestProcessStartupStatus::kPending, call.kind, call.index,
          call.address, created.handle};
}

NativeGuestProcessStartupResult NativeGuestProcessLauncher::ContinueStartup() {
  if (!startup_ ||
      startup_->active_initializer == kernel::kInvalidKernelHandle) {
    return {NativeGuestProcessStartupStatus::kInvalidArgument};
  }
  const auto call = startup_->lifecycle_calls[startup_->next_lifecycle_call];
  const auto active_initializer = startup_->active_initializer;
  const auto run = runner_.RunNext();
  if (run.status == NativeGuestThreadRunStatus::kExecutionLaneBusy) {
    return {NativeGuestProcessStartupStatus::kPending,
            call.kind,
            call.index,
            call.address,
            active_initializer,
            kernel::KernelStatus::kOk,
            NativeGuestThreadRegistrationStatus::kOk,
            run};
  }
  if (run.status == NativeGuestThreadRunStatus::kIdle) {
    return {NativeGuestProcessStartupStatus::kBlocked,
            call.kind,
            call.index,
            call.address,
            active_initializer,
            kernel::KernelStatus::kOk,
            NativeGuestThreadRegistrationStatus::kOk,
            run};
  }
  if (run.thread != active_initializer) {
    return {NativeGuestProcessStartupStatus::kPending,
            call.kind,
            call.index,
            call.address,
            active_initializer,
            kernel::KernelStatus::kOk,
            NativeGuestThreadRegistrationStatus::kOk,
            run,
            {},
            1};
  }
  if (run.status == NativeGuestThreadRunStatus::kThreadBlocked) {
    return {NativeGuestProcessStartupStatus::kBlocked,
            call.kind,
            call.index,
            call.address,
            active_initializer,
            kernel::KernelStatus::kOk,
            NativeGuestThreadRegistrationStatus::kOk,
            run,
            {},
            1};
  }
  if (run.status == NativeGuestThreadRunStatus::kThreadYielded) {
    return {NativeGuestProcessStartupStatus::kPending,
            call.kind,
            call.index,
            call.address,
            active_initializer,
            kernel::KernelStatus::kOk,
            NativeGuestThreadRegistrationStatus::kOk,
            run,
            {},
            1};
  }
  if (run.status != NativeGuestThreadRunStatus::kThreadExited ||
      run.execution.status != NativeGuestExecutionStatus::kOk) {
    const auto result = NativeGuestProcessStartupResult{
        NativeGuestProcessStartupStatus::kInitializerExecutionFailed,
        call.kind,
        call.index,
        call.address,
        active_initializer,
        kernel::KernelStatus::kOk,
        NativeGuestThreadRegistrationStatus::kOk,
        run,
        {},
        1};
    startup_.reset();
    return result;
  }
  if (run.execution.return_value != 0) {
    const auto result = NativeGuestProcessStartupResult{
        NativeGuestProcessStartupStatus::kInitializerRejected,
        call.kind,
        call.index,
        call.address,
        active_initializer,
        kernel::KernelStatus::kOk,
        NativeGuestThreadRegistrationStatus::kOk,
        run,
        {},
        1};
    startup_.reset();
    return result;
  }

  ++startup_->next_lifecycle_call;
  startup_->active_initializer = kernel::kInvalidKernelHandle;
  if (startup_->next_lifecycle_call < startup_->lifecycle_calls.size()) {
    auto result = StartNextInitializer();
    result.slices = 1;
    return result;
  }
  auto result = FinishStartup();
  result.slices = 1;
  return result;
}

NativeGuestProcessStartupResult
NativeGuestProcessLauncher::RunStartupUntilReady(std::size_t maximum_slices) {
  NativeGuestProcessStartupResult last;
  for (std::size_t slice = 0; slice < maximum_slices; ++slice) {
    last = ContinueStartup();
    if (last.status != NativeGuestProcessStartupStatus::kPending &&
        last.status != NativeGuestProcessStartupStatus::kBlocked) {
      last.slices = slice + last.slices;
      return last;
    }
  }
  last.status = NativeGuestProcessStartupStatus::kSliceLimitReached;
  last.slices = maximum_slices;
  return last;
}

NativeGuestProcessStartupResult NativeGuestProcessLauncher::FinishStartup() {
  if (!startup_) {
    return {NativeGuestProcessStartupStatus::kInvalidArgument};
  }
  std::array<std::string_view, 2> extra_arguments;
  for (std::size_t index = 0; index < startup_->extra_argument_count; ++index) {
    extra_arguments[index] = startup_->extra_arguments[index];
  }
  const auto launch =
      Launch(startup_->metadata, startup_->process_image_name,
             startup_->stack_search_start,
             std::span<const std::string_view>(extra_arguments.data(),
                                               startup_->extra_argument_count),
             startup_->exit_handler_address, startup_->stack_size);
  startup_.reset();
  return {launch ? NativeGuestProcessStartupStatus::kReady
                 : NativeGuestProcessStartupStatus::kMainLaunchFailed,
          loader::LifecycleCallKind::kNone,
          0,
          0,
          launch.thread,
          launch.kernel_status,
          launch.registration_status,
          {},
          launch};
}

bool NativeGuestProcessLauncher::startup_active() const noexcept {
  return startup_.has_value();
}

std::string_view NativeGuestProcessLaunchStatusName(
    NativeGuestProcessLaunchStatus status) noexcept {
  switch (status) {
    case NativeGuestProcessLaunchStatus::kOk:
      return "ok";
    case NativeGuestProcessLaunchStatus::kInvalidArgument:
      return "invalid-argument";
    case NativeGuestProcessLaunchStatus::kEntryPointMissing:
      return "entry-point-missing";
    case NativeGuestProcessLaunchStatus::kThreadCreateFailed:
      return "thread-create-failed";
    case NativeGuestProcessLaunchStatus::kThreadRegistrationFailed:
      return "thread-registration-failed";
    case NativeGuestProcessLaunchStatus::kThreadRollbackFailed:
      return "thread-rollback-failed";
  }
  return "unknown";
}

std::string_view NativeGuestProcessStartupStatusName(
    NativeGuestProcessStartupStatus status) noexcept {
  switch (status) {
    case NativeGuestProcessStartupStatus::kPending:
      return "pending";
    case NativeGuestProcessStartupStatus::kBlocked:
      return "blocked";
    case NativeGuestProcessStartupStatus::kReady:
      return "ready";
    case NativeGuestProcessStartupStatus::kInvalidArgument:
      return "invalid-argument";
    case NativeGuestProcessStartupStatus::kAlreadyActive:
      return "already-active";
    case NativeGuestProcessStartupStatus::kInitializerThreadCreateFailed:
      return "initializer-thread-create-failed";
    case NativeGuestProcessStartupStatus::kInitializerThreadRegistrationFailed:
      return "initializer-thread-registration-failed";
    case NativeGuestProcessStartupStatus::kInitializerThreadRollbackFailed:
      return "initializer-thread-rollback-failed";
    case NativeGuestProcessStartupStatus::kInitializerExecutionFailed:
      return "initializer-execution-failed";
    case NativeGuestProcessStartupStatus::kInitializerRejected:
      return "initializer-rejected";
    case NativeGuestProcessStartupStatus::kMainLaunchFailed:
      return "main-launch-failed";
    case NativeGuestProcessStartupStatus::kSliceLimitReached:
      return "slice-limit-reached";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
