// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <string>

#include "kernel/event_flag.h"
#include "kernel/guest_scheduler.h"
#include "kernel/object.h"
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

  const auto event = events.Create("event", 0, 0);
  const auto first = scheduler.CreateThread("first", 10);
  const auto second = scheduler.CreateThread("second", 20);
  Check(event && event.handle == 1, "event did not use the shared table");
  Check(first && first.handle == 2, "first thread handle was not shared");
  Check(second && second.handle == 3, "second thread handle was not shared");
  Check(runtime.handles().size() == 3, "shared handle count is incorrect");
  Check(events.Set(first.handle, 1) == KernelStatus::kNotFound,
        "thread handle was accepted as an event");

  Check(!scheduler.BlockCurrent("wait"),
        "scheduler blocked without a running thread");
  Check(!scheduler.BlockCurrent(""), "scheduler accepted an empty wait key");

  const auto selected_first = scheduler.SelectNext();
  Check(selected_first && *selected_first == first.handle,
        "ready queue did not select the first thread");
  Check(!scheduler.SelectNext(), "scheduler selected two running threads");
  Check(scheduler.BlockCurrent("event:1"), "first thread did not block");

  const auto selected_second = scheduler.SelectNext();
  Check(selected_second && *selected_second == second.handle,
        "ready queue did not select the second thread");
  Check(scheduler.BlockCurrent("event:1"), "second thread did not block");
  Check(!scheduler.current_thread(), "blocked thread remained current");

  Check(scheduler.WakeBlockedThreads("", 1) == 0,
        "empty wake key changed scheduler state");
  Check(scheduler.WakeBlockedThreads("event:1", 1) == 1,
        "bounded wake did not wake one thread");
  auto first_snapshot = scheduler.Snapshot(first.handle);
  auto second_snapshot = scheduler.Snapshot(second.handle);
  Check(first_snapshot && first_snapshot->state == GuestThreadState::kReady &&
            first_snapshot->wait_key.empty(),
        "first thread did not become ready");
  Check(second_snapshot &&
            second_snapshot->state == GuestThreadState::kBlocked &&
            second_snapshot->wait_key == "event:1",
        "bounded wake changed the second thread");

  Check(scheduler.SelectNext() == first.handle,
        "woken thread was not selected");
  Check(scheduler.YieldCurrent(), "running thread did not yield");
  Check(scheduler.WakeBlockedThreads("event:1") == 1,
        "unbounded wake did not wake the remaining thread");
  Check(scheduler.SelectNext() == first.handle,
        "yield did not preserve FIFO ordering");
  Check(scheduler.JoinThread(999).status == KernelStatus::kNotFound,
        "unknown thread join returned the wrong status");
  Check(scheduler.JoinThread(first.handle).status ==
            KernelStatus::kInvalidArgument,
        "self join was accepted");
  Check(scheduler.JoinThread(second.handle).status ==
            KernelStatus::kWouldBlock,
        "join did not block for a live thread");
  first_snapshot = scheduler.Snapshot(first.handle);
  Check(first_snapshot &&
            first_snapshot->state == GuestThreadState::kBlocked &&
            !first_snapshot->wait_key.empty(),
        "join wait state was not preserved");
  Check(scheduler.JoinThread(second.handle).status == KernelStatus::kBusy,
        "join without a running caller returned the wrong status");

  Check(scheduler.SelectNext() == second.handle,
        "remaining ready thread was not selected");
  Check(scheduler.ExitCurrent(0x5678), "second thread did not exit");
  first_snapshot = scheduler.Snapshot(first.handle);
  Check(first_snapshot && first_snapshot->state == GuestThreadState::kReady,
        "thread exit did not wake its joiner");
  Check(scheduler.SelectNext() == first.handle,
        "woken joiner was not selected");
  const auto joined = scheduler.JoinThread(second.handle);
  Check(joined && joined.exit_value == 0x5678,
        "join did not return the thread exit value");
  Check(scheduler.ExitCurrent(0x1234), "first thread did not exit");
  first_snapshot = scheduler.Snapshot(first.handle);
  Check(first_snapshot && first_snapshot->state == GuestThreadState::kExited &&
            first_snapshot->exit_value == 0x1234,
        "thread exit state was not preserved");
  Check(!scheduler.SelectNext(), "scheduler selected an exited thread");
  Check(!scheduler.YieldCurrent(),
        "scheduler yielded without a running thread");
  Check(scheduler.SnapshotAll().size() == 2,
        "thread snapshot inventory is incomplete");

  KernelRuntime join_runtime;
  auto &join_scheduler = join_runtime.scheduler();
  const auto joiner_one = join_scheduler.CreateThread("joiner-one", 0);
  const auto joiner_two = join_scheduler.CreateThread("joiner-two", 0);
  const auto target = join_scheduler.CreateThread("target", 0);
  Check(joiner_one && joiner_two && target,
        "multi-join thread creation failed");
  Check(join_scheduler.SelectNext() == joiner_one.handle,
        "first joiner was not selected");
  Check(join_scheduler.JoinThread(target.handle).status ==
            KernelStatus::kWouldBlock,
        "first joiner did not block");
  Check(join_scheduler.SelectNext() == joiner_two.handle,
        "second joiner was not selected");
  Check(join_scheduler.JoinThread(target.handle).status ==
            KernelStatus::kWouldBlock,
        "second joiner did not block");
  Check(join_scheduler.SelectNext() == target.handle,
        "join target was not selected");
  Check(join_scheduler.ExitCurrent(7), "join target did not exit");
  Check(join_scheduler.Snapshot(joiner_one.handle)->state ==
            GuestThreadState::kReady &&
            join_scheduler.Snapshot(joiner_two.handle)->state ==
                GuestThreadState::kReady,
        "thread exit did not wake every joiner");

  Check(scheduler.CreateThread(std::string(32, 'x'), 0).status ==
            KernelStatus::kInvalidArgument,
        "long thread name was accepted");

  const auto disposable =
      scheduler.CreateThread("disposable", 30, 0x400000, 0xbeef);
  const auto disposable_snapshot = scheduler.Snapshot(disposable.handle);
  Check(disposable && disposable_snapshot &&
            disposable_snapshot->entry_address == 0x400000 &&
            disposable_snapshot->argument == 0xbeef,
        "thread start metadata was not preserved");
  Check(scheduler.DiscardReadyThread(disposable.handle) &&
            !scheduler.Snapshot(disposable.handle) &&
            runtime.handles().size() == 3,
        "ready thread rollback did not release its shared handle");

  std::cout << "kernel scheduler tests passed\n";
  return 0;
}
