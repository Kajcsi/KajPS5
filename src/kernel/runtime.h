// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <utility>

#include "kernel/clock.h"
#include "kernel/event_queue.h"
#include "kernel/event_flag.h"
#include "kernel/file.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "kernel/semaphore.h"

namespace kajps5::kernel {

class KernelRuntime final {
public:
  KernelRuntime()
      : scheduler_(handles_), event_queues_(handles_, scheduler_),
        event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_), files_(handles_) {}
  explicit KernelRuntime(std::unique_ptr<KernelClockSource> clock_source)
      : scheduler_(handles_), event_queues_(handles_, scheduler_),
        event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_), files_(handles_),
        clock_(std::move(clock_source)) {}

  KernelRuntime(const KernelRuntime &) = delete;
  KernelRuntime &operator=(const KernelRuntime &) = delete;

  [[nodiscard]] HandleTable &handles() noexcept { return handles_; }
  [[nodiscard]] GuestScheduler &scheduler() noexcept { return scheduler_; }
  [[nodiscard]] EventQueueService &event_queues() noexcept {
    return event_queues_;
  }
  [[nodiscard]] EventFlagService &event_flags() noexcept {
    return event_flags_;
  }
  [[nodiscard]] SemaphoreService &semaphores() noexcept { return semaphores_; }
  [[nodiscard]] FileService &files() noexcept { return files_; }
  [[nodiscard]] KernelClockService &clock() noexcept { return clock_; }

private:
  HandleTable handles_;
  GuestScheduler scheduler_;
  EventQueueService event_queues_;
  EventFlagService event_flags_;
  SemaphoreService semaphores_;
  FileService files_;
  KernelClockService clock_;
};

} // namespace kajps5::kernel
