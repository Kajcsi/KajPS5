// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/kernel_event_queue_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "hle_kernel_event_queue_exports_test: " << message << '\n';
    ++failures;
  }
}

std::vector<std::byte> Bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const auto character : text) {
    result.push_back(static_cast<std::byte>(character));
  }
  return result;
}

std::uint64_t KernelResult(std::int32_t result) {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
}

std::uint64_t Dispatch(kajps5::hle::ExportRegistry &registry,
                       std::string_view symbol,
                       kajps5::hle::HleCallContext &context) {
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(static_cast<bool>(result), "event-queue export dispatch failed");
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

} // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleRegister;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;

  GuestMemory memory(0x1000, 0x1000);
  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelEventQueueExports(
            registry, runtime.event_queues()) == ExportRegistryStatus::kOk &&
            registry.size() == 26,
        "event-queue exports did not register atomically");

  auto name = Bytes("main-queue");
  name.push_back(std::byte{0});
  Check(memory.Initialize(0x1200, name), "event-queue name setup failed");
  HleCallContext create_context(memory);
  Check(create_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            create_context.SetRegister(HleRegister::kRsi, 0x1200),
        "event-queue create setup failed");
  std::uint64_t handle = 0;
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEqueueNid,
                 create_context) == 0 &&
            create_context.ReadUInt64(0x1100, handle) ==
                kajps5::hle::HleContextStatus::kOk &&
            handle != 0 && runtime.handles().size() == 1,
        "NID event-queue creation failed");

  HleCallContext fault_create_context(memory);
  Check(fault_create_context.SetRegister(HleRegister::kRdi, 0x3000) &&
            fault_create_context.SetRegister(HleRegister::kRsi, 0x1200),
        "faulting event-queue create setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEqueueName,
                 fault_create_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault) &&
            runtime.handles().size() == 1,
        "faulting output created an event queue");

  auto overlong_name = Bytes(std::string(
      kajps5::kernel::kMaximumEventQueueNameLength + 1, 'q'));
  Check(memory.Initialize(0x1300, overlong_name),
        "overlong event-queue name setup failed");
  HleCallContext overlong_context(memory);
  Check(overlong_context.SetRegister(HleRegister::kRdi, 0x1110) &&
            overlong_context.SetRegister(HleRegister::kRsi, 0x1300),
        "overlong event-queue create setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEqueueName,
                 overlong_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "overlong event-queue name returned the wrong result");

  HleCallContext add_edge_context(memory);
  Check(add_edge_context.SetRegister(HleRegister::kRdi, handle) &&
            add_edge_context.SetRegister(HleRegister::kRsi, 7),
        "edge user-event setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelAddUserEventEdgeNid,
                 add_edge_context) == 0,
        "NID edge user-event registration failed");

  HleCallContext trigger_context(memory);
  Check(trigger_context.SetRegister(HleRegister::kRdi, handle) &&
            trigger_context.SetRegister(HleRegister::kRsi, 7) &&
            trigger_context.SetRegister(HleRegister::kRdx, 0x11),
        "user-event trigger setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelTriggerUserEventName,
                 trigger_context) == 0,
        "named user-event trigger failed");
  HleCallContext second_trigger_context(memory);
  Check(second_trigger_context.SetRegister(HleRegister::kRdi, handle) &&
            second_trigger_context.SetRegister(HleRegister::kRsi, 7) &&
            second_trigger_context.SetRegister(HleRegister::kRdx, 0x22),
        "second user-event trigger setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelTriggerUserEventNid,
                 second_trigger_context) == 0,
        "NID user-event trigger failed");

  HleCallContext fault_wait_context(memory);
  Check(fault_wait_context.SetRegister(HleRegister::kRdi, handle) &&
            fault_wait_context.SetRegister(HleRegister::kRsi, 0x3000) &&
            fault_wait_context.SetRegister(HleRegister::kRdx, 1) &&
            fault_wait_context.SetRegister(HleRegister::kRcx, 0x1100) &&
            fault_wait_context.SetRegister(HleRegister::kR8, 0),
        "faulting event wait setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelWaitEqueueNid,
                 fault_wait_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "faulting event output returned the wrong result");

  const auto polled = runtime.event_queues().Poll(handle, 2);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 7 &&
            polled.events[0].flags ==
                (kajps5::kernel::kEventAdd | kajps5::kernel::kEventClear) &&
            polled.events[0].fflags == 0 &&
            polled.events[0].data == 0 &&
            polled.events[0].user_data == 0x22,
        "HLE triggers did not use the shared deterministic queue");

  HleCallContext add_context(memory);
  Check(add_context.SetRegister(HleRegister::kRdi, handle) &&
            add_context.SetRegister(HleRegister::kRsi, 9),
        "normal user-event setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelAddUserEventName,
                 add_context) == 0,
        "named user-event registration failed");
  HleCallContext delete_user_context(memory);
  Check(delete_user_context.SetRegister(HleRegister::kRdi, handle) &&
            delete_user_context.SetRegister(HleRegister::kRsi, 9),
        "delete user-event setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteUserEventNid,
                 delete_user_context) == 0,
        "NID user-event deletion failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteUserEventName,
                 delete_user_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorNotFound),
        "missing user event returned the wrong result");

  constexpr std::uint64_t kEventAddress = 0x1400;
  constexpr std::uint64_t kOutCountAddress = 0x1480;
  constexpr std::uint64_t kTimeoutAddress = 0x1490;
  HleCallContext timed_wait_context(memory);
  Check(timed_wait_context.SetRegister(HleRegister::kRdi, handle) &&
            timed_wait_context.SetRegister(HleRegister::kRsi,
                                           kEventAddress) &&
            timed_wait_context.SetRegister(HleRegister::kRdx, 1) &&
            timed_wait_context.SetRegister(HleRegister::kRcx,
                                           kOutCountAddress) &&
            timed_wait_context.SetRegister(HleRegister::kR8,
                                           kTimeoutAddress) &&
            timed_wait_context.WriteUInt32(kTimeoutAddress, 0) ==
                kajps5::hle::HleContextStatus::kOk,
        "timed event wait setup failed");
  std::uint32_t out_count = 1;
  Check(Dispatch(registry, kajps5::hle::kKernelWaitEqueueName,
                 timed_wait_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorTimedOut) &&
            timed_wait_context.ReadUInt32(kOutCountAddress, out_count) ==
                kajps5::hle::HleContextStatus::kOk &&
            out_count == 0,
        "zero-time event wait returned the wrong result");

  const auto waiter = runtime.scheduler().CreateThread("hle-equeue", 0);
  Check(waiter && runtime.scheduler().SelectNext() == waiter.handle,
        "blocking event wait thread setup failed");
  HleCallContext wait_context(memory);
  Check(wait_context.SetRegister(HleRegister::kRdi, handle) &&
            wait_context.SetRegister(HleRegister::kRsi, kEventAddress) &&
            wait_context.SetRegister(HleRegister::kRdx, 1) &&
            wait_context.SetRegister(HleRegister::kRcx,
                                     kOutCountAddress) &&
            wait_context.SetRegister(HleRegister::kR8, 0),
        "blocking event wait setup failed");
  const std::vector<std::string> libraries = {
      kajps5::hle::kLibKernelName};
  const auto blocked = registry.Dispatch(
      kajps5::hle::kKernelWaitEqueueNid, libraries, wait_context);
  Check(blocked.status == ExportRegistryStatus::kOk &&
            blocked.handler_status ==
                kajps5::hle::HleContextStatus::kBlocked &&
            !runtime.scheduler().current_thread(),
        "empty HLE event wait did not block the guest thread");
  Check(runtime.event_queues().TriggerUserEvent(handle, 7, 0x66) ==
                kajps5::kernel::KernelStatus::kOk &&
            runtime.scheduler().SelectNext() == waiter.handle,
        "HLE event wait did not wake for a queued edge");
  const auto resumed = registry.Dispatch(
      kajps5::hle::kKernelWaitEqueueNid, libraries, wait_context);
  std::uint64_t event_ident = 0;
  std::uint32_t event_filter_and_flags = 0;
  std::uint32_t event_fflags = 0;
  std::uint64_t event_data = 0;
  std::uint64_t event_user_data = 0;
  Check(resumed &&
            wait_context.ReadUInt32(kOutCountAddress, out_count) ==
                kajps5::hle::HleContextStatus::kOk &&
            wait_context.ReadUInt64(kEventAddress, event_ident) ==
                kajps5::hle::HleContextStatus::kOk &&
            wait_context.ReadUInt32(kEventAddress + 8,
                                    event_filter_and_flags) ==
                kajps5::hle::HleContextStatus::kOk &&
            wait_context.ReadUInt32(kEventAddress + 12, event_fflags) ==
                kajps5::hle::HleContextStatus::kOk &&
            wait_context.ReadUInt64(kEventAddress + 16, event_data) ==
                kajps5::hle::HleContextStatus::kOk &&
            wait_context.ReadUInt64(kEventAddress + 24,
                                    event_user_data) ==
                kajps5::hle::HleContextStatus::kOk &&
            out_count == 1 && event_ident == 7 &&
            event_filter_and_flags == 0x0021fff5U && event_fflags == 0 &&
            event_data == 0 && event_user_data == 0x66,
        "resumed HLE event wait wrote the wrong event record");

  HleCallContext accessor_context(memory);
  Check(accessor_context.SetRegister(HleRegister::kRdi, kEventAddress),
        "event accessor setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelGetEventUserDataNid,
                 accessor_context) == 0x66 &&
            Dispatch(registry, kajps5::hle::kKernelGetEventIdName,
                     accessor_context) == 7 &&
            Dispatch(registry, kajps5::hle::kKernelGetEventFilterNid,
                     accessor_context) == 0xfffffff5U &&
            Dispatch(registry, kajps5::hle::kKernelGetEventDataName,
                     accessor_context) == 0 &&
            Dispatch(registry, kajps5::hle::kKernelGetEventFflagsNid,
                     accessor_context) == 0 &&
            Dispatch(registry, kajps5::hle::kKernelGetEventErrorName,
                     accessor_context) == 0,
        "event accessors returned the wrong record fields");
  Check(accessor_context.WriteUInt32(kEventAddress + 8, 0x4000fff5U) ==
                kajps5::hle::HleContextStatus::kOk &&
            accessor_context.WriteUInt64(kEventAddress + 16,
                                         0x80020003U) ==
                kajps5::hle::HleContextStatus::kOk &&
            Dispatch(registry, kajps5::hle::kKernelGetEventErrorNid,
                     accessor_context) == 0x80020003U &&
            runtime.scheduler().ExitCurrent(0),
        "event error accessor ignored the error flag");

  HleCallContext delete_context(memory);
  Check(delete_context.SetRegister(HleRegister::kRdi, handle),
        "event-queue delete setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteEqueueNid,
                 delete_context) == 0 &&
            runtime.handles().Find(
                handle, kajps5::kernel::KernelObjectType::kEventQueue) ==
                nullptr,
        "NID event-queue deletion failed");
  HleCallContext stale_context(memory);
  Check(stale_context.SetRegister(HleRegister::kRdi, handle) &&
            stale_context.SetRegister(HleRegister::kRsi, 1),
        "stale event-queue setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelAddUserEventEdgeName,
                 stale_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorBadFileDescriptor),
        "stale event-queue handle returned the wrong result");

  Check(kajps5::hle::RegisterKernelEventQueueExports(
            registry, runtime.event_queues()) ==
                ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 26,
        "duplicate event-queue export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
