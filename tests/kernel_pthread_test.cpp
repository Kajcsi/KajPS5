// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>

#include "kernel/pthread.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

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

  std::cout << "kernel pthread tests passed\n";
  return 0;
}
