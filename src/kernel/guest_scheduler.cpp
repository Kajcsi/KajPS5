// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/guest_scheduler.h"

#include <utility>

#include "kernel/object.h"

namespace kajps5::kernel {

struct GuestScheduler::GuestThread final : KernelObject {
  GuestThread(std::string thread_name, int thread_priority)
      : KernelObject(KernelObjectType::kThread), name(std::move(thread_name)),
        priority(thread_priority) {}

  std::string name;
  int priority = 0;
  GuestThreadState state = GuestThreadState::kReady;
  std::string wait_key;
  std::uint64_t exit_value = 0;
};

GuestScheduler::GuestScheduler(HandleTable &handles) noexcept
    : handles_(handles) {}

GuestScheduler::~GuestScheduler() = default;

GuestThreadCreateResult GuestScheduler::CreateThread(std::string name,
                                                     int priority) {
  if (name.size() > kMaximumGuestThreadNameLength) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  auto thread = std::make_shared<GuestThread>(std::move(name), priority);
  const auto handle = handles_.Insert(thread);
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }

  std::lock_guard lock(mutex_);
  threads_.emplace(*handle, std::move(thread));
  ready_threads_.push_back(*handle);
  return {KernelStatus::kOk, *handle};
}

std::optional<KernelHandle> GuestScheduler::SelectNext() {
  std::lock_guard lock(mutex_);
  if (current_thread_) {
    return std::nullopt;
  }

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
  if (wait_key.empty()) {
    return false;
  }

  std::lock_guard lock(mutex_);
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
  current_thread_.reset();
  return true;
}

std::size_t GuestScheduler::WakeBlockedThreads(std::string_view wait_key,
                                               std::size_t maximum_count) {
  if (wait_key.empty() || maximum_count == 0) {
    return 0;
  }

  std::lock_guard lock(mutex_);
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
    ready_threads_.push_back(handle);
    ++wake_count;
  }
  return wake_count;
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
  found->second->exit_value = exit_value;
  current_thread_.reset();
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

GuestThreadSnapshot GuestScheduler::MakeSnapshot(KernelHandle handle,
                                                 const GuestThread &thread) {
  return {handle,       thread.name,     thread.priority,
          thread.state, thread.wait_key, thread.exit_value};
}

} // namespace kajps5::kernel
