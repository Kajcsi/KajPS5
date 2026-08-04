// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "kernel/event_queue.h"
#include "kernel/object.h"
#include "kernel/runtime.h"

namespace {

class TestClockSource final : public kajps5::kernel::KernelClockSource {
public:
  [[nodiscard]] std::int64_t RealtimeNanoseconds() const override {
    return static_cast<std::int64_t>(monotonic_nanoseconds_);
  }

  [[nodiscard]] std::uint64_t MonotonicNanoseconds() const override {
    return monotonic_nanoseconds_;
  }

  void Set(std::uint64_t value) noexcept { monotonic_nanoseconds_ = value; }

private:
  std::uint64_t monotonic_nanoseconds_ = 0;
};

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

  Check(queues.AddGraphicsEvent(created.handle, 0x20, 0xdeadbeefU) ==
                KernelStatus::kOk &&
            queues.TriggerGraphicsEvents(7) == 1,
        "graphics event filter trigger failed");
  polled = queues.Poll(created.handle, 2);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 0x20 &&
            polled.events[0].filter == kEventFilterGraphics &&
            polled.events[0].flags == 0 &&
            polled.events[0].fflags == 1 &&
            polled.events[0].data == 7 &&
            polled.events[0].user_data == 0xdeadbeefU &&
            queues.Poll(created.handle, 1).status == KernelStatus::kBusy &&
            queues.DeleteGraphicsEvent(created.handle, 0x20) ==
                KernelStatus::kOk,
        "graphics event payload or reset behavior is incorrect");

  Check(queues.AddVideoOutEvent(created.handle, 0x6, 0xfeedfaceU) ==
                KernelStatus::kOk &&
            queues.TriggerVideoOutEvents(0x1234U) == 1,
        "VideoOut event filter trigger failed");
  Check(queues.DeleteVideoOutEvent(created.handle, 0x6) == KernelStatus::kOk &&
            queues.AddVideoOutEvent(created.handle, 0x6, 0xfeedface1U) ==
                KernelStatus::kOk &&
            queues.Poll(created.handle, 1).status == KernelStatus::kBusy &&
            queues.TriggerVideoOutEvents(0x1235U) == 1,
        "stale VideoOut generation survived re-registration");
  polled = queues.Poll(created.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 0x6 &&
            polled.events[0].filter == kEventFilterVideoOut &&
            polled.events[0].fflags == 1 && polled.events[0].data == 0x1235U &&
            polled.events[0].user_data == 0xfeedface1U &&
            queues.DeleteVideoOutEvent(created.handle, 0x6) == KernelStatus::kOk,
        "VideoOut event payload or generation registration is incorrect");

  const auto generation_queue = queues.Create("generation");
  const auto generation_waiter =
      runtime.scheduler().CreateThread("generation-waiter", 0);
  Check(generation_queue && generation_waiter &&
            queues.AddGraphicsEvent(generation_queue.handle, 0x30, 0x111U) ==
                KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == generation_waiter.handle &&
            queues.Wait(generation_queue.handle, 1).status ==
                KernelStatus::kWouldBlock &&
            queues.TriggerGraphicsEvents(8) == 1 &&
            queues.DeleteGraphicsEvent(generation_queue.handle, 0x30) ==
                KernelStatus::kOk &&
            queues.AddGraphicsEvent(generation_queue.handle, 0x30, 0x222U) ==
                KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == generation_waiter.handle &&
            queues.Wait(generation_queue.handle, 1).status ==
                KernelStatus::kWouldBlock,
        "old graphics event survived listener re-registration");
  Check(queues.TriggerGraphicsEvents(9) == 1 &&
            runtime.scheduler().SelectNext() == generation_waiter.handle,
        "new graphics event did not wake its waiter");
  polled = queues.Wait(generation_queue.handle, 1);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 0x30 &&
            polled.events[0].data == 9 &&
            polled.events[0].user_data == 0x222U &&
            runtime.scheduler().ExitCurrent(0) &&
            queues.Delete(generation_queue.handle) == KernelStatus::kOk,
        "re-registered graphics listener received the wrong event");

  Check(queues.Delete(created.handle) == KernelStatus::kOk &&
            queues.Delete(created.handle) == KernelStatus::kNotFound &&
            queues.AddUserEvent(created.handle, 1, false) ==
                KernelStatus::kNotFound,
        "stale event queue handle remained valid");
}

void TestTimedEventQueueWait() {
  using namespace kajps5::kernel;

  auto clock = std::make_unique<TestClockSource>();
  auto *const clock_view = clock.get();
  KernelRuntime runtime(std::move(clock));
  auto &queues = runtime.event_queues();
  auto &scheduler = runtime.scheduler();
  const auto queue = queues.Create("timed");
  Check(queue && queues.AddUserEvent(queue.handle, 1, true) ==
                     KernelStatus::kOk &&
            queues.Wait(queue.handle, 1, 0).status ==
                KernelStatus::kTimedOut,
        "zero-time event queue poll did not time out");

  clock_view->Set(1'000'000);
  const auto waiter = scheduler.CreateThread("timed-waiter", 0);
  Check(waiter && scheduler.SelectNext() == waiter.handle &&
            queues.Wait(queue.handle, 1, 25).status ==
                KernelStatus::kWouldBlock,
        "timed event queue wait did not block");
  clock_view->Set(1'025'000);
  Check(queues.TriggerUserEvent(queue.handle, 1, 0x55) ==
                KernelStatus::kOk &&
            scheduler.SelectNext() == waiter.handle &&
            queues.Wait(queue.handle, 1, 25).status ==
                KernelStatus::kTimedOut,
        "expired event queue wait accepted a late event");
  const auto late_event = queues.Poll(queue.handle, 1);
  Check(late_event && late_event.events.size() == 1 &&
            late_event.events[0].user_data == 0x55 &&
            scheduler.ExitCurrent(0),
        "late event was lost after a timed wait");
}

} // namespace

int main() {
  TestEventQueueService();
  TestTimedEventQueueWait();
  std::cout << "kernel event queue tests passed\n";
  return 0;
}
