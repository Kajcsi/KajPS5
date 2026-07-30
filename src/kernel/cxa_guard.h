// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "kernel/guest_scheduler.h"
#include "kernel/status.h"

namespace kajps5::kernel {

struct CxaGuardAcquireResult {
  KernelStatus status = KernelStatus::kOk;
  bool should_initialize = false;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class CxaGuardService final {
 public:
  explicit CxaGuardService(GuestScheduler& scheduler) noexcept;

  [[nodiscard]] CxaGuardAcquireResult Acquire(std::uint64_t address,
                                              bool initialized);
  [[nodiscard]] KernelStatus Release(std::uint64_t address);
  [[nodiscard]] KernelStatus Abort(std::uint64_t address);
  [[nodiscard]] std::size_t owned_count() const;

 private:
  [[nodiscard]] static std::string WaitKey(std::uint64_t address);
  [[nodiscard]] KernelStatus Complete(std::uint64_t address);

  GuestScheduler& scheduler_;
  mutable std::mutex mutex_;
  std::map<std::uint64_t, KernelHandle> owners_;
};

}  // namespace kajps5::kernel
