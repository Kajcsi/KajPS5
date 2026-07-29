// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "kernel/event_flag.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "kernel/semaphore.h"

namespace kajps5::kernel {

class KernelRuntime final {
public:
  KernelRuntime()
      : scheduler_(handles_), event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_) {}

  KernelRuntime(const KernelRuntime &) = delete;
  KernelRuntime &operator=(const KernelRuntime &) = delete;

  [[nodiscard]] HandleTable &handles() noexcept { return handles_; }
  [[nodiscard]] GuestScheduler &scheduler() noexcept { return scheduler_; }
  [[nodiscard]] EventFlagService &event_flags() noexcept {
    return event_flags_;
  }
  [[nodiscard]] SemaphoreService &semaphores() noexcept { return semaphores_; }

private:
  HandleTable handles_;
  GuestScheduler scheduler_;
  EventFlagService event_flags_;
  SemaphoreService semaphores_;
};

} // namespace kajps5::kernel
