// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <string>

#include "kernel/event_queue.h"
#include "kernel/object.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestEventQueueService() {
  using namespace kajps5::kernel;

  KernelRuntime runtime;
  auto &queues = runtime.event_queues();
  Check(queues.Create(std::string(kMaximumEventQueueNameLength + 1, 'q'))
            .status == KernelStatus::kInvalidArgument,
        "long event queue name was accepted");

  const auto created = queues.Create("main");
  Check(created &&
            runtime.handles().Find(created.handle,
                                   KernelObjectType::kEventQueue) != nullptr,
        "typed event queue creation failed");
  Check(queues.Poll(created.handle, 0).status ==
            KernelStatus::kInvalidArgument,
        "zero-capacity event poll was accepted");
  Check(queues.Poll(created.handle, 1).status == KernelStatus::kBusy,
        "empty event queue did not report busy");

  Check(queues.AddUserEvent(created.handle, 7, true) == KernelStatus::kOk &&
            queues.AddUserEvent(created.handle, 7, true) == KernelStatus::kOk,
        "idempotent edge registration failed");
  Check(queues.TriggerUserEvent(created.handle, 7, 0x11) ==
            KernelStatus::kOk &&
            queues.TriggerUserEvent(created.handle, 7, 0x22) ==
                KernelStatus::kOk,
        "user event trigger failed");
  auto polled = queues.Poll(created.handle, 2);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 7 &&
            polled.events[0].filter == kEventFilterUser &&
            polled.events[0].flags == (kEventAdd | kEventClear) &&
            polled.events[0].fflags == 0 &&
            polled.events[0].data == 0 &&
            polled.events[0].user_data == 0x22,
        "duplicate edge triggers did not coalesce deterministically");

  Check(queues.AddUserEvent(created.handle, 2, false) == KernelStatus::kOk &&
            queues.AddUserEvent(created.handle, 3, true) == KernelStatus::kOk &&
            queues.TriggerUserEvent(created.handle, 3, 0x33) ==
                KernelStatus::kOk &&
            queues.TriggerUserEvent(created.handle, 2, 0x22) ==
                KernelStatus::kOk,
        "ordered event fixture setup failed");
  polled = queues.Poll(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 3,
        "event poll did not preserve trigger order or capacity");
  polled = queues.Poll(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 2 &&
            polled.events[0].flags == kEventAdd,
        "second ordered event was not retained");
  polled = queues.Poll(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 2 &&
            queues.DeleteUserEvent(created.handle, 2) == KernelStatus::kOk &&
            queues.Poll(created.handle, 1).status == KernelStatus::kBusy,
        "level event did not persist until its registration was deleted");

  Check(queues.AddUserEvent(created.handle, 9, true) == KernelStatus::kOk &&
            queues.TriggerUserEvent(created.handle, 9, 0x99) ==
                KernelStatus::kOk &&
            queues.DeleteUserEvent(created.handle, 9) == KernelStatus::kOk &&
            queues.Poll(created.handle, 1).status == KernelStatus::kBusy,
        "deleted registration left a pending user event");
  Check(queues.DeleteUserEvent(created.handle, 9) ==
            KernelStatus::kNoSuchEntry &&
            queues.TriggerUserEvent(created.handle, 99, 0) ==
                KernelStatus::kNoSuchEntry,
        "missing registration returned the wrong status");

  const auto thread = runtime.scheduler().CreateThread("waiter", 0);
  Check(thread && runtime.scheduler().SelectNext() == thread.handle,
        "event queue waiter setup failed");
  const auto waiting = queues.Wait(created.handle, 1);
  Check(waiting.status == KernelStatus::kWouldBlock &&
            !runtime.scheduler().current_thread(),
        "empty event queue did not block the current thread");
  Check(queues.TriggerUserEvent(created.handle, 7, 0x77) ==
            KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == thread.handle,
        "user event did not wake its blocked thread");
  polled = queues.Wait(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].data == 0 &&
            polled.events[0].user_data == 0x77,
        "woken event queue waiter did not receive its event");
  Check(runtime.scheduler().ExitCurrent(0),
        "event queue waiter exit failed");

  const auto first_waiter = runtime.scheduler().CreateThread("first", 0);
  const auto second_waiter = runtime.scheduler().CreateThread("second", 0);
  Check(first_waiter && second_waiter &&
            runtime.scheduler().SelectNext() == first_waiter.handle &&
            queues.Wait(created.handle, 1).status ==
                KernelStatus::kWouldBlock &&
            runtime.scheduler().SelectNext() == second_waiter.handle &&
            queues.Wait(created.handle, 1).status ==
                KernelStatus::kWouldBlock,
        "multi-waiter fixture setup failed");
  Check(queues.TriggerUserEvent(created.handle, 7, 0x81) ==
            KernelStatus::kOk &&
            runtime.scheduler().Snapshot(first_waiter.handle)->state ==
                GuestThreadState::kReady &&
            runtime.scheduler().Snapshot(second_waiter.handle)->state ==
                GuestThreadState::kBlocked &&
            runtime.scheduler().SelectNext() == first_waiter.handle,
        "one edge woke more than one waiter");
  polled = queues.Wait(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].user_data == 0x81 &&
            runtime.scheduler().ExitCurrent(0),
        "first waiter did not receive its reserved edge");
  Check(queues.TriggerUserEvent(created.handle, 7, 0x82) ==
            KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == second_waiter.handle,
        "second edge did not wake the remaining waiter");
  polled = queues.Wait(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].user_data == 0x82 &&
            runtime.scheduler().ExitCurrent(0),
        "second waiter did not receive its reserved edge");

  const auto first_level_waiter =
      runtime.scheduler().CreateThread("level-first", 0);
  const auto second_level_waiter =
      runtime.scheduler().CreateThread("level-second", 0);
  Check(first_level_waiter && second_level_waiter &&
            queues.AddUserEvent(created.handle, 12, false) ==
                KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == first_level_waiter.handle &&
            queues.Wait(created.handle, 1).status ==
                KernelStatus::kWouldBlock &&
            runtime.scheduler().SelectNext() == second_level_waiter.handle &&
            queues.Wait(created.handle, 1).status ==
                KernelStatus::kWouldBlock,
        "level-waiter fixture setup failed");
  Check(queues.TriggerUserEvent(created.handle, 12, 0x91) ==
            KernelStatus::kOk &&
            runtime.scheduler().Snapshot(first_level_waiter.handle)->state ==
                GuestThreadState::kReady &&
            runtime.scheduler().Snapshot(second_level_waiter.handle)->state ==
                GuestThreadState::kReady,
        "level event did not wake every waiter");
  Check(runtime.scheduler().SelectNext() == first_level_waiter.handle,
        "first level waiter was not selected");
  polled = queues.Wait(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].user_data == 0x91 &&
            runtime.scheduler().ExitCurrent(0) &&
            runtime.scheduler().SelectNext() == second_level_waiter.handle,
        "first level waiter did not receive its reservation");
  polled = queues.Wait(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].user_data == 0x91 &&
            runtime.scheduler().ExitCurrent(0) &&
            queues.DeleteUserEvent(created.handle, 12) == KernelStatus::kOk,
        "second level waiter did not receive its reservation");

  const auto delete_queue = queues.Create("delete-wake");
  const auto delete_waiter = runtime.scheduler().CreateThread("delete", 0);
  Check(delete_queue && delete_waiter &&
            runtime.scheduler().SelectNext() == delete_waiter.handle &&
            queues.Wait(delete_queue.handle, 1).status ==
                KernelStatus::kWouldBlock,
        "delete-wake fixture setup failed");
  Check(queues.Delete(delete_queue.handle) == KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == delete_waiter.handle,
        "event queue deletion did not wake its blocked thread");
  Check(queues.Wait(delete_queue.handle, 1).status ==
            KernelStatus::kPermissionDenied,
        "deleted event queue did not complete its existing waiter");
  Check(runtime.scheduler().ExitCurrent(0),
        "delete-woken thread exit failed");
  Check(queues.Wait(delete_queue.handle, 1).status == KernelStatus::kNotFound,
        "deleted event queue remained valid for a new wait");

  Check(queues.Delete(created.handle) == KernelStatus::kOk &&
            queues.Delete(created.handle) == KernelStatus::kNotFound &&
            queues.AddUserEvent(created.handle, 1, false) ==
                KernelStatus::kNotFound,
        "stale event queue handle remained valid");
}

} // namespace

int main() {
  TestEventQueueService();
  std::cout << "kernel event queue tests passed\n";
  return 0;
}
