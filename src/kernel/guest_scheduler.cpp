// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/guest_scheduler.h"

#include <utility>

#include "kernel/object.h"

namespace kajps5::kernel {
namespace {

std::string ThreadExitWaitKey(KernelHandle handle) {
  return "thread-exit:" + std::to_string(handle);
}

} // namespace

struct GuestScheduler::GuestThread final : KernelObject {
  GuestThread(std::string thread_name, int thread_priority,
              std::uint64_t thread_entry_address,
              std::uint64_t thread_argument)
      : KernelObject(KernelObjectType::kThread), name(std::move(thread_name)),
        priority(thread_priority), entry_address(thread_entry_address),
        argument(thread_argument) {}

  std::string name;
  int priority = 0;
  GuestThreadState state = GuestThreadState::kReady;
  std::string wait_key;
  std::uint64_t exit_value = 0;
  std::uint64_t entry_address = 0;
  std::uint64_t argument = 0;
  std::optional<std::uint64_t> wait_deadline_nanoseconds;
  std::string timed_out_wait_key;
};

GuestScheduler::GuestScheduler(HandleTable &handles,
                               KernelClockService &clock) noexcept
    : handles_(handles), clock_(clock) {}

GuestScheduler::~GuestScheduler() = default;

GuestThreadCreateResult GuestScheduler::CreateThread(std::string name,
                                                     int priority,
                                                     std::uint64_t entry_address,
                                                     std::uint64_t argument) {
  if (name.size() > kMaximumGuestThreadNameLength) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  auto thread = std::make_shared<GuestThread>(
      std::move(name), priority, entry_address, argument);
  const auto handle = handles_.Insert(thread);
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }

  std::lock_guard lock(mutex_);
  threads_.emplace(*handle, std::move(thread));
  ready_threads_.push_back(*handle);
  return {KernelStatus::kOk, *handle};
}

bool GuestScheduler::DiscardReadyThread(KernelHandle handle) {
  std::lock_guard lock(mutex_);
  const auto found = threads_.find(handle);
  if (found == threads_.end() ||
      found->second->state != GuestThreadState::kReady) {
    return false;
  }
  if (!handles_.Remove(handle, KernelObjectType::kThread)) {
    return false;
  }

  std::erase(ready_threads_, handle);
  threads_.erase(found);
  return true;
}

std::optional<KernelHandle> GuestScheduler::SelectNext() {
  std::lock_guard lock(mutex_);
  if (current_thread_) {
    return std::nullopt;
  }
  WakeExpiredThreadsLocked(clock_.MonotonicNanoseconds());

  while (!ready_threads_.empty()) {
    const auto handle = ready_threads_.front();
    ready_threads_.pop_front();
    const auto found = threads_.find(handle);
    if (found == threads_.end() ||
        found->second->state != GuestThreadState::kReady) {
      continue;
    }

    found->second->state = GuestThreadState::kRunning;
    current_thread_ = handle;
    return handle;
  }
  return std::nullopt;
}

bool GuestScheduler::YieldCurrent() {
  std::lock_guard lock(mutex_);
  if (!current_thread_) {
    return false;
  }

  const auto found = threads_.find(*current_thread_);
  if (found == threads_.end() ||
      found->second->state != GuestThreadState::kRunning) {
    return false;
  }

  found->second->state = GuestThreadState::kReady;
  ready_threads_.push_back(*current_thread_);
  current_thread_.reset();
  return true;
}

bool GuestScheduler::BlockCurrent(std::string wait_key) {
  std::lock_guard lock(mutex_);
  return BlockCurrentLocked(std::move(wait_key), std::nullopt);
}

bool GuestScheduler::BlockCurrentUntil(
    std::string wait_key, std::uint64_t deadline_nanoseconds) {
  std::lock_guard lock(mutex_);
  return BlockCurrentLocked(std::move(wait_key), deadline_nanoseconds);
}

bool GuestScheduler::BlockCurrentLocked(
    std::string wait_key,
    std::optional<std::uint64_t> deadline_nanoseconds) {
  if (wait_key.empty()) {
    return false;
  }
  if (!current_thread_) {
    return false;
  }

  const auto found = threads_.find(*current_thread_);
  if (found == threads_.end() ||
      found->second->state != GuestThreadState::kRunning) {
    return false;
  }

  found->second->state = GuestThreadState::kBlocked;
  found->second->wait_key = std::move(wait_key);
  found->second->wait_deadline_nanoseconds = deadline_nanoseconds;
  found->second->timed_out_wait_key.clear();
  current_thread_.reset();
  return true;
}

std::size_t GuestScheduler::WakeBlockedThreads(std::string_view wait_key,
                                               std::size_t maximum_count) {
  if (wait_key.empty() || maximum_count == 0) {
    return 0;
  }

  std::lock_guard lock(mutex_);
  WakeExpiredThreadsLocked(clock_.MonotonicNanoseconds());
  std::size_t wake_count = 0;
  for (auto &[handle, thread] : threads_) {
    if (wake_count == maximum_count) {
      break;
    }
    if (thread->state != GuestThreadState::kBlocked ||
        thread->wait_key != wait_key) {
      continue;
    }

    thread->state = GuestThreadState::kReady;
    thread->wait_key.clear();
    thread->wait_deadline_nanoseconds.reset();
    thread->timed_out_wait_key.clear();
    ready_threads_.push_back(handle);
    ++wake_count;
  }
  return wake_count;
}

bool GuestScheduler::WakeBlockedThread(KernelHandle handle,
                                       std::string_view wait_key) {
  if (handle == kInvalidKernelHandle || wait_key.empty()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  WakeExpiredThreadsLocked(clock_.MonotonicNanoseconds());
  const auto found = threads_.find(handle);
  if (found == threads_.end() ||
      found->second->state != GuestThreadState::kBlocked ||
      found->second->wait_key != wait_key) {
    return false;
  }

  found->second->state = GuestThreadState::kReady;
  found->second->wait_key.clear();
  found->second->wait_deadline_nanoseconds.reset();
  found->second->timed_out_wait_key.clear();
  ready_threads_.push_back(handle);
  return true;
}

GuestThreadJoinResult GuestScheduler::JoinThread(KernelHandle handle) {
  std::lock_guard lock(mutex_);
  const auto target = threads_.find(handle);
  if (target == threads_.end()) {
    return {KernelStatus::kNotFound, 0};
  }
  if (target->second->state == GuestThreadState::kExited) {
    return {KernelStatus::kOk, target->second->exit_value};
  }
  if (!current_thread_) {
    return {KernelStatus::kBusy, 0};
  }
  if (*current_thread_ == handle) {
    return {KernelStatus::kInvalidArgument, 0};
  }

  const auto caller = threads_.find(*current_thread_);
  if (caller == threads_.end() ||
      caller->second->state != GuestThreadState::kRunning) {
    return {KernelStatus::kBusy, 0};
  }
  caller->second->state = GuestThreadState::kBlocked;
  caller->second->wait_key = ThreadExitWaitKey(handle);
  caller->second->wait_deadline_nanoseconds.reset();
  caller->second->timed_out_wait_key.clear();
  current_thread_.reset();
  return {KernelStatus::kWouldBlock, 0};
}

bool GuestScheduler::ExitCurrent(std::uint64_t exit_value) {
  std::lock_guard lock(mutex_);
  if (!current_thread_) {
    return false;
  }

  const auto found = threads_.find(*current_thread_);
  if (found == threads_.end() ||
      found->second->state != GuestThreadState::kRunning) {
    return false;
  }

  found->second->state = GuestThreadState::kExited;
  found->second->wait_key.clear();
  found->second->wait_deadline_nanoseconds.reset();
  found->second->timed_out_wait_key.clear();
  found->second->exit_value = exit_value;
  const auto wait_key = ThreadExitWaitKey(*current_thread_);
  for (auto &[handle, thread] : threads_) {
    if (thread->state != GuestThreadState::kBlocked ||
        thread->wait_key != wait_key) {
      continue;
    }
    thread->state = GuestThreadState::kReady;
    thread->wait_key.clear();
    thread->wait_deadline_nanoseconds.reset();
    thread->timed_out_wait_key.clear();
    ready_threads_.push_back(handle);
  }
  current_thread_.reset();
  return true;
}

bool GuestScheduler::CurrentThreadTimedOut(std::string_view wait_key) const {
  if (wait_key.empty()) {
    return false;
  }
  std::lock_guard lock(mutex_);
  if (!current_thread_) {
    return false;
  }
  const auto found = threads_.find(*current_thread_);
  return found != threads_.end() &&
         found->second->state == GuestThreadState::kRunning &&
         found->second->timed_out_wait_key == wait_key;
}

bool GuestScheduler::ConsumeCurrentThreadTimeout(std::string_view wait_key) {
  if (wait_key.empty()) {
    return false;
  }
  std::lock_guard lock(mutex_);
  if (!current_thread_) {
    return false;
  }
  const auto found = threads_.find(*current_thread_);
  if (found == threads_.end() ||
      found->second->state != GuestThreadState::kRunning ||
      found->second->timed_out_wait_key != wait_key) {
    return false;
  }
  found->second->timed_out_wait_key.clear();
  return true;
}

std::optional<KernelHandle> GuestScheduler::current_thread() const {
  std::lock_guard lock(mutex_);
  return current_thread_;
}

std::optional<GuestThreadSnapshot>
GuestScheduler::Snapshot(KernelHandle handle) const {
  std::lock_guard lock(mutex_);
  const auto found = threads_.find(handle);
  if (found == threads_.end()) {
    return std::nullopt;
  }
  return MakeSnapshot(handle, *found->second);
}

std::vector<GuestThreadSnapshot> GuestScheduler::SnapshotAll() const {
  std::lock_guard lock(mutex_);
  std::vector<GuestThreadSnapshot> snapshots;
  snapshots.reserve(threads_.size());
  for (const auto &[handle, thread] : threads_) {
    snapshots.push_back(MakeSnapshot(handle, *thread));
  }
  return snapshots;
}

void GuestScheduler::WakeExpiredThreadsLocked(
    std::uint64_t now_nanoseconds) {
  for (auto &[handle, thread] : threads_) {
    if (thread->state != GuestThreadState::kBlocked ||
        !thread->wait_deadline_nanoseconds ||
        *thread->wait_deadline_nanoseconds > now_nanoseconds) {
      continue;
    }
    thread->state = GuestThreadState::kReady;
    thread->timed_out_wait_key = thread->wait_key;
    thread->wait_key.clear();
    thread->wait_deadline_nanoseconds.reset();
    ready_threads_.push_back(handle);
  }
}

GuestThreadSnapshot GuestScheduler::MakeSnapshot(KernelHandle handle,
                                                 const GuestThread &thread) {
  return {handle,
          thread.name,
          thread.priority,
          thread.state,
          thread.wait_key,
          thread.exit_value,
          thread.entry_address,
          thread.argument};
}

} // namespace kajps5::kernel
