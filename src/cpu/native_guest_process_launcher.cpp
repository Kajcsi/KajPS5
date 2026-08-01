// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_process_launcher.h"

#include <array>

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

}  // namespace kajps5::cpu
