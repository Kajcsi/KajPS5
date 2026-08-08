// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "core/memory/guest_memory.h"
#include "kernel/pthread.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

class PthreadClockSource final : public kajps5::kernel::KernelClockSource {
 public:
  [[nodiscard]] std::int64_t RealtimeNanoseconds() const override {
    return realtime_nanoseconds;
  }

  [[nodiscard]] std::uint64_t MonotonicNanoseconds() const override {
    return monotonic_nanoseconds;
  }

  std::int64_t realtime_nanoseconds = 0;
  std::uint64_t monotonic_nanoseconds = 0;
};

}  // namespace

int main() {
  using namespace kajps5::kernel;

  KernelRuntime runtime;
  auto& pthreads = runtime.pthreads();
  auto& scheduler = runtime.scheduler();

  const auto attributes = pthreads.CreateMutexAttribute();
  Check(static_cast<bool>(attributes), "mutex attribute creation failed");
  const auto defaults = pthreads.GetMutexAttribute(attributes.handle);
  Check(defaults && defaults->type == kPthreadMutexErrorCheck &&
            defaults->protocol == 0,
        "mutex attribute defaults are incorrect");
  Check(pthreads.SetMutexAttributeType(attributes.handle,
                                       kPthreadMutexRecursive) ==
                KernelStatus::kOk &&
            pthreads.SetMutexAttributeProtocol(attributes.handle, 1) ==
                KernelStatus::kOk,
        "mutex attribute update failed");
  Check(pthreads.SetMutexAttributeType(attributes.handle, 0) ==
                KernelStatus::kInvalidArgument &&
            pthreads.SetMutexAttributeProtocol(attributes.handle, 3) ==
                KernelStatus::kInvalidArgument,
        "invalid mutex attributes were accepted");

  const auto mutex = pthreads.CreateMutex(attributes.handle);
  const auto initial_mutex = pthreads.GetMutex(mutex.handle);
  Check(static_cast<bool>(mutex) && initial_mutex &&
            initial_mutex->type == kPthreadMutexRecursive &&
            initial_mutex->protocol == 1,
        "mutex did not copy its attributes");

  const auto owner = scheduler.CreateThread("owner", 700);
  const auto waiter = scheduler.CreateThread("waiter", 700);
  const auto observer = scheduler.CreateThread("observer", 700);
  Check(owner && waiter && observer && scheduler.SelectNext() == owner.handle,
        "mutex scheduler setup failed");
  const auto recursive_lock =
      pthreads.LockMutex(mutex.handle, false) == KernelStatus::kOk &&
      pthreads.LockMutex(mutex.handle, false) == KernelStatus::kOk;
  const auto recursively_owned = pthreads.GetMutex(mutex.handle);
  Check(recursive_lock && recursively_owned &&
            recursively_owned->recursion_count == 2,
        "recursive mutex did not track nested ownership");
  const auto first_unlock =
      pthreads.UnlockMutex(mutex.handle) == KernelStatus::kOk;
  const auto partly_owned = pthreads.GetMutex(mutex.handle);
  Check(first_unlock && partly_owned && partly_owned->recursion_count == 1,
        "recursive mutex released too much state");
  Check(scheduler.YieldCurrent() && scheduler.SelectNext() == waiter.handle,
        "mutex waiter was not selected");
  Check(pthreads.LockMutex(mutex.handle, false) == KernelStatus::kWouldBlock &&
            pthreads.GetMutex(mutex.handle)->waiter_count == 1,
        "contended mutex did not block its waiter");
  Check(scheduler.SelectNext() == observer.handle &&
            pthreads.LockMutex(mutex.handle, true) == KernelStatus::kBusy,
        "pthread_mutex_trylock acquired a contended mutex");
  Check(pthreads.ExitCurrent(0), "observer thread exit failed");
  Check(scheduler.SelectNext() == owner.handle &&
            pthreads.UnlockMutex(mutex.handle) == KernelStatus::kOk,
        "mutex owner did not release its final acquisition");
  const auto granted_mutex = pthreads.GetMutex(mutex.handle);
  Check(granted_mutex && granted_mutex->owner == waiter.handle &&
            granted_mutex->waiter_count == 0,
        "mutex ownership was not granted to the first waiter");
  Check(pthreads.ExitCurrent(0), "mutex owner exit failed");
  Check(scheduler.SelectNext() == waiter.handle &&
            pthreads.LockMutex(mutex.handle, false) == KernelStatus::kOk &&
            pthreads.UnlockMutex(mutex.handle) == KernelStatus::kOk,
        "woken mutex waiter did not complete its lock call");
  Check(pthreads.DestroyMutex(mutex.handle) == KernelStatus::kOk,
        "released mutex could not be destroyed");

  const auto abandoned = pthreads.CreateMutex(0);
  Check(static_cast<bool>(abandoned) &&
            pthreads.LockMutex(abandoned.handle, false) == KernelStatus::kOk &&
            pthreads.ExitCurrent(0x55),
        "owned mutex abandonment setup failed");
  const auto abandoned_state = pthreads.GetMutex(abandoned.handle);
  Check(abandoned_state &&
            abandoned_state->owner == kInvalidKernelHandle &&
            abandoned_state->recursion_count == 0,
        "thread exit left a mutex owned");
  Check(pthreads.DestroyMutex(abandoned.handle) == KernelStatus::kOk,
        "abandoned mutex could not be destroyed");

  Check(pthreads.DestroyMutexAttribute(attributes.handle) == KernelStatus::kOk,
        "mutex attribute destruction failed");
  Check(pthreads.DestroyMutexAttribute(attributes.handle) ==
            KernelStatus::kNotFound,
        "stale mutex attribute was accepted");

  // HLE mutexes use guest-resident opaque objects, while CreateMutex above
  // remains the synthetic/internal compatibility path.
  kajps5::memory::GuestMemory guest_mutex_memory(
      0x1000, kPthreadMutexArenaSize + 0x2000);
  KernelRuntime guest_mutex_runtime;
  auto& guest_mutex_pthreads = guest_mutex_runtime.pthreads();
  Check(guest_mutex_pthreads.ConfigureGuestMutexArena(guest_mutex_memory,
                                                       0x1000),
        "guest mutex arena configuration failed");
  Check(guest_mutex_memory.CanAccess(
            0x1000, kPthreadMutexArenaSize,
            kajps5::memory::GuestMemoryProtection::kWrite),
        "guest mutex arena fixture was not mapped writable");
  Check(guest_mutex_pthreads.CreateGuestMutex(0, 99).status ==
                KernelStatus::kInvalidArgument,
        "invalid guest mutex initialization did not fail before reserving a slot");
  const auto guest_mutex = guest_mutex_pthreads.CreateGuestMutex(
      0, kPthreadMutexRecursive);
  Check(guest_mutex && guest_mutex.handle == 0x1000 &&
            guest_mutex_pthreads.GetMutex(guest_mutex.handle) &&
            guest_mutex_pthreads.GetMutex(guest_mutex.handle)->type ==
                kPthreadMutexRecursive,
        "guest mutex allocation did not expose the first opaque-object slot");
  std::vector<PthreadMutexCreateResult> guest_mutexes;
  guest_mutexes.reserve(kPthreadMutexArenaSlotCount - 1);
  for (std::size_t index = 1; index < kPthreadMutexArenaSlotCount; ++index) {
    guest_mutexes.push_back(guest_mutex_pthreads.CreateGuestMutex(0));
  }
  Check(std::all_of(guest_mutexes.begin(), guest_mutexes.end(),
                    [](const PthreadMutexCreateResult& created) {
                      return static_cast<bool>(created);
                    }) &&
            guest_mutex_pthreads.CreateGuestMutex(0).status ==
                KernelStatus::kNoResources,
        "guest mutex arena did not enforce bounded exhaustion");
  Check(guest_mutex_pthreads.DestroyMutex(guest_mutex.handle) ==
                KernelStatus::kOk &&
            guest_mutex_pthreads.CreateGuestMutex(0).handle ==
                guest_mutex.handle,
        "guest mutex arena did not reuse a released object slot");

  KernelRuntime collision_runtime;
  auto& collision_pthreads = collision_runtime.pthreads();
  const auto first_internal_mutex = collision_pthreads.CreateMutex(0);
  Check(static_cast<bool>(first_internal_mutex),
        "collision fixture did not create its initial internal mutex");
  kajps5::memory::GuestMemory collision_memory(
      first_internal_mutex.handle,
      kPthreadMutexArenaSize + kPthreadMutexObjectSize);
  Check(collision_pthreads.ConfigureGuestMutexArena(
            collision_memory, first_internal_mutex.handle),
        "guest arena could not overlap an existing synthetic mutex handle");
  const auto skipped_guest_mutex = collision_pthreads.CreateGuestMutex(0);
  Check(skipped_guest_mutex &&
            skipped_guest_mutex.handle ==
                first_internal_mutex.handle + kPthreadMutexObjectSize &&
            skipped_guest_mutex.handle != first_internal_mutex.handle &&
            collision_pthreads.DestroyMutex(skipped_guest_mutex.handle) ==
                KernelStatus::kOk &&
            collision_pthreads.ReleaseGuestMutexArena(),
        "guest mutex allocation did not skip an occupied synthetic handle");
  Check(collision_pthreads.ConfigureGuestMutexArena(
            collision_memory, first_internal_mutex.handle + 1),
        "guest arena could not overlap a future synthetic mutex handle");
  const auto collision_guest_mutex = collision_pthreads.CreateGuestMutex(0);
  const auto second_internal_mutex = collision_pthreads.CreateMutex(0);
  Check(collision_guest_mutex && second_internal_mutex &&
            collision_guest_mutex.handle == first_internal_mutex.handle + 1 &&
            second_internal_mutex.handle == first_internal_mutex.handle + 2 &&
            collision_guest_mutex.handle != first_internal_mutex.handle &&
            collision_guest_mutex.handle != second_internal_mutex.handle &&
            collision_pthreads.GetMutex(first_internal_mutex.handle) &&
            collision_pthreads.GetMutex(collision_guest_mutex.handle) &&
            collision_pthreads.GetMutex(second_internal_mutex.handle) &&
            collision_pthreads.DestroyMutex(collision_guest_mutex.handle) ==
                KernelStatus::kOk &&
            collision_pthreads.CreateGuestMutex(0).handle ==
                collision_guest_mutex.handle,
        "guest and synthetic mutex handles aliased or leaked their slots");

  KernelRuntime rwlock_collision_runtime;
  auto& rwlock_collision_pthreads = rwlock_collision_runtime.pthreads();
  const auto rwlock_collision_mutex = rwlock_collision_pthreads.CreateMutex(0);
  kajps5::memory::GuestMemory rwlock_collision_memory(
      rwlock_collision_mutex.handle, kPthreadMutexArenaSize +
                                        kPthreadMutexObjectSize);
  Check(rwlock_collision_mutex &&
            rwlock_collision_pthreads.ConfigureGuestMutexArena(
                rwlock_collision_memory, rwlock_collision_mutex.handle) &&
            rwlock_collision_pthreads.CreateGuestRwlock().handle ==
                rwlock_collision_mutex.handle + kPthreadMutexObjectSize,
        "guest rwlock allocation collided with a synthetic mutex");

  KernelRuntime mutex_collision_runtime;
  auto& mutex_collision_pthreads = mutex_collision_runtime.pthreads();
  const auto mutex_collision_base = std::uint64_t{0x0000600600000001};
  kajps5::memory::GuestMemory mutex_collision_memory(
      mutex_collision_base, kPthreadMutexArenaSize + kPthreadMutexObjectSize);
  const auto first_collision_rwlock =
      mutex_collision_pthreads.ConfigureGuestMutexArena(
          mutex_collision_memory, mutex_collision_base)
          ? mutex_collision_pthreads.CreateGuestRwlock()
          : PthreadRwlockCreateResult{KernelStatus::kNoResources, 0};
  const auto skipped_synthetic_mutex = mutex_collision_pthreads.CreateMutex(0);
  Check(first_collision_rwlock &&
            first_collision_rwlock.handle == mutex_collision_base &&
            skipped_synthetic_mutex &&
            skipped_synthetic_mutex.handle == mutex_collision_base + 1,
        "synthetic mutex allocation collided with a guest rwlock");

  // Rwlocks share the bounded guest pthread-object arena with mutexes, retain
  // pointer identity, and hand off a blocked writer through the scheduler.
  KernelRuntime rwlock_runtime;
  auto& rwlock_pthreads = rwlock_runtime.pthreads();
  auto& rwlock_scheduler = rwlock_runtime.scheduler();
  kajps5::memory::GuestMemory rwlock_memory(0x1000,
                                             kPthreadMutexArenaSize + 0x2000);
  Check(rwlock_pthreads.ConfigureGuestMutexArena(rwlock_memory, 0x1000),
        "rwlock guest arena configuration failed");
  const auto rwlock = rwlock_pthreads.CreateGuestRwlock();
  const auto rwlock_mutex = rwlock_pthreads.CreateGuestMutex(0);
  Check(rwlock && rwlock.handle == 0x1000 && rwlock_mutex &&
            rwlock_mutex.handle == 0x1000 + kPthreadMutexObjectSize,
        "rwlock and mutex guest objects collided");
  const auto rw_reader = rwlock_scheduler.CreateThread("rw-reader", 700);
  const auto rw_writer = rwlock_scheduler.CreateThread("rw-writer", 700);
  const auto rw_observer = rwlock_scheduler.CreateThread("rw-observer", 700);
  Check(rw_reader && rw_writer && rw_observer &&
            rwlock_scheduler.SelectNext() == rw_reader.handle &&
            rwlock_pthreads.LockRwlock(rwlock.handle, false, false) ==
                KernelStatus::kOk &&
            rwlock_pthreads.LockRwlock(rwlock.handle, false, false) ==
                KernelStatus::kOk &&
            rwlock_pthreads.GetRwlock(rwlock.handle)->reader_count == 2 &&
            rwlock_scheduler.YieldCurrent() &&
            rwlock_scheduler.SelectNext() == rw_writer.handle &&
            rwlock_pthreads.LockRwlock(rwlock.handle, true, false) ==
                KernelStatus::kWouldBlock &&
            rwlock_pthreads.GetRwlock(rwlock.handle)->waiter_count == 1 &&
            rwlock_scheduler.SelectNext() == rw_observer.handle &&
            rwlock_pthreads.LockRwlock(rwlock.handle, false, true) ==
                KernelStatus::kBusy && rwlock_pthreads.ExitCurrent(0) &&
            rwlock_scheduler.SelectNext() == rw_reader.handle &&
            rwlock_pthreads.UnlockRwlock(rwlock.handle) == KernelStatus::kOk &&
            rwlock_pthreads.GetRwlock(rwlock.handle)->reader_count == 1 &&
            rwlock_pthreads.UnlockRwlock(rwlock.handle) == KernelStatus::kOk &&
            rwlock_pthreads.CanDestroyRwlock(rwlock.handle) ==
                KernelStatus::kBusy && rwlock_pthreads.ExitCurrent(0) &&
            rwlock_scheduler.SelectNext() == rw_writer.handle &&
            rwlock_pthreads.LockRwlock(rwlock.handle, true, false) ==
                KernelStatus::kOk &&
            rwlock_pthreads.UnlockRwlock(rwlock.handle) == KernelStatus::kOk &&
            rwlock_pthreads.DestroyRwlock(rwlock.handle) == KernelStatus::kOk &&
            rwlock_pthreads.CreateGuestRwlock().handle == rwlock.handle,
        "rwlock ownership, blocking, destroy, or arena reuse failed");

  KernelRuntime condition_runtime;
  auto& condition_pthreads = condition_runtime.pthreads();
  auto& condition_scheduler = condition_runtime.scheduler();
  const auto condition = condition_pthreads.CreateCondition();
  const auto condition_mutex = condition_pthreads.CreateMutex(0);
  const auto condition_waiter =
      condition_scheduler.CreateThread("condition-waiter", 700);
  const auto condition_signaler =
      condition_scheduler.CreateThread("condition-signaler", 700);
  Check(condition && condition_mutex && condition_waiter &&
            condition_signaler &&
            condition_scheduler.SelectNext() == condition_waiter.handle,
        "condition scheduler setup failed");
  Check(condition_pthreads.LockMutex(condition_mutex.handle, false) ==
                KernelStatus::kOk &&
            condition_pthreads.WaitCondition(condition.handle,
                                              condition_mutex.handle) ==
                KernelStatus::kWouldBlock,
        "condition wait did not release its mutex and block");
  const auto waiting_condition =
      condition_pthreads.GetCondition(condition.handle);
  const auto released_condition_mutex =
      condition_pthreads.GetMutex(condition_mutex.handle);
  Check(waiting_condition && waiting_condition->waiting_count == 1 &&
            waiting_condition->active_waiter_count == 1 &&
            released_condition_mutex &&
            released_condition_mutex->owner == kInvalidKernelHandle &&
            released_condition_mutex->condition_waiter_count == 1,
        "condition wait state was not recorded");
  Check(condition_pthreads.DestroyCondition(condition.handle) ==
                KernelStatus::kBusy &&
            condition_pthreads.DestroyMutex(condition_mutex.handle) ==
                KernelStatus::kBusy,
        "condition wait resources were destroyed while in use");
  Check(condition_scheduler.SelectNext() == condition_signaler.handle &&
            condition_pthreads.LockMutex(condition_mutex.handle, false) ==
                KernelStatus::kOk &&
            condition_pthreads.SignalCondition(condition.handle, false) ==
                KernelStatus::kOk &&
            condition_scheduler.YieldCurrent() &&
            condition_scheduler.SelectNext() == condition_waiter.handle &&
            condition_pthreads.WaitCondition(condition.handle,
                                              condition_mutex.handle) ==
                KernelStatus::kWouldBlock,
        "condition signal did not release one waiter");
  const auto reacquiring_condition =
      condition_pthreads.GetCondition(condition.handle);
  const auto contended_condition_mutex =
      condition_pthreads.GetMutex(condition_mutex.handle);
  Check(reacquiring_condition && reacquiring_condition->waiting_count == 0 &&
            reacquiring_condition->active_waiter_count == 1 &&
            contended_condition_mutex &&
            contended_condition_mutex->owner == condition_signaler.handle &&
            contended_condition_mutex->waiter_count == 1 &&
            condition_scheduler.SelectNext() == condition_signaler.handle &&
            condition_pthreads.UnlockMutex(condition_mutex.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.ExitCurrent(0),
        "condition waiter did not block while reacquiring its mutex");
  Check(condition_scheduler.SelectNext() == condition_waiter.handle &&
            condition_pthreads.WaitCondition(condition.handle,
                                              condition_mutex.handle) ==
                KernelStatus::kOk,
        "signaled condition waiter did not reacquire its mutex");
  const auto completed_condition =
      condition_pthreads.GetCondition(condition.handle);
  const auto reacquired_condition_mutex =
      condition_pthreads.GetMutex(condition_mutex.handle);
  Check(completed_condition && completed_condition->waiting_count == 0 &&
            completed_condition->active_waiter_count == 0 &&
            reacquired_condition_mutex &&
            reacquired_condition_mutex->owner == condition_waiter.handle &&
            reacquired_condition_mutex->condition_waiter_count == 0,
        "condition waiter completion left stale state");
  Check(condition_pthreads.UnlockMutex(condition_mutex.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.WaitCondition(condition.handle,
                                              condition_mutex.handle) ==
                KernelStatus::kPermissionDenied,
        "condition wait accepted a mutex owned by another thread");
  Check(condition_pthreads.DestroyCondition(condition.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.DestroyMutex(condition_mutex.handle) ==
                KernelStatus::kOk,
        "released condition resources could not be destroyed");

  const auto recursive_attributes =
      condition_pthreads.CreateMutexAttribute();
  Check(recursive_attributes &&
            condition_pthreads.SetMutexAttributeType(
                recursive_attributes.handle, kPthreadMutexRecursive) ==
                KernelStatus::kOk,
        "recursive condition mutex setup failed");
  const auto recursive_condition = condition_pthreads.CreateCondition();
  const auto recursive_condition_mutex =
      condition_pthreads.CreateMutex(recursive_attributes.handle);
  Check(recursive_condition && recursive_condition_mutex &&
            condition_pthreads.LockMutex(recursive_condition_mutex.handle,
                                          false) == KernelStatus::kOk &&
            condition_pthreads.LockMutex(recursive_condition_mutex.handle,
                                          false) == KernelStatus::kOk &&
            condition_pthreads.WaitCondition(
                recursive_condition.handle,
                recursive_condition_mutex.handle) ==
                KernelStatus::kInvalidArgument,
        "condition wait accepted a multiply locked recursive mutex");
  Check(condition_pthreads.UnlockMutex(recursive_condition_mutex.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.UnlockMutex(recursive_condition_mutex.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.DestroyCondition(recursive_condition.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.DestroyMutex(recursive_condition_mutex.handle) ==
                KernelStatus::kOk &&
            condition_pthreads.DestroyMutexAttribute(
                recursive_attributes.handle) == KernelStatus::kOk,
        "recursive condition resources could not be released");

  KernelRuntime broadcast_runtime;
  auto& broadcast_pthreads = broadcast_runtime.pthreads();
  auto& broadcast_scheduler = broadcast_runtime.scheduler();
  const auto broadcast_condition = broadcast_pthreads.CreateCondition();
  const auto broadcast_mutex = broadcast_pthreads.CreateMutex(0);
  const auto first_broadcast_waiter =
      broadcast_scheduler.CreateThread("first-broadcast-waiter", 700);
  const auto second_broadcast_waiter =
      broadcast_scheduler.CreateThread("second-broadcast-waiter", 700);
  const auto broadcaster =
      broadcast_scheduler.CreateThread("broadcaster", 700);
  Check(broadcast_condition && broadcast_mutex && first_broadcast_waiter &&
            second_broadcast_waiter && broadcaster &&
            broadcast_scheduler.SelectNext() == first_broadcast_waiter.handle,
        "condition broadcast scheduler setup failed");
  Check(broadcast_pthreads.LockMutex(broadcast_mutex.handle, false) ==
                KernelStatus::kOk &&
            broadcast_pthreads.WaitCondition(broadcast_condition.handle,
                                              broadcast_mutex.handle) ==
                KernelStatus::kWouldBlock &&
            broadcast_scheduler.SelectNext() == second_broadcast_waiter.handle &&
            broadcast_pthreads.LockMutex(broadcast_mutex.handle, false) ==
                KernelStatus::kOk &&
            broadcast_pthreads.WaitCondition(broadcast_condition.handle,
                                              broadcast_mutex.handle) ==
                KernelStatus::kWouldBlock,
        "condition broadcast waiters did not block");
  Check(broadcast_scheduler.SelectNext() == broadcaster.handle &&
            broadcast_pthreads.SignalCondition(broadcast_condition.handle,
                                                true) == KernelStatus::kOk &&
            broadcast_pthreads.ExitCurrent(0),
        "condition broadcast did not wake its waiters");
  Check(broadcast_scheduler.SelectNext() == first_broadcast_waiter.handle &&
            broadcast_pthreads.WaitCondition(broadcast_condition.handle,
                                              broadcast_mutex.handle) ==
                KernelStatus::kOk &&
            broadcast_pthreads.UnlockMutex(broadcast_mutex.handle) ==
                KernelStatus::kOk &&
            broadcast_pthreads.ExitCurrent(0),
        "first broadcast waiter did not complete");
  Check(broadcast_scheduler.SelectNext() == second_broadcast_waiter.handle &&
            broadcast_pthreads.WaitCondition(broadcast_condition.handle,
                                              broadcast_mutex.handle) ==
                KernelStatus::kOk &&
            broadcast_pthreads.UnlockMutex(broadcast_mutex.handle) ==
                KernelStatus::kOk,
        "second broadcast waiter did not complete");
  const auto broadcast_state =
      broadcast_pthreads.GetCondition(broadcast_condition.handle);
  Check(broadcast_state && broadcast_state->waiting_count == 0 &&
            broadcast_state->active_waiter_count == 0 &&
            broadcast_pthreads.DestroyCondition(broadcast_condition.handle) ==
                KernelStatus::kOk &&
            broadcast_pthreads.DestroyMutex(broadcast_mutex.handle) ==
                KernelStatus::kOk,
        "condition broadcast left stale waiters");

  auto timed_source = std::make_unique<PthreadClockSource>();
  auto* timed_source_view = timed_source.get();
  timed_source_view->realtime_nanoseconds = 100'000'000'000;
  timed_source_view->monotonic_nanoseconds = 1'000'000;
  KernelRuntime timed_runtime(std::move(timed_source));
  auto& timed_pthreads = timed_runtime.pthreads();
  auto& timed_scheduler = timed_runtime.scheduler();
  const auto timed_condition = timed_pthreads.CreateCondition();
  const auto timed_mutex = timed_pthreads.CreateMutex(0);
  const auto timed_waiter = timed_scheduler.CreateThread("timed-waiter", 700);
  Check(timed_condition && timed_mutex && timed_waiter &&
            timed_scheduler.SelectNext() == timed_waiter.handle &&
            timed_pthreads.LockMutex(timed_mutex.handle, false) ==
                KernelStatus::kOk &&
            timed_pthreads.WaitConditionFor(timed_condition.handle,
                                            timed_mutex.handle, 50) ==
                KernelStatus::kWouldBlock,
        "relative condition timeout setup failed");
  timed_source_view->monotonic_nanoseconds = 1'049'999;
  Check(!timed_scheduler.SelectNext(),
        "relative condition wait woke before its deadline");
  timed_source_view->monotonic_nanoseconds = 1'050'000;
  Check(timed_scheduler.SelectNext() == timed_waiter.handle &&
            timed_pthreads.WaitConditionFor(timed_condition.handle,
                                            timed_mutex.handle, 999) ==
                KernelStatus::kTimedOut,
        "relative condition wait did not time out at its deadline");
  const auto relative_timeout_condition =
      timed_pthreads.GetCondition(timed_condition.handle);
  const auto relative_timeout_mutex =
      timed_pthreads.GetMutex(timed_mutex.handle);
  Check(relative_timeout_condition &&
            relative_timeout_condition->waiting_count == 0 &&
            relative_timeout_condition->active_waiter_count == 0 &&
            relative_timeout_mutex &&
            relative_timeout_mutex->owner == timed_waiter.handle &&
            relative_timeout_mutex->condition_waiter_count == 0,
        "timed-out condition wait did not reacquire its mutex");

  const auto absolute_signaler =
      timed_scheduler.CreateThread("absolute-signaler", 700);
  Check(absolute_signaler &&
            timed_pthreads.UnlockMutex(timed_mutex.handle) ==
                KernelStatus::kOk &&
            timed_pthreads.LockMutex(timed_mutex.handle, false) ==
                KernelStatus::kOk &&
            timed_pthreads.WaitConditionUntilRealtime(
                timed_condition.handle, timed_mutex.handle,
                {100, 1'000'000}) == KernelStatus::kWouldBlock &&
            timed_scheduler.SelectNext() == absolute_signaler.handle &&
            timed_pthreads.SignalCondition(timed_condition.handle, false) ==
                KernelStatus::kOk &&
            timed_pthreads.ExitCurrent(0) &&
            timed_scheduler.SelectNext() == timed_waiter.handle &&
            timed_pthreads.WaitConditionUntilRealtime(
                timed_condition.handle, timed_mutex.handle,
                {100, 1'000'000}) == KernelStatus::kOk,
        "signal did not win before an absolute condition deadline");
  Check(timed_pthreads.WaitConditionUntilRealtime(
            timed_condition.handle, timed_mutex.handle, {-1, 0}) ==
            KernelStatus::kInvalidArgument,
        "condition wait accepted a negative realtime deadline");

  const auto late_signaler =
      timed_scheduler.CreateThread("late-signaler", 700);
  Check(late_signaler &&
            timed_pthreads.UnlockMutex(timed_mutex.handle) ==
                KernelStatus::kOk &&
            timed_pthreads.LockMutex(timed_mutex.handle, false) ==
                KernelStatus::kOk &&
            timed_pthreads.WaitConditionFor(timed_condition.handle,
                                            timed_mutex.handle, 10) ==
                KernelStatus::kWouldBlock &&
            timed_scheduler.SelectNext() == late_signaler.handle,
        "late condition signal setup failed");
  timed_source_view->monotonic_nanoseconds += 10'000;
  Check(timed_pthreads.SignalCondition(timed_condition.handle, false) ==
                KernelStatus::kOk &&
            timed_pthreads.ExitCurrent(0) &&
            timed_scheduler.SelectNext() == timed_waiter.handle &&
            timed_pthreads.WaitConditionFor(timed_condition.handle,
                                            timed_mutex.handle, 10) ==
                KernelStatus::kTimedOut,
        "a late condition signal overrode an expired deadline");
  Check(timed_pthreads.UnlockMutex(timed_mutex.handle) == KernelStatus::kOk &&
            timed_pthreads.DestroyCondition(timed_condition.handle) ==
                KernelStatus::kOk &&
            timed_pthreads.DestroyMutex(timed_mutex.handle) ==
                KernelStatus::kOk,
        "timed condition resources could not be destroyed");

  std::cout << "kernel pthread tests passed\n";
  return 0;
}
