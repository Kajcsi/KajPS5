// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "kernel/event_flag.h"
#include "kernel/handle_table.h"
#include "kernel/object.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

class TimerStub final : public kajps5::kernel::KernelObject {
public:
  TimerStub() : KernelObject(kajps5::kernel::KernelObjectType::kTimer) {}
};

void TestHandleTable() {
  using namespace kajps5::kernel;

  HandleTable handles;
  Check(!handles.Insert({}), "null object received a handle");

  auto event = std::make_shared<EventFlag>("event", 0, 0);
  const auto event_handle = handles.Insert(event);
  const auto timer_handle = handles.Insert(std::make_shared<TimerStub>());
  Check(event_handle && *event_handle == 1, "first handle was not stable");
  Check(timer_handle && *timer_handle == 2, "second handle was not stable");
  Check(handles.size() == 2, "handle table size is incorrect");
  Check(handles.Find(*event_handle, KernelObjectType::kEventFlag) == event,
        "typed event lookup failed");
  Check(!handles.Find(*event_handle, KernelObjectType::kTimer),
        "wrong object type was accepted");
  Check(!handles.Find(kInvalidKernelHandle, KernelObjectType::kEventFlag),
        "invalid handle was accepted");

  const auto removed =
      handles.Remove(*event_handle, KernelObjectType::kEventFlag);
  Check(removed == event, "removed object identity changed");
  Check(!handles.Find(*event_handle, KernelObjectType::kEventFlag),
        "removed handle still resolves");
  Check(!handles.Remove(*timer_handle, KernelObjectType::kEventFlag),
        "wrong object type was removed");

  const auto next_handle = handles.Insert(std::make_shared<TimerStub>());
  Check(next_handle && *next_handle == 3, "stale handle was reused");
}

void TestEventFlagService() {
  using namespace kajps5::kernel;

  EventFlagService service;
  const auto too_long = service.Create(std::string(32, 'x'), 0, 0);
  Check(too_long.status == KernelStatus::kInvalidArgument,
        "long event name was accepted");
  Check(service.Create("bad-attr", 0x04, 0).status ==
            KernelStatus::kInvalidArgument,
        "invalid event attributes were accepted");

  const auto created =
      service.Create(std::string(kMaximumEventFlagNameLength, 'e'),
                     kEventFlagThreadFifo | kEventFlagMulti, 0x03);
  Check(created && created.handle != kInvalidKernelHandle,
        "valid event creation failed");

  auto poll = service.Poll(created.handle, 0x05, kEventFlagWaitAll);
  Check(poll.status == KernelStatus::kBusy && poll.observed_pattern == 0x03,
        "unsatisfied all-bit poll changed its snapshot");

  Check(service.Set(created.handle, 0x04) == KernelStatus::kOk,
        "event set failed");
  poll = service.Poll(created.handle, 0x05,
                      kEventFlagWaitAll | kEventFlagClearPattern);
  Check(poll && poll.observed_pattern == 0x07,
        "all-bit poll did not return the pre-clear pattern");

  poll = service.Poll(created.handle, 0x02, kEventFlagWaitAny);
  Check(poll && poll.observed_pattern == 0x02,
        "pattern clear did not preserve unrelated bits");

  Check(service.Set(created.handle, 0x08) == KernelStatus::kOk,
        "second event set failed");
  poll = service.Poll(created.handle, 0x08,
                      kEventFlagWaitAny | kEventFlagClearAll);
  Check(poll && poll.observed_pattern == 0x0a,
        "any-bit clear-all poll returned the wrong snapshot");
  poll = service.Poll(created.handle, 0x01, kEventFlagWaitAny);
  Check(poll.status == KernelStatus::kBusy && poll.observed_pattern == 0,
        "clear-all did not clear every bit");

  Check(service.Set(created.handle, 0x0f) == KernelStatus::kOk,
        "mask setup failed");
  Check(service.Clear(created.handle, 0x05) == KernelStatus::kOk,
        "mask clear failed");
  poll = service.Poll(created.handle, 0x04, kEventFlagWaitAny);
  Check(poll && poll.observed_pattern == 0x05,
        "clear mask did not retain the selected bits");

  Check(service.Poll(created.handle, 0, kEventFlagWaitAny).status ==
            KernelStatus::kInvalidArgument,
        "zero poll pattern was accepted");
  Check(service.Poll(created.handle, 1, 0).status ==
            KernelStatus::kInvalidArgument,
        "invalid wait mode was accepted");
  Check(service.Poll(created.handle, 1, 0x42).status ==
            KernelStatus::kInvalidArgument,
        "unknown wait bits were accepted");

  Check(service.Delete(created.handle) == KernelStatus::kOk,
        "event deletion failed");
  Check(service.Delete(created.handle) == KernelStatus::kNotFound,
        "deleted handle remained valid");
  Check(service.Set(created.handle, 1) == KernelStatus::kNotFound,
        "set accepted a deleted handle");
}

} // namespace

int main() {
  TestHandleTable();
  TestEventFlagService();
  std::cout << "kernel event flag tests passed\n";
  return 0;
}
