// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "kernel/ampr_command_buffer.h"
#include "kernel/clock.h"
#include "kernel/cxa_guard.h"
#include "kernel/direct_memory.h"
#include "kernel/event_queue.h"
#include "kernel/event_flag.h"
#include "kernel/file.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "kernel/json_value.h"
#include "kernel/libc_heap.h"
#include "kernel/process_lifecycle.h"
#include "kernel/pthread.h"
#include "kernel/semaphore.h"

namespace kajps5::kernel {

class KernelRuntime final {
public:
  KernelRuntime()
      : scheduler_(handles_, clock_), cxa_guards_(scheduler_),
        pthreads_(scheduler_, clock_),
        event_queues_(handles_, scheduler_, clock_),
        event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_), files_(handles_) {}
  explicit KernelRuntime(std::unique_ptr<KernelClockSource> clock_source)
      : clock_(std::move(clock_source)), scheduler_(handles_, clock_),
        cxa_guards_(scheduler_), pthreads_(scheduler_, clock_),
        event_queues_(handles_, scheduler_, clock_),
        event_flags_(handles_, scheduler_),
        semaphores_(handles_, scheduler_), files_(handles_) {}

  KernelRuntime(const KernelRuntime &) = delete;
  KernelRuntime &operator=(const KernelRuntime &) = delete;

  [[nodiscard]] HandleTable &handles() noexcept { return handles_; }
  [[nodiscard]] GuestScheduler &scheduler() noexcept { return scheduler_; }
  [[nodiscard]] CxaGuardService& cxa_guards() noexcept {
    return cxa_guards_;
  }
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
  [[nodiscard]] JsonValueService& json_values() noexcept {
    return json_values_;
  }
  [[nodiscard]] LibcHeapService& libc_heap() noexcept { return libc_heap_; }
  [[nodiscard]] ProcessLifecycleService& process_lifecycle() noexcept {
    return process_lifecycle_;
  }
  [[nodiscard]] AmprCommandBufferService& ampr_command_buffers() noexcept {
    return ampr_command_buffers_;
  }
  void SetProcessParametersAddress(std::uint64_t address) noexcept {
    process_parameters_address_ = address;
  }
  [[nodiscard]] std::uint64_t process_parameters_address() const noexcept {
    return process_parameters_address_;
  }
  void SetSanitizerMallocReplaceAddress(std::uint64_t address) noexcept {
    sanitizer_malloc_replace_address_ = address;
  }
  [[nodiscard]] std::uint64_t sanitizer_malloc_replace_address() const
      noexcept {
    return sanitizer_malloc_replace_address_;
  }
  void SetSanitizerNewReplaceAddress(std::uint64_t address) noexcept {
    sanitizer_new_replace_address_ = address;
  }
  [[nodiscard]] std::uint64_t sanitizer_new_replace_address() const noexcept {
    return sanitizer_new_replace_address_;
  }
  void SetApplicationHeapApiAddress(std::uint64_t address) noexcept {
    application_heap_api_address_ = address;
  }
  [[nodiscard]] std::uint64_t application_heap_api_address() const noexcept {
    return application_heap_api_address_;
  }
  void SetThreadAtexitCountCallback(std::uint64_t callback) noexcept {
    thread_atexit_count_callback_ = callback;
  }
  [[nodiscard]] std::uint64_t thread_atexit_count_callback() const noexcept {
    return thread_atexit_count_callback_;
  }
  void SetThreadAtexitReportCallback(std::uint64_t callback) noexcept {
    thread_atexit_report_callback_ = callback;
  }
  [[nodiscard]] std::uint64_t thread_atexit_report_callback() const noexcept {
    return thread_atexit_report_callback_;
  }
  void SetThreadDtorsCallback(std::uint64_t callback) noexcept {
    thread_dtors_callback_ = callback;
  }
  [[nodiscard]] std::uint64_t thread_dtors_callback() const noexcept {
    return thread_dtors_callback_;
  }

private:
  HandleTable handles_;
  KernelClockService clock_;
  GuestScheduler scheduler_;
  CxaGuardService cxa_guards_;
  PthreadService pthreads_;
  EventQueueService event_queues_;
  EventFlagService event_flags_;
  SemaphoreService semaphores_;
  FileService files_;
  DirectMemoryService direct_memory_;
  JsonValueService json_values_;
  LibcHeapService libc_heap_;
  ProcessLifecycleService process_lifecycle_;
  AmprCommandBufferService ampr_command_buffers_;
  std::uint64_t process_parameters_address_ = 0;
  std::uint64_t sanitizer_malloc_replace_address_ = 0;
  std::uint64_t sanitizer_new_replace_address_ = 0;
  std::uint64_t application_heap_api_address_ = 0;
  std::uint64_t thread_atexit_count_callback_ = 0;
  std::uint64_t thread_atexit_report_callback_ = 0;
  std::uint64_t thread_dtors_callback_ = 0;
};

} // namespace kajps5::kernel
