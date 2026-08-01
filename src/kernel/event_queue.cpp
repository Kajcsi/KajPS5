// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/event_queue.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <new>
#include <utility>

#include "kernel/clock.h"
#include "kernel/guest_scheduler.h"

namespace kajps5::kernel {

EventQueue::EventQueue(std::string name)
    : KernelObject(KernelObjectType::kEventQueue), name_(std::move(name)) {}

const std::string &EventQueue::name() const noexcept { return name_; }

EventRegistrationResult EventQueue::RegisterEvent(
    std::uint64_t ident, std::int16_t filter, std::uint16_t flags,
    std::uint64_t user_data) {
  std::lock_guard lock(mutex_);
  const auto found = std::find_if(
      registrations_.begin(), registrations_.end(),
      [ident, filter](const auto &event) {
        return event.ident == ident && event.filter == filter;
      });
  if (found != registrations_.end() && found->flags == flags &&
      found->user_data == user_data) {
    return {KernelStatus::kOk, found->generation};
  }
  if (generation_exhausted_) {
    return {KernelStatus::kNoResources, 0};
  }
  const auto generation = next_generation_;
  if (next_generation_ == std::numeric_limits<std::uint64_t>::max()) {
    generation_exhausted_ = true;
  } else {
    ++next_generation_;
  }
  if (found != registrations_.end()) {
    found->flags = flags;
    found->user_data = user_data;
    found->generation = generation;
  } else {
    try {
      registrations_.push_back(
          {ident, filter, flags, user_data, generation});
    } catch (const std::bad_alloc &) {
      return {KernelStatus::kNoResources, 0};
    }
  }
  std::erase_if(pending_events_, [ident, filter](const auto &queued) {
    return queued.event.ident == ident && queued.event.filter == filter;
  });
  return {KernelStatus::kOk, generation};
}

KernelStatus EventQueue::DeleteEvent(std::uint64_t ident,
                                     std::int16_t filter) {
  std::lock_guard lock(mutex_);
  const auto registered = std::find_if(
      registrations_.begin(), registrations_.end(),
      [ident, filter](const auto &event) {
        return event.ident == ident && event.filter == filter;
      });
  if (registered == registrations_.end()) {
    return KernelStatus::kNoSuchEntry;
  }
  registrations_.erase(registered);
  std::erase_if(pending_events_, [ident, filter](const auto &queued) {
    return queued.event.ident == ident && queued.event.filter == filter;
  });
  return KernelStatus::kOk;
}

KernelStatus EventQueue::AddUserEvent(std::uint64_t ident, bool edge) {
  return RegisterEvent(
             ident, kEventFilterUser,
             static_cast<std::uint16_t>(kEventAdd |
                                        (edge ? kEventClear : 0)),
             0)
      .status;
}

KernelStatus EventQueue::DeleteUserEvent(std::uint64_t ident) {
  return DeleteEvent(ident, kEventFilterUser);
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
      [ident, generation = registered->generation](const auto &queued) {
        return queued.event.ident == ident &&
               queued.event.filter == kEventFilterUser &&
               queued.registration_generation == generation;
      });
  if (pending != pending_events_.end()) {
    pending->event = {registered->ident, registered->filter,
                      registered->flags, 0, 0, user_data};
    return KernelStatus::kOk;
  }

  pending_events_.push_back(
      {{registered->ident, registered->filter, registered->flags, 0, 0,
        user_data},
       registered->generation,
       (registered->flags & kEventClear) == 0});
  return KernelStatus::kOk;
}

bool EventQueue::TriggerFirstByFilter(std::int16_t filter,
                                      std::uint64_t data) {
  std::lock_guard lock(mutex_);
  const auto registered = std::find_if(
      registrations_.begin(), registrations_.end(),
      [filter](const auto &event) { return event.filter == filter; });
  if (registered == registrations_.end()) {
    return false;
  }
  const auto pending = std::find_if(
      pending_events_.begin(), pending_events_.end(),
      [registered](const auto &queued) {
        return queued.event.ident == registered->ident &&
               queued.event.filter == registered->filter &&
               queued.registration_generation == registered->generation;
      });
  const RegisteredKernelEvent event{
      {registered->ident, registered->filter, 0, 1, data,
       registered->user_data},
      registered->generation,
      false};
  if (pending != pending_events_.end()) {
    *pending = event;
  } else {
    pending_events_.push_back(event);
  }
  return true;
}

std::vector<RegisteredKernelEvent> EventQueue::Poll(
    std::size_t maximum_count) {
  std::lock_guard lock(mutex_);
  const auto count = std::min(maximum_count, pending_events_.size());
  std::vector<RegisteredKernelEvent> events;
  events.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    events.push_back(pending_events_.front());
    pending_events_.pop_front();
  }
  for (const auto &event : events) {
    if (event.persistent) {
      pending_events_.push_back(event);
    }
  }
  return events;
}

bool EventQueue::IsCurrent(const RegisteredKernelEvent &event) const {
  std::lock_guard lock(mutex_);
  return std::any_of(
      registrations_.begin(), registrations_.end(),
      [&event](const auto &registration) {
        return registration.ident == event.event.ident &&
               registration.filter == event.event.filter &&
               registration.generation == event.registration_generation;
      });
}

EventQueueService::EventQueueService(HandleTable &handles,
                                     GuestScheduler &scheduler,
                                     KernelClockService &clock) noexcept
    : handles_(handles), scheduler_(scheduler), clock_(clock) {}

EventQueueCreateResult EventQueueService::Create(std::string name) {
  if (name.size() > kMaximumEventQueueNameLength) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }
  auto queue = std::make_shared<EventQueue>(std::move(name));
  const auto handle = handles_.Insert(queue);
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }
  std::lock_guard wait_lock(wait_mutex_);
  try {
    queues_.emplace(*handle, std::move(queue));
  } catch (const std::bad_alloc &) {
    (void)handles_.Remove(*handle, KernelObjectType::kEventQueue);
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }
  return {KernelStatus::kOk, *handle};
}

KernelStatus EventQueueService::Delete(KernelHandle handle) {
  std::lock_guard wait_lock(wait_mutex_);
  if (!handles_.Remove(handle, KernelObjectType::kEventQueue)) {
    return KernelStatus::kNotFound;
  }
  queues_.erase(handle);
  const auto wait_key = MakeWaitKey(handle);
  for (auto &[thread_handle, waiter] : waiters_) {
    if (waiter.queue_handle != handle) {
      continue;
    }
    const auto was_waiting = waiter.completion == WaitCompletion::kWaiting;
    waiter.completion = WaitCompletion::kDeleted;
    waiter.events.clear();
    if (was_waiting) {
      (void)scheduler_.WakeBlockedThread(thread_handle, wait_key);
    }
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

KernelStatus EventQueueService::AddGraphicsEvent(
    KernelHandle handle, std::uint64_t ident, std::uint64_t user_data) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto queue = Find(handle);
  return queue ? queue->RegisterEvent(ident, kEventFilterGraphics, 0,
                                      user_data).status
               : KernelStatus::kNotFound;
}

KernelStatus EventQueueService::DeleteGraphicsEvent(
    KernelHandle handle, std::uint64_t ident) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto queue = Find(handle);
  return queue ? queue->DeleteEvent(ident, kEventFilterGraphics)
               : KernelStatus::kNotFound;
}

std::size_t EventQueueService::TriggerGraphicsEvents(std::uint64_t data) {
  std::lock_guard wait_lock(wait_mutex_);
  std::size_t triggered = 0;
  for (const auto &[handle, queue] : queues_) {
    if (!queue->TriggerFirstByFilter(kEventFilterGraphics, data)) {
      continue;
    }
    ++triggered;
    ReserveWaitingThreadsLocked(handle, queue);
  }
  return triggered;
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
  auto current = CurrentEvents(queue, std::move(events));
  return current.empty()
             ? EventQueuePollResult{KernelStatus::kBusy, {}}
             : EventQueuePollResult{KernelStatus::kOk, std::move(current)};
}

EventQueuePollResult EventQueueService::Wait(KernelHandle handle,
                                             std::size_t maximum_count,
                                             std::optional<std::uint64_t>
                                                 timeout_microseconds) {
  std::lock_guard wait_lock(wait_mutex_);
  if (maximum_count == 0) {
    return {KernelStatus::kInvalidArgument, {}};
  }

  const auto current_thread = scheduler_.current_thread();
  if (current_thread) {
    const auto existing = waiters_.find(*current_thread);
    if (existing != waiters_.end()) {
      const auto wait_key = MakeWaitKey(handle);
      if (existing->second.queue_handle != handle) {
        return {KernelStatus::kBusy, {}};
      }
      if (existing->second.completion == WaitCompletion::kDeleted) {
        waiters_.erase(existing);
        return {KernelStatus::kPermissionDenied, {}};
      }
      if (existing->second.completion == WaitCompletion::kReserved) {
        const auto queue = Find(handle);
        if (!queue) {
          waiters_.erase(existing);
          return {KernelStatus::kPermissionDenied, {}};
        }
        auto events = CurrentEvents(queue, std::move(existing->second.events));
        if (!events.empty()) {
          waiters_.erase(existing);
          return {KernelStatus::kOk, std::move(events)};
        }
        existing->second.completion = WaitCompletion::kWaiting;
      }

      if (scheduler_.ConsumeCurrentThreadTimeout(wait_key)) {
        waiters_.erase(existing);
        return {KernelStatus::kTimedOut, {}};
      }

      auto result = PollLocked(handle, existing->second.maximum_count);
      if (result.status != KernelStatus::kBusy) {
        waiters_.erase(existing);
        return result;
      }
      const auto blocked = existing->second.deadline_nanoseconds
                               ? scheduler_.BlockCurrentUntil(
                                     wait_key,
                                     *existing->second.deadline_nanoseconds)
                               : scheduler_.BlockCurrent(wait_key);
      if (blocked) {
        return {KernelStatus::kWouldBlock, {}};
      }
      return result;
    }
  }

  auto result = PollLocked(handle, maximum_count);
  if (result.status != KernelStatus::kBusy) {
    return result;
  }
  if (timeout_microseconds && *timeout_microseconds == 0) {
    return {KernelStatus::kTimedOut, {}};
  }
  if (!current_thread) {
    return result;
  }

  std::optional<std::uint64_t> deadline;
  if (timeout_microseconds) {
    constexpr auto kNanosecondsPerMicrosecond = std::uint64_t{1'000};
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto duration =
        *timeout_microseconds > maximum / kNanosecondsPerMicrosecond
            ? maximum
            : *timeout_microseconds * kNanosecondsPerMicrosecond;
    const auto now = clock_.MonotonicNanoseconds();
    deadline = duration > maximum - now ? maximum : now + duration;
  }
  waiters_.emplace(
      *current_thread,
      Waiter{handle, maximum_count, deadline, WaitCompletion::kWaiting, {}});
  const auto wait_key = MakeWaitKey(handle);
  const auto blocked = deadline
                           ? scheduler_.BlockCurrentUntil(wait_key, *deadline)
                           : scheduler_.BlockCurrent(wait_key);
  if (!blocked) {
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
    if (waiter.deadline_nanoseconds &&
        *waiter.deadline_nanoseconds <= clock_.MonotonicNanoseconds()) {
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

std::vector<KernelEvent> EventQueueService::CurrentEvents(
    const std::shared_ptr<EventQueue> &queue,
    std::vector<RegisteredKernelEvent> events) {
  std::vector<KernelEvent> current;
  current.reserve(events.size());
  for (const auto &event : events) {
    if (queue->IsCurrent(event)) {
      current.push_back(event.event);
    }
  }
  return current;
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
