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
#include "hle/kernel_semaphore_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_semaphore_exports_test: " << message << '\n';
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
  Check(static_cast<bool>(result), "semaphore export dispatch failed");
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
  Check(kajps5::hle::RegisterKernelSemaphoreExports(
            registry, runtime.semaphores()) == ExportRegistryStatus::kOk &&
            registry.size() == 8,
        "semaphore exports did not register atomically");

  auto name = Bytes("work");
  name.push_back(std::byte{0});
  Check(memory.Initialize(0x1200, name), "semaphore name setup failed");
  HleCallContext create_context(memory);
  Check(create_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            create_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            create_context.SetRegister(HleRegister::kRdx, 1) &&
            create_context.SetRegister(HleRegister::kRcx, 1) &&
            create_context.SetRegister(HleRegister::kR8, 2) &&
            create_context.SetRegister(HleRegister::kR9, 0),
        "create semaphore argument setup failed");
  std::uint64_t handle = 0;
  Check(Dispatch(registry, kajps5::hle::kKernelCreateSemaNid,
                 create_context) == 0 &&
            create_context.ReadUInt64(0x1100, handle) ==
                kajps5::hle::HleContextStatus::kOk &&
            handle != 0 && runtime.handles().size() == 1,
        "NID semaphore creation failed");

  HleCallContext poll_context(memory);
  Check(poll_context.SetRegister(HleRegister::kRdi, handle) &&
            poll_context.SetRegister(HleRegister::kRsi, 1),
        "poll semaphore setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPollSemaName,
                 poll_context) == 0,
        "named semaphore poll did not acquire the count");
  HleCallContext busy_context(memory);
  Check(busy_context.SetRegister(HleRegister::kRdi, handle) &&
            busy_context.SetRegister(HleRegister::kRsi, 1),
        "busy poll setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPollSemaNid, busy_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorBusy),
        "empty semaphore poll returned the wrong kernel result");

  HleCallContext signal_context(memory);
  Check(signal_context.SetRegister(HleRegister::kRdi, handle) &&
            signal_context.SetRegister(HleRegister::kRsi, 1),
        "signal semaphore setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelSignalSemaNid,
                 signal_context) == 0,
        "NID semaphore signal failed");
  HleCallContext overflow_context(memory);
  Check(overflow_context.SetRegister(HleRegister::kRdi, handle) &&
            overflow_context.SetRegister(HleRegister::kRsi, 2),
        "overflow signal setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelSignalSemaName,
                 overflow_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "overflow signal returned the wrong kernel result");
  HleCallContext preserved_context(memory);
  Check(preserved_context.SetRegister(HleRegister::kRdi, handle) &&
            preserved_context.SetRegister(HleRegister::kRsi, 1),
        "preserved-count poll setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPollSemaName,
                 preserved_context) == 0,
        "failed signal changed the semaphore count");

  HleCallContext fault_create_context(memory);
  Check(fault_create_context.SetRegister(HleRegister::kRdi, 0x3000) &&
            fault_create_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            fault_create_context.SetRegister(HleRegister::kRdx, 1) &&
            fault_create_context.SetRegister(HleRegister::kRcx, 0) &&
            fault_create_context.SetRegister(HleRegister::kR8, 1),
        "faulting create setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateSemaName,
                 fault_create_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault) &&
            runtime.handles().size() == 1,
        "faulting handle output created a semaphore");

  HleCallContext name_fault_context(memory);
  Check(name_fault_context.SetRegister(HleRegister::kRdi, 0x1110) &&
            name_fault_context.SetRegister(HleRegister::kRsi, 0x3000) &&
            name_fault_context.SetRegister(HleRegister::kRdx, 1) &&
            name_fault_context.SetRegister(HleRegister::kRcx, 0) &&
            name_fault_context.SetRegister(HleRegister::kR8, 1),
        "faulting name setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateSemaName,
                 name_fault_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "faulting semaphore name returned the wrong kernel result");

  HleCallContext option_context(memory);
  Check(option_context.SetRegister(HleRegister::kRdi, 0x1110) &&
            option_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            option_context.SetRegister(HleRegister::kRdx, 1) &&
            option_context.SetRegister(HleRegister::kRcx, 0) &&
            option_context.SetRegister(HleRegister::kR8, 1) &&
            option_context.SetRegister(HleRegister::kR9, 1),
        "unsupported option setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCreateSemaName,
                 option_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "unsupported semaphore option was accepted");

  HleCallContext delete_context(memory);
  Check(delete_context.SetRegister(HleRegister::kRdi, handle),
        "delete semaphore setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteSemaName,
                 delete_context) == 0 &&
            runtime.handles().size() == 0,
        "named semaphore delete failed");
  HleCallContext stale_context(memory);
  Check(stale_context.SetRegister(HleRegister::kRdi, handle),
        "stale semaphore setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelDeleteSemaNid,
                 stale_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorNoSuchProcess),
        "stale semaphore returned the wrong kernel result");

  Check(kajps5::hle::RegisterKernelSemaphoreExports(
            registry, runtime.semaphores()) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 8,
        "duplicate semaphore export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
