// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/event_queue.h"

#include <algorithm>
#include <charconv>
#include <utility>

#include "kernel/guest_scheduler.h"

namespace kajps5::kernel {

EventQueue::EventQueue(std::string name)
    : KernelObject(KernelObjectType::kEventQueue), name_(std::move(name)) {}

const std::string &EventQueue::name() const noexcept { return name_; }

KernelStatus EventQueue::AddUserEvent(std::uint64_t ident, bool edge) {
  std::lock_guard lock(mutex_);
  const auto found = std::find_if(
      registrations_.begin(), registrations_.end(),
      [ident](const auto &event) {
        return event.ident == ident && event.filter == kEventFilterUser;
      });
  if (found != registrations_.end()) {
    return KernelStatus::kOk;
  }
  registrations_.push_back(
      {ident, kEventFilterUser,
       static_cast<std::uint16_t>(kEventAdd | (edge ? kEventClear : 0))});
  return KernelStatus::kOk;
}

KernelStatus EventQueue::DeleteUserEvent(std::uint64_t ident) {
  std::lock_guard lock(mutex_);
  const auto registered = std::find_if(
      registrations_.begin(), registrations_.end(),
      [ident](const auto &event) {
        return event.ident == ident && event.filter == kEventFilterUser;
      });
  if (registered == registrations_.end()) {
    return KernelStatus::kNoSuchEntry;
  }
  registrations_.erase(registered);
  std::erase_if(pending_events_, [ident](const auto &event) {
    return event.ident == ident && event.filter == kEventFilterUser;
  });
  return KernelStatus::kOk;
}

KernelStatus EventQueue::TriggerUserEvent(std::uint64_t ident,
                                          std::uint64_t user_data) {
  std::lock_guard lock(mutex_);
  const auto registered = std::find_if(
      registrations_.begin(), registrations_.end(),
      [ident](const auto &event) {
        return event.ident == ident && event.filter == kEventFilterUser;
      });
  if (registered == registrations_.end()) {
    return KernelStatus::kNoSuchEntry;
  }

  const auto pending = std::find_if(
      pending_events_.begin(), pending_events_.end(),
      [ident](const auto &event) {
        return event.ident == ident && event.filter == kEventFilterUser;
      });
  if (pending != pending_events_.end()) {
    *pending = {registered->ident, registered->filter, registered->flags, 0, 0,
                user_data};
    return KernelStatus::kOk;
  }

  pending_events_.push_back({registered->ident, registered->filter,
                             registered->flags, 0, 0, user_data});
  return KernelStatus::kOk;
}

std::vector<KernelEvent> EventQueue::Poll(std::size_t maximum_count) {
  std::lock_guard lock(mutex_);
  const auto count = std::min(maximum_count, pending_events_.size());
  std::vector<KernelEvent> events;
  events.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    events.push_back(pending_events_.front());
    pending_events_.pop_front();
  }
  for (const auto &event : events) {
    if ((event.flags & kEventClear) == 0) {
      pending_events_.push_back(event);
    }
  }
  return events;
}

EventQueueService::EventQueueService(HandleTable &handles,
                                     GuestScheduler &scheduler) noexcept
    : handles_(handles), scheduler_(scheduler) {}

EventQueueCreateResult EventQueueService::Create(std::string name) {
  if (name.size() > kMaximumEventQueueNameLength) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }
  auto queue = std::make_shared<EventQueue>(std::move(name));
  const auto handle = handles_.Insert(std::move(queue));
  return handle ? EventQueueCreateResult{KernelStatus::kOk, *handle}
                : EventQueueCreateResult{KernelStatus::kNoResources,
                                         kInvalidKernelHandle};
}

KernelStatus EventQueueService::Delete(KernelHandle handle) {
  std::lock_guard wait_lock(wait_mutex_);
  if (!handles_.Remove(handle, KernelObjectType::kEventQueue)) {
    return KernelStatus::kNotFound;
  }
  const auto wait_key = MakeWaitKey(handle);
  for (auto &[thread_handle, waiter] : waiters_) {
    if (waiter.queue_handle != handle ||
        waiter.completion != WaitCompletion::kWaiting) {
      continue;
    }
    waiter.completion = WaitCompletion::kDeleted;
    (void)scheduler_.WakeBlockedThread(thread_handle, wait_key);
  }
  return KernelStatus::kOk;
}

KernelStatus EventQueueService::AddUserEvent(KernelHandle handle,
                                             std::uint64_t ident, bool edge) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto queue = Find(handle);
  return queue ? queue->AddUserEvent(ident, edge) : KernelStatus::kNotFound;
}

KernelStatus EventQueueService::DeleteUserEvent(KernelHandle handle,
                                                std::uint64_t ident) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto queue = Find(handle);
  return queue ? queue->DeleteUserEvent(ident) : KernelStatus::kNotFound;
}

KernelStatus EventQueueService::TriggerUserEvent(KernelHandle handle,
                                                 std::uint64_t ident,
                                                 std::uint64_t user_data) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto queue = Find(handle);
  if (!queue) {
    return KernelStatus::kNotFound;
  }
  const auto status = queue->TriggerUserEvent(ident, user_data);
  if (status == KernelStatus::kOk) {
    ReserveWaitingThreadsLocked(handle, queue);
  }
  return status;
}

EventQueuePollResult EventQueueService::Poll(KernelHandle handle,
                                             std::size_t maximum_count) {
  std::lock_guard wait_lock(wait_mutex_);
  return PollLocked(handle, maximum_count);
}

EventQueuePollResult EventQueueService::PollLocked(
    KernelHandle handle, std::size_t maximum_count) {
  if (maximum_count == 0) {
    return {KernelStatus::kInvalidArgument, {}};
  }
  const auto queue = Find(handle);
  if (!queue) {
    return {KernelStatus::kNotFound, {}};
  }
  auto events = queue->Poll(maximum_count);
  if (events.empty()) {
    return {KernelStatus::kBusy, {}};
  }
  return {KernelStatus::kOk, std::move(events)};
}

EventQueuePollResult EventQueueService::Wait(KernelHandle handle,
                                             std::size_t maximum_count) {
  std::lock_guard wait_lock(wait_mutex_);
  if (maximum_count == 0) {
    return {KernelStatus::kInvalidArgument, {}};
  }

  const auto current_thread = scheduler_.current_thread();
  if (current_thread) {
    const auto existing = waiters_.find(*current_thread);
    if (existing != waiters_.end()) {
      if (existing->second.queue_handle != handle) {
        return {KernelStatus::kBusy, {}};
      }
      if (existing->second.completion == WaitCompletion::kReserved) {
        auto events = std::move(existing->second.events);
        waiters_.erase(existing);
        return {KernelStatus::kOk, std::move(events)};
      }
      if (existing->second.completion == WaitCompletion::kDeleted) {
        waiters_.erase(existing);
        return {KernelStatus::kPermissionDenied, {}};
      }

      auto result = PollLocked(handle, existing->second.maximum_count);
      if (result.status != KernelStatus::kBusy) {
        waiters_.erase(existing);
        return result;
      }
      if (scheduler_.BlockCurrent(MakeWaitKey(handle))) {
        return {KernelStatus::kWouldBlock, {}};
      }
      return result;
    }
  }

  auto result = PollLocked(handle, maximum_count);
  if (result.status != KernelStatus::kBusy) {
    return result;
  }
  if (!current_thread) {
    return result;
  }
  waiters_.emplace(*current_thread,
                   Waiter{handle, maximum_count, WaitCompletion::kWaiting, {}});
  if (!scheduler_.BlockCurrent(MakeWaitKey(handle))) {
    waiters_.erase(*current_thread);
    return result;
  }
  return {KernelStatus::kWouldBlock, {}};
}

void EventQueueService::ReserveWaitingThreadsLocked(
    KernelHandle handle, const std::shared_ptr<EventQueue> &queue) {
  const auto wait_key = MakeWaitKey(handle);
  for (auto &[thread_handle, waiter] : waiters_) {
    if (waiter.queue_handle != handle ||
        waiter.completion != WaitCompletion::kWaiting) {
      continue;
    }
    auto events = queue->Poll(waiter.maximum_count);
    if (events.empty()) {
      break;
    }
    waiter.events = std::move(events);
    waiter.completion = WaitCompletion::kReserved;
    (void)scheduler_.WakeBlockedThread(thread_handle, wait_key);
  }
}

std::shared_ptr<EventQueue>
EventQueueService::Find(KernelHandle handle) const {
  return std::static_pointer_cast<EventQueue>(
      handles_.Find(handle, KernelObjectType::kEventQueue));
}

std::string EventQueueService::MakeWaitKey(KernelHandle handle) {
  char digits[20]{};
  const auto converted =
      std::to_chars(digits, digits + sizeof(digits), handle, 16);
  return "event_queue:" +
         std::string(digits, static_cast<std::size_t>(converted.ptr - digits));
}

} // namespace kajps5::kernel
