// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <string>

#include "kernel/guest_scheduler.h"
#include "kernel/runtime.h"
#include "kernel/semaphore.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

} // namespace

int main() {
  using namespace kajps5::kernel;

  KernelRuntime runtime;
  auto &scheduler = runtime.scheduler();
  auto &semaphores = runtime.semaphores();

  Check(semaphores.Create(std::string(129, 'x'), 0, 0, 1).status ==
            KernelStatus::kInvalidArgument,
        "long semaphore name was accepted");
  Check(semaphores.Create("bad-attr", 3, 0, 1).status ==
            KernelStatus::kInvalidArgument,
        "invalid semaphore attributes were accepted");
  Check(semaphores.Create("bad-count", 0, 2, 1).status ==
            KernelStatus::kInvalidArgument,
        "invalid semaphore counts were accepted");

  const auto semaphore = semaphores.Create("work", kSemaphoreThreadFifo, 1, 3);
  Check(static_cast<bool>(semaphore), "valid semaphore creation failed");
  Check(runtime.handles().Find(semaphore.handle,
                               KernelObjectType::kSemaphore) != nullptr,
        "typed semaphore lookup failed");
  Check(runtime.handles().Find(semaphore.handle,
                               KernelObjectType::kEventFlag) == nullptr,
        "semaphore handle was accepted as an event");

  auto poll = semaphores.Poll(semaphore.handle, 1);
  Check(poll && poll.remaining_count == 0,
        "available semaphore count was not acquired");
  poll = semaphores.Poll(semaphore.handle, 1);
  Check(poll.status == KernelStatus::kBusy && poll.remaining_count == 0,
        "empty semaphore poll did not report busy");
  Check(semaphores.Poll(semaphore.handle, 0).status ==
            KernelStatus::kInvalidArgument,
        "zero semaphore acquisition was accepted");

  const auto first = scheduler.CreateThread("first", 10);
  const auto second = scheduler.CreateThread("second", 20);
  Check(first && second, "waiter creation failed");
  Check(scheduler.SelectNext() == first.handle,
        "first waiter was not selected");
  auto wait = semaphores.Wait(semaphore.handle, 2);
  Check(wait.status == KernelStatus::kWouldBlock,
        "first semaphore waiter did not block");
  Check(scheduler.SelectNext() == second.handle,
        "second waiter was not selected");
  wait = semaphores.Wait(semaphore.handle, 1);
  Check(wait.status == KernelStatus::kWouldBlock,
        "second semaphore waiter did not block");

  Check(semaphores.Signal(semaphore.handle, 1) == KernelStatus::kOk,
        "semaphore signal failed");
  Check(scheduler.SelectNext() == first.handle,
        "signal did not wake waiters in handle order");
  wait = semaphores.Wait(semaphore.handle, 2);
  Check(wait.status == KernelStatus::kWouldBlock && wait.remaining_count == 1,
        "insufficient wake did not recheck and block");
  Check(scheduler.SelectNext() == second.handle,
        "second waiter was not selected after recheck");
  wait = semaphores.Wait(semaphore.handle, 1);
  Check(wait && wait.remaining_count == 0,
        "second waiter did not acquire the available count");
  Check(scheduler.ExitCurrent(0), "second waiter did not exit");

  Check(semaphores.Signal(semaphore.handle, 2) == KernelStatus::kOk,
        "second semaphore signal failed");
  Check(scheduler.SelectNext() == first.handle,
        "first waiter did not wake again");
  wait = semaphores.Wait(semaphore.handle, 2);
  Check(wait && wait.remaining_count == 0,
        "first waiter did not acquire both counts");
  Check(scheduler.ExitCurrent(0), "first waiter did not exit");

  Check(semaphores.Signal(semaphore.handle, 4) ==
            KernelStatus::kInvalidArgument,
        "semaphore count exceeded its maximum");
  const auto count = semaphores.GetCount(semaphore.handle);
  Check(count && count.remaining_count == 0,
        "failed signal changed the semaphore count");

  const auto deleted = semaphores.Create("deleted", 0, 0, 1);
  const auto deleted_waiter = scheduler.CreateThread("deleted", 0);
  Check(deleted && deleted_waiter, "delete fixture creation failed");
  Check(scheduler.SelectNext() == deleted_waiter.handle,
        "delete waiter was not selected");
  Check(semaphores.Wait(deleted.handle, 1).status == KernelStatus::kWouldBlock,
        "delete waiter did not block");
  Check(semaphores.Delete(deleted.handle) == KernelStatus::kOk,
        "waited semaphore was not deleted");
  Check(scheduler.SelectNext() == deleted_waiter.handle,
        "semaphore delete did not release its waiter");
  Check(semaphores.Wait(deleted.handle, 1).status == KernelStatus::kNotFound,
        "released waiter did not observe semaphore deletion");

  std::cout << "kernel semaphore tests passed\n";
  return 0;
}
