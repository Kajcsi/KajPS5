// Copyright (C) 2026 KajPS5 contributors
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
#include "hle/kernel_event_flag_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_event_flag_exports_test: " << message << '\n';
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

std::uint64_t Dispatch(kajps5::hle::ExportRegistry& registry,
                       std::string_view symbol,
                       kajps5::hle::HleCallContext& context) {
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(static_cast<bool>(result), "event-flag export dispatch failed");
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

}  // namespace

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
  Check(kajps5::hle::RegisterKernelEventFlagExports(
            registry, runtime.event_flags()) == ExportRegistryStatus::kOk &&
            registry.size() == 10,
        "event-flag exports did not register atomically");

  auto name = Bytes("work-ready");
  name.push_back(std::byte{0});
  Check(memory.Initialize(0x1200, name), "event-flag name setup failed");
  HleCallContext create_context(memory);
  Check(create_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            create_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            create_context.SetRegister(HleRegister::kRdx, 0x21) &&
            create_context.SetRegister(HleRegister::kRcx, 0x03),
        "create event-flag argument setup failed");
  std::uint64_t handle = 0;
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEventFlagNid,
                 create_context) == 0 &&
            create_context.ReadUInt64(0x1100, handle) ==
                kajps5::hle::HleContextStatus::kOk &&
            handle != 0 && runtime.handles().size() == 1,
        "NID event-flag creation failed");

  HleCallContext busy_context(memory);
  Check(busy_context.SetRegister(HleRegister::kRdi, handle) &&
            busy_context.SetRegister(HleRegister::kRsi, 0x04) &&
            busy_context.SetRegister(HleRegister::kRdx,
                                     kajps5::kernel::kEventFlagWaitAny) &&
            busy_context.SetRegister(HleRegister::kRcx, 0x1300),
        "busy poll setup failed");
  std::uint64_t observed = 0;
  Check(Dispatch(registry, kajps5::hle::kKernelPollEventFlagName,
                 busy_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorBusy) &&
            busy_context.ReadUInt64(0x1300, observed) ==
                kajps5::hle::HleContextStatus::kOk &&
            observed == 0x03,
        "unsatisfied poll did not return the observed pattern");

  HleCallContext set_context(memory);
  Check(set_context.SetRegister(HleRegister::kRdi, handle) &&
            set_context.SetRegister(HleRegister::kRsi, 0x04),
        "set event-flag setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelSetEventFlagNid,
                 set_context) == 0,
        "NID event-flag set failed");

  HleCallContext clear_pattern_poll(memory);
  Check(clear_pattern_poll.SetRegister(HleRegister::kRdi, handle) &&
            clear_pattern_poll.SetRegister(HleRegister::kRsi, 0x05) &&
            clear_pattern_poll.SetRegister(
                HleRegister::kRdx, kajps5::kernel::kEventFlagWaitAll |
                                       kajps5::kernel::kEventFlagClearPattern) &&
            clear_pattern_poll.SetRegister(HleRegister::kRcx, 0x1308),
        "clear-pattern poll setup failed");
  observed = 0;
  Check(Dispatch(registry, kajps5::hle::kKernelPollEventFlagNid,
                 clear_pattern_poll) == 0 &&
            clear_pattern_poll.ReadUInt64(0x1308, observed) ==
                kajps5::hle::HleContextStatus::kOk &&
            observed == 0x07,
        "successful poll did not report its pre-clear pattern");

  HleCallContext second_set_context(memory);
  Check(second_set_context.SetRegister(HleRegister::kRdi, handle) &&
            second_set_context.SetRegister(HleRegister::kRsi, 0x0d),
        "second set setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelSetEventFlagName,
                 second_set_context) == 0,
        "named event-flag set failed");
  HleCallContext clear_context(memory);
  Check(clear_context.SetRegister(HleRegister::kRdi, handle) &&
            clear_context.SetRegister(HleRegister::kRsi, 0x0a),
        "clear event-flag setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelClearEventFlagNid,
                 clear_context) == 0,
        "NID event-flag clear failed");

  HleCallContext fault_poll_context(memory);
  Check(fault_poll_context.SetRegister(HleRegister::kRdi, handle) &&
            fault_poll_context.SetRegister(HleRegister::kRsi, 0x08) &&
            fault_poll_context.SetRegister(
                HleRegister::kRdx, kajps5::kernel::kEventFlagWaitAny |
                                       kajps5::kernel::kEventFlagClearAll) &&
            fault_poll_context.SetRegister(HleRegister::kRcx, 0x3000),
        "faulting poll setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPollEventFlagName,
                 fault_poll_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "faulting result pointer returned the wrong kernel result");
  HleCallContext preserved_poll_context(memory);
  Check(preserved_poll_context.SetRegister(HleRegister::kRdi, handle) &&
            preserved_poll_context.SetRegister(HleRegister::kRsi, 0x08) &&
            preserved_poll_context.SetRegister(
                HleRegister::kRdx, kajps5::kernel::kEventFlagWaitAny) &&
            preserved_poll_context.SetRegister(HleRegister::kRcx, 0x1310),
        "preserved-pattern poll setup failed");
  observed = 0;
  Check(Dispatch(registry, kajps5::hle::kKernelPollEventFlagName,
                 preserved_poll_context) == 0 &&
            preserved_poll_context.ReadUInt64(0x1310, observed) ==
                kajps5::hle::HleContextStatus::kOk &&
            observed == 0x0a,
        "faulting result pointer changed the event bits");

  HleCallContext zero_pattern_context(memory);
  Check(zero_pattern_context.SetRegister(HleRegister::kRdi, handle) &&
            zero_pattern_context.SetRegister(HleRegister::kRdx,
                                             kajps5::kernel::kEventFlagWaitAny),
        "zero-pattern poll setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPollEventFlagNid,
                 zero_pattern_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "zero event pattern was accepted");
  HleCallContext invalid_mode_context(memory);
  Check(invalid_mode_context.SetRegister(HleRegister::kRdi, handle) &&
            invalid_mode_context.SetRegister(HleRegister::kRsi, 1),
        "invalid-mode poll setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPollEventFlagName,
                 invalid_mode_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "invalid event wait mode was accepted");

  HleCallContext fault_create_context(memory);
  Check(fault_create_context.SetRegister(HleRegister::kRdi, 0x3000) &&
            fault_create_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            fault_create_context.SetRegister(HleRegister::kRdx, 1),
        "faulting create setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEventFlagName,
                 fault_create_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault) &&
            runtime.handles().size() == 1,
        "faulting handle output created an event flag");

  HleCallContext name_fault_context(memory);
  Check(name_fault_context.SetRegister(HleRegister::kRdi, 0x1110) &&
            name_fault_context.SetRegister(HleRegister::kRsi, 0x3000) &&
            name_fault_context.SetRegister(HleRegister::kRdx, 1),
        "faulting name setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEventFlagName,
                 name_fault_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "faulting event name returned the wrong kernel result");

  const auto overlong_name = Bytes(std::string(32, 'e'));
  Check(memory.Initialize(0x1400, overlong_name),
        "overlong event name setup failed");
  HleCallContext overlong_context(memory);
  Check(overlong_context.SetRegister(HleRegister::kRdi, 0x1110) &&
            overlong_context.SetRegister(HleRegister::kRsi, 0x1400) &&
            overlong_context.SetRegister(HleRegister::kRdx, 1),
        "overlong event create setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEventFlagName,
                 overlong_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "overlong event name returned the wrong kernel result");

  HleCallContext option_context(memory);
  Check(option_context.SetRegister(HleRegister::kRdi, 0x1110) &&
            option_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            option_context.SetRegister(HleRegister::kRdx, 1) &&
            option_context.SetRegister(HleRegister::kR8, 1),
        "unsupported option setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateEventFlagName,
                 option_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "unsupported event option was accepted");

  HleCallContext delete_context(memory);
  Check(delete_context.SetRegister(HleRegister::kRdi, handle),
        "delete event-flag setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteEventFlagNid,
                 delete_context) == 0 &&
            runtime.handles().size() == 0,
        "NID event-flag delete failed");
  HleCallContext stale_context(memory);
  Check(stale_context.SetRegister(HleRegister::kRdi, handle),
        "stale event setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelSetEventFlagName,
                 stale_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorNoSuchProcess),
        "stale event handle returned the wrong kernel result");

  Check(kajps5::hle::RegisterKernelEventFlagExports(
            registry, runtime.event_flags()) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 10,
        "duplicate event-flag export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
