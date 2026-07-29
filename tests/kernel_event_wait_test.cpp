// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>

#include "kernel/event_flag.h"
#include "kernel/guest_scheduler.h"
#include "kernel/runtime.h"

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
  auto &events = runtime.event_flags();
  auto &scheduler = runtime.scheduler();

  const auto event = events.Create("shared", kEventFlagMulti, 0);
  const auto all_waiter = scheduler.CreateThread("all", 10);
  const auto any_waiter = scheduler.CreateThread("any", 20);
  Check(event && all_waiter && any_waiter, "wait fixture creation failed");
  Check(events.Wait(event.handle, 1, kEventFlagWaitAny).status ==
            KernelStatus::kBusy,
        "wait without a running thread changed scheduler state");

  Check(scheduler.SelectNext() == all_waiter.handle,
        "all-bit waiter was not selected");
  auto wait = events.Wait(event.handle, 0x03, kEventFlagWaitAll);
  Check(wait.status == KernelStatus::kWouldBlock && wait.observed_pattern == 0,
        "unsatisfied all-bit wait did not block");

  Check(scheduler.SelectNext() == any_waiter.handle,
        "any-bit waiter was not selected");
  wait = events.Wait(event.handle, 0x01,
                     kEventFlagWaitAny | kEventFlagClearPattern);
  Check(wait.status == KernelStatus::kWouldBlock,
        "unsatisfied any-bit wait did not block");

  Check(events.Set(event.handle, 0x01) == KernelStatus::kOk,
        "event set failed");
  Check(scheduler.SelectNext() == all_waiter.handle,
        "set did not wake waiters in handle order");
  wait = events.Wait(event.handle, 0x03, kEventFlagWaitAll);
  Check(wait.status == KernelStatus::kWouldBlock &&
            wait.observed_pattern == 0x01,
        "spurious wake did not recheck and block");

  Check(scheduler.SelectNext() == any_waiter.handle,
        "satisfied waiter was not selected");
  wait = events.Wait(event.handle, 0x01,
                     kEventFlagWaitAny | kEventFlagClearPattern);
  Check(wait && wait.observed_pattern == 0x01,
        "satisfied wait returned the wrong pattern");
  Check(scheduler.ExitCurrent(0), "satisfied waiter did not exit");

  Check(events.Set(event.handle, 0x03) == KernelStatus::kOk,
        "second event set failed");
  Check(scheduler.SelectNext() == all_waiter.handle,
        "remaining waiter did not wake");
  wait = events.Wait(event.handle, 0x03, kEventFlagWaitAll);
  Check(wait && wait.observed_pattern == 0x03,
        "all-bit waiter did not complete");
  Check(scheduler.ExitCurrent(0), "all-bit waiter did not exit");

  const auto deleted_event = events.Create("deleted", 0, 0);
  const auto deleted_waiter = scheduler.CreateThread("deleted", 0);
  Check(deleted_event && deleted_waiter, "delete fixture creation failed");
  Check(scheduler.SelectNext() == deleted_waiter.handle,
        "delete waiter was not selected");
  Check(events.Wait(deleted_event.handle, 1, kEventFlagWaitAny).status ==
            KernelStatus::kWouldBlock,
        "delete waiter did not block");
  Check(events.Delete(deleted_event.handle) == KernelStatus::kOk,
        "waited event was not deleted");
  Check(scheduler.SelectNext() == deleted_waiter.handle,
        "delete did not release its waiter");
  Check(events.Wait(deleted_event.handle, 1, kEventFlagWaitAny).status ==
            KernelStatus::kNotFound,
        "released waiter did not observe event deletion");

  std::cout << "kernel event wait tests passed\n";
  return 0;
}
