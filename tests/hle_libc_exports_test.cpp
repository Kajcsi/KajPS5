// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/libc_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_libc_exports_test: " << message << '\n';
    ++failures;
  }
}

std::uint64_t ReadWord(kajps5::hle::HleCallContext& context,
                       std::uint64_t address) {
  std::uint64_t word = 0;
  Check(context.ReadUInt64(address, word) ==
            kajps5::hle::HleContextStatus::kOk,
        "guard word read failed");
  return word;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::HleRegister;
  using kajps5::kernel::GuestThreadState;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;

  GuestMemory memory(0x1000, 0x100);
  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcExports(
            registry, runtime.cxa_guards(), runtime.process_lifecycle(),
            runtime.libc_heap(), memory) == ExportRegistryStatus::kOk &&
            registry.size() == 58,
        "libc exports did not register atomically");

  constexpr std::uint64_t kGuardAddress = 0x1020;
  constexpr std::uint64_t kUpperWord = 0xaabbccdd00000000;
  HleCallContext setup(memory);
  Check(setup.WriteUInt64(kGuardAddress, kUpperWord) == HleContextStatus::kOk,
        "guard setup failed");

  const auto first = runtime.scheduler().CreateThread("first", 1);
  Check(first && runtime.scheduler().SelectNext() == first.handle,
        "first guard thread did not start");
  const std::vector<std::string> libc_scope = {kajps5::hle::kLibcName};

  HleCallContext pure_virtual(memory);
  const auto pure_virtual_result = registry.Dispatch(
      kajps5::hle::kCxaPureVirtualNid, libc_scope, pure_virtual);
  Check(pure_virtual_result.status == ExportRegistryStatus::kOk &&
            pure_virtual_result.handler_status ==
                HleContextStatus::kFatalGuestError &&
            !pure_virtual_result && !pure_virtual.return_written() &&
            registry.Lookup(kajps5::hle::kCxaPureVirtualName, libc_scope),
        "pure virtual call did not stop at the fatal guest boundary");

  HleCallContext acquire_first(memory);
  Check(acquire_first.SetRegister(HleRegister::kRdi, kGuardAddress),
        "first acquire argument setup failed");
  const auto acquired = registry.Dispatch(
      kajps5::hle::kCxaGuardAcquireNid, libc_scope, acquire_first);
  Check(acquired &&
            acquire_first.GetRegister(HleRegister::kRax).value_or(0) == 1 &&
            ReadWord(acquire_first, kGuardAddress) ==
                (kUpperWord | 0x0100) &&
            runtime.cxa_guards().owned_count() == 1,
        "first guard acquire did not claim the guest guard");

  HleCallContext recursive(memory);
  Check(recursive.SetRegister(HleRegister::kRdi, kGuardAddress),
        "recursive acquire argument setup failed");
  Check(registry.Dispatch(kajps5::hle::kCxaGuardAcquireName, libc_scope,
                          recursive) &&
            recursive.GetRegister(HleRegister::kRax).value_or(1) == 0,
        "recursive guard acquire did not return without reinitializing");

  const auto second = runtime.scheduler().CreateThread("second", 1);
  Check(second && runtime.scheduler().YieldCurrent() &&
            runtime.scheduler().SelectNext() == second.handle,
        "second guard thread did not start");
  HleCallContext blocked(memory);
  Check(blocked.SetRegister(HleRegister::kRdi, kGuardAddress),
        "blocked acquire argument setup failed");
  const auto blocked_result = registry.Dispatch(
      kajps5::hle::kCxaGuardAcquireNid, libc_scope, blocked);
  const auto blocked_snapshot = runtime.scheduler().Snapshot(second.handle);
  Check(blocked_result.status == ExportRegistryStatus::kOk &&
            blocked_result.handler_status == HleContextStatus::kBlocked &&
            !blocked.return_written() && blocked_snapshot.has_value() &&
            blocked_snapshot->state == GuestThreadState::kBlocked,
        "contended guard acquire did not block the guest thread");

  Check(runtime.scheduler().SelectNext() == first.handle,
        "guard owner did not resume");
  HleCallContext release(memory);
  Check(release.SetRegister(HleRegister::kRdi, kGuardAddress),
        "release argument setup failed");
  const auto released = registry.Dispatch(
      kajps5::hle::kCxaGuardReleaseNid, libc_scope, release);
  const auto released_snapshot = runtime.scheduler().Snapshot(second.handle);
  Check(released && ReadWord(release, kGuardAddress) == (kUpperWord | 1) &&
            runtime.cxa_guards().owned_count() == 0 &&
            released_snapshot.has_value() &&
            released_snapshot->state == GuestThreadState::kReady,
        "guard release did not publish completion and wake the waiter");

  Check(runtime.scheduler().YieldCurrent() &&
            runtime.scheduler().SelectNext() == second.handle,
        "released guard waiter did not resume");
  HleCallContext completed(memory);
  Check(completed.SetRegister(HleRegister::kRdi, kGuardAddress),
        "completed acquire argument setup failed");
  Check(registry.Dispatch(kajps5::hle::kCxaGuardAcquireNid, libc_scope,
                          completed) &&
            completed.GetRegister(HleRegister::kRax).value_or(1) == 0,
        "completed guard was initialized twice");

  Check(setup.WriteUInt64(kGuardAddress, kUpperWord) == HleContextStatus::kOk,
        "abort guard reset failed");
  HleCallContext acquire_abort(memory);
  Check(acquire_abort.SetRegister(HleRegister::kRdi, kGuardAddress) &&
            registry.Dispatch(kajps5::hle::kCxaGuardAcquireNid, libc_scope,
                              acquire_abort),
        "abort-path guard acquire failed");
  HleCallContext abort(memory);
  Check(abort.SetRegister(HleRegister::kRdi, kGuardAddress) &&
            registry.Dispatch(kajps5::hle::kCxaGuardAbortNid, libc_scope,
                              abort) &&
            ReadWord(abort, kGuardAddress) == kUpperWord &&
            runtime.cxa_guards().owned_count() == 0,
        "guard abort did not clear the low state bits");

  Check(setup.WriteUInt64(kGuardAddress, kUpperWord) == HleContextStatus::kOk,
        "owner-check guard reset failed");
  HleCallContext acquire_owned(memory);
  Check(acquire_owned.SetRegister(HleRegister::kRdi, kGuardAddress) &&
            registry.Dispatch(kajps5::hle::kCxaGuardAcquireNid, libc_scope,
                              acquire_owned) &&
            runtime.scheduler().YieldCurrent() &&
            runtime.scheduler().SelectNext() == first.handle,
        "owner-check guard setup failed");
  HleCallContext wrong_release(memory);
  Check(wrong_release.SetRegister(HleRegister::kRdi, kGuardAddress),
        "wrong-owner release argument setup failed");
  const auto rejected_release = registry.Dispatch(
      kajps5::hle::kCxaGuardReleaseNid, libc_scope, wrong_release);
  Check(rejected_release.status == ExportRegistryStatus::kOk &&
            rejected_release.handler_status ==
                HleContextStatus::kInvalidArgument &&
            ReadWord(wrong_release, kGuardAddress) ==
                (kUpperWord | 0x0100) &&
            runtime.cxa_guards().owned_count() == 1,
        "non-owner guard release changed the guard");

  HleCallContext owner_wait(memory);
  Check(owner_wait.SetRegister(HleRegister::kRdi, kGuardAddress),
        "owner wait argument setup failed");
  const auto owner_wait_result = registry.Dispatch(
      kajps5::hle::kCxaGuardAcquireNid, libc_scope, owner_wait);
  Check(owner_wait_result.handler_status == HleContextStatus::kBlocked &&
            runtime.scheduler().SelectNext() == second.handle,
        "non-owner acquire did not wait for the guard owner");
  HleCallContext owner_abort(memory);
  Check(owner_abort.SetRegister(HleRegister::kRdi, kGuardAddress),
        "owner abort argument setup failed");
  const auto owner_aborted = registry.Dispatch(
      kajps5::hle::kCxaGuardAbortNid, libc_scope, owner_abort);
  const auto owner_wait_snapshot =
      runtime.scheduler().Snapshot(first.handle);
  Check(owner_aborted && owner_wait_snapshot.has_value() &&
            owner_wait_snapshot->state == GuestThreadState::kReady,
        "owner abort did not wake the blocked guest thread");

  HleCallContext fault(memory);
  Check(fault.SetRegister(HleRegister::kRdi, 0x2000),
        "fault acquire argument setup failed");
  const auto faulted = registry.Dispatch(
      kajps5::hle::kCxaGuardAcquireNid, libc_scope, fault);
  Check(faulted.status == ExportRegistryStatus::kOk &&
            faulted.handler_status == HleContextStatus::kMemoryFault &&
            runtime.cxa_guards().owned_count() == 0,
        "invalid guard memory changed ownership state");

  Check(kajps5::hle::HleContextStatusName(HleContextStatus::kBlocked) ==
            "blocked",
        "blocked HLE status name is unstable");
  return failures == 0 ? 0 : 1;
}
