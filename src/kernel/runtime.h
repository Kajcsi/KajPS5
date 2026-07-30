// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <utility>

#include "kernel/clock.h"
#include "kernel/direct_memory.h"
#include "kernel/event_queue.h"
#include "kernel/event_flag.h"
#include "kernel/file.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "kernel/pthread.h"
#include "kernel/semaphore.h"

namespace kajps5::kernel {

class KernelRuntime final {
public:
  KernelRuntime()
      : scheduler_(handles_, clock_), pthreads_(scheduler_),
        event_queues_(handles_, scheduler_),
        event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_), files_(handles_) {}
  explicit KernelRuntime(std::unique_ptr<KernelClockSource> clock_source)
      : clock_(std::move(clock_source)), scheduler_(handles_, clock_),
        pthreads_(scheduler_),
        event_queues_(handles_, scheduler_),
        event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_), files_(handles_) {}

  KernelRuntime(const KernelRuntime &) = delete;
  KernelRuntime &operator=(const KernelRuntime &) = delete;

  [[nodiscard]] HandleTable &handles() noexcept { return handles_; }
  [[nodiscard]] GuestScheduler &scheduler() noexcept { return scheduler_; }
  [[nodiscard]] PthreadService& pthreads() noexcept { return pthreads_; }
  [[nodiscard]] EventQueueService &event_queues() noexcept {
    return event_queues_;
  }
  [[nodiscard]] EventFlagService &event_flags() noexcept {
    return event_flags_;
  }
  [[nodiscard]] SemaphoreService &semaphores() noexcept { return semaphores_; }
  [[nodiscard]] FileService &files() noexcept { return files_; }
  [[nodiscard]] KernelClockService &clock() noexcept { return clock_; }
  [[nodiscard]] DirectMemoryService &direct_memory() noexcept {
    return direct_memory_;
  }

private:
  HandleTable handles_;
  KernelClockService clock_;
  GuestScheduler scheduler_;
  PthreadService pthreads_;
  EventQueueService event_queues_;
  EventFlagService event_flags_;
  SemaphoreService semaphores_;
  FileService files_;
  DirectMemoryService direct_memory_;
};

} // namespace kajps5::kernel
