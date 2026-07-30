// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/cxa_guard.h"

#include <string>

namespace kajps5::kernel {

CxaGuardService::CxaGuardService(GuestScheduler& scheduler) noexcept
    : scheduler_(scheduler) {}

CxaGuardAcquireResult CxaGuardService::Acquire(std::uint64_t address,
                                               bool initialized) {
  if (address == 0) {
    return {KernelStatus::kInvalidArgument, false};
  }
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return {KernelStatus::kBusy, false};
  }
  if (initialized) {
    return {KernelStatus::kOk, false};
  }

  {
    std::lock_guard lock(mutex_);
    const auto found = owners_.find(address);
    if (found == owners_.end()) {
      owners_.emplace(address, *current_thread);
      return {KernelStatus::kOk, true};
    }
    if (found->second == *current_thread) {
      return {KernelStatus::kOk, false};
    }
  }

  return scheduler_.BlockCurrent(WaitKey(address))
             ? CxaGuardAcquireResult{KernelStatus::kWouldBlock, false}
             : CxaGuardAcquireResult{KernelStatus::kBusy, false};
}

KernelStatus CxaGuardService::Release(std::uint64_t address) {
  return Complete(address);
}

KernelStatus CxaGuardService::Abort(std::uint64_t address) {
  return Complete(address);
}

std::size_t CxaGuardService::owned_count() const {
  std::lock_guard lock(mutex_);
  return owners_.size();
}

std::string CxaGuardService::WaitKey(std::uint64_t address) {
  return "cxa-guard:" + std::to_string(address);
}

KernelStatus CxaGuardService::Complete(std::uint64_t address) {
  if (address == 0) {
    return KernelStatus::kInvalidArgument;
  }
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return KernelStatus::kBusy;
  }

  {
    std::lock_guard lock(mutex_);
    const auto found = owners_.find(address);
    if (found != owners_.end()) {
      if (found->second != *current_thread) {
        return KernelStatus::kPermissionDenied;
      }
      owners_.erase(found);
    }
  }
  (void)scheduler_.WakeBlockedThreads(WaitKey(address));
  return KernelStatus::kOk;
}

}  // namespace kajps5::kernel
