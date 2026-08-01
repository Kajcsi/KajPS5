// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cpu/native_guest_thread_runner.h"
#include "kernel/status.h"
#include "loader/launch_metadata.h"

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

 private:
  kernel::PthreadService& pthreads_;
  NativeGuestThreadRunner& runner_;
};

[[nodiscard]] std::string_view NativeGuestProcessLaunchStatusName(
    NativeGuestProcessLaunchStatus status) noexcept;

}  // namespace kajps5::cpu
