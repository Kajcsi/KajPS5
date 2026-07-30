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
            registry.size() == 12,
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

  const auto polled = runtime.event_queues().Poll(handle, 2);
  Check(polled && polled.events.size() == 1 &&
            polled.events[0].ident == 7 &&
            polled.events[0].flags ==
                (kajps5::kernel::kEventAdd | kajps5::kernel::kEventClear) &&
            polled.events[0].fflags == 1 &&
            polled.events[0].data == 0x22,
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

  HleCallContext delete_context(memory);
  Check(delete_context.SetRegister(HleRegister::kRdi, handle),
        "event-queue delete setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteEqueueNid,
                 delete_context) == 0 &&
            runtime.handles().size() == 0,
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
            registry.size() == 12,
        "duplicate event-queue export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
