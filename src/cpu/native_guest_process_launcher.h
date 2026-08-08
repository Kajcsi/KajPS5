// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/native_guest_thread_runner.h"
#include "kernel/status.h"
#include "loader/launch_metadata.h"
#include "loader/lifecycle_plan.h"

namespace kajps5::kernel {
class PthreadService;
}  // namespace kajps5::kernel

namespace kajps5::cpu {

enum class NativeGuestProcessLaunchStatus {
  kOk,
  kInvalidArgument,
  kEntryPointMissing,
  kThreadCreateFailed,
  kThreadRegistrationFailed,
  kThreadRollbackFailed,
};

struct NativeGuestProcessLaunchResult {
  NativeGuestProcessLaunchStatus status = NativeGuestProcessLaunchStatus::kOk;
  kernel::KernelStatus kernel_status = kernel::KernelStatus::kOk;
  NativeGuestThreadRegistrationStatus registration_status =
      NativeGuestThreadRegistrationStatus::kOk;
  kernel::KernelHandle thread = kernel::kInvalidKernelHandle;
  NativeGuestThreadAllocationResult allocation;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == NativeGuestProcessLaunchStatus::kOk;
  }
};

enum class NativeGuestProcessStartupStatus {
  kPending,
  kBlocked,
  kReady,
  kInvalidArgument,
  kAlreadyActive,
  kInitializerThreadCreateFailed,
  kInitializerThreadRegistrationFailed,
  kInitializerThreadRollbackFailed,
  kInitializerExecutionFailed,
  kInitializerRejected,
  kMainLaunchFailed,
  kSliceLimitReached,
};

struct NativeGuestProcessStartupResult {
  NativeGuestProcessStartupStatus status =
      NativeGuestProcessStartupStatus::kPending;
  loader::LifecycleCallKind lifecycle_kind = loader::LifecycleCallKind::kNone;
  std::size_t lifecycle_index = 0;
  std::uint64_t lifecycle_address = 0;
  kernel::KernelHandle thread = kernel::kInvalidKernelHandle;
  kernel::KernelStatus kernel_status = kernel::KernelStatus::kOk;
  NativeGuestThreadRegistrationStatus registration_status =
      NativeGuestThreadRegistrationStatus::kOk;
  NativeGuestThreadRunResult run;
  NativeGuestProcessLaunchResult launch;
  std::size_t slices = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == NativeGuestProcessStartupStatus::kReady;
  }
};

class NativeGuestProcessLauncher final {
 public:
  NativeGuestProcessLauncher(kernel::PthreadService& pthreads,
                             NativeGuestThreadRunner& runner) noexcept;

  NativeGuestProcessLauncher(const NativeGuestProcessLauncher&) = delete;
  NativeGuestProcessLauncher& operator=(const NativeGuestProcessLauncher&) =
      delete;

  [[nodiscard]] NativeGuestProcessLaunchResult Launch(
      const loader::ExecutableLaunchMetadata& metadata,
      std::string_view process_image_name, std::uint64_t stack_search_start,
      std::span<const std::string_view> extra_arguments = {},
      std::uint64_t exit_handler_address = 0,
      std::uint64_t stack_size = kDefaultNativeGuestProcessStackSize);
  [[nodiscard]] NativeGuestProcessStartupResult BeginStartup(
      const loader::ExecutableLaunchMetadata& metadata,
      const loader::ExecutableLifecyclePlan& lifecycle,
      std::string_view process_image_name, std::uint64_t stack_search_start,
      std::span<const std::string_view> extra_arguments = {},
      std::uint64_t exit_handler_address = 0,
      std::uint64_t stack_size = kDefaultNativeGuestProcessStackSize);
  [[nodiscard]] NativeGuestProcessStartupResult ContinueStartup();
  [[nodiscard]] NativeGuestProcessStartupResult RunStartupUntilReady(
      std::size_t maximum_slices);
  [[nodiscard]] bool startup_active() const noexcept;

 private:
  struct StartupLifecycleCall {
    std::uint64_t address = 0;
    loader::LifecycleCallKind kind = loader::LifecycleCallKind::kNone;
    std::size_t index = 0;
  };

  struct StartupState {
    loader::ExecutableLaunchMetadata metadata;
    std::string process_image_name;
    std::array<std::string, 2> extra_arguments;
    std::size_t extra_argument_count = 0;
    std::uint64_t stack_search_start = 0;
    std::uint64_t exit_handler_address = 0;
    std::uint64_t stack_size = kDefaultNativeGuestProcessStackSize;
    std::vector<StartupLifecycleCall> lifecycle_calls;
    std::size_t next_lifecycle_call = 0;
    kernel::KernelHandle active_initializer = kernel::kInvalidKernelHandle;
  };

  [[nodiscard]] NativeGuestProcessStartupResult StartNextInitializer();
  [[nodiscard]] NativeGuestProcessStartupResult FinishStartup();

  kernel::PthreadService& pthreads_;
  NativeGuestThreadRunner& runner_;
  std::optional<StartupState> startup_;
};

[[nodiscard]] std::string_view NativeGuestProcessLaunchStatusName(
    NativeGuestProcessLaunchStatus status) noexcept;
[[nodiscard]] std::string_view NativeGuestProcessStartupStatusName(
    NativeGuestProcessStartupStatus status) noexcept;

}  // namespace kajps5::cpu
