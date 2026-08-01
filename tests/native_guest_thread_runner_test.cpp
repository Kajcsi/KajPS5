// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_thread_runner.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "cpu/native_hle_trampoline.h"
#include "hle/export_registry.h"
#include "kernel/clock.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "kernel/pthread.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "native_guest_thread_runner_test: " << message << '\n';
    std::exit(1);
  }
}

void AppendUInt64(std::vector<std::byte>& code, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    code.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

std::vector<std::byte> BuildImportEntry(std::uint64_t trampoline) {
  std::vector<std::byte> code = {std::byte{0x48}, std::byte{0x89},
                                 std::byte{0xfb}, std::byte{0x48},
                                 std::byte{0xb8}};
  AppendUInt64(code, trampoline);
  code.insert(code.end(), {std::byte{0x48}, std::byte{0x83}, std::byte{0xec},
                           std::byte{0x08}, std::byte{0xff}, std::byte{0xd0},
                           std::byte{0x48}, std::byte{0x83}, std::byte{0xc4},
                           std::byte{0x08}, std::byte{0x48}, std::byte{0x01},
                           std::byte{0xd8}, std::byte{0xc3}});
  return code;
}

}  // namespace

int main() {
#if !defined(_M_X64) && !defined(__x86_64__)
  return 0;
#else
  using kajps5::cpu::NativeGuestExecutionContext;
  using kajps5::cpu::NativeGuestThreadRegistrationStatus;
  using kajps5::cpu::NativeGuestThreadRunner;
  using kajps5::cpu::NativeGuestThreadRunStatus;
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleContextStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  auto memory = GuestMemory::CreateHostMapped(0x20000);
  Check(memory != nullptr, "host-mapped guest memory allocation failed");
  if (!memory) {
    return 1;
  }
  const auto base = memory->base_address();
  const auto yield_code = base;
  const auto peer_code = base + 0x100;
  const auto block_code = base + 0x200;
  const auto exit_code = base + 0x300;
  const auto process_code = base + 0x400;
  const auto stack_one = base + 0x4000;
  const auto stack_two = base + 0x8000;
  const auto stack_three = base + 0xc000;
  const auto stack_four = base + 0x10000;
  constexpr auto stack_size = std::uint64_t{0x4000};
  constexpr auto read_write =
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite;
  Check(memory->Map(base, 0x1000, read_write) &&
            memory->Map(stack_one, stack_size, read_write) &&
            memory->Map(stack_two, stack_size, read_write) &&
            memory->Map(stack_three, stack_size, read_write) &&
            memory->Map(stack_four, stack_size, read_write),
        "guest thread mappings failed");

  kajps5::kernel::HandleTable handles;
  kajps5::kernel::KernelClockService clock;
  kajps5::kernel::GuestScheduler scheduler(handles, clock);
  kajps5::kernel::PthreadService pthreads(scheduler, clock);
  NativeGuestExecutionContext execution_context;
  std::size_t yield_dispatches = 0;
  std::size_t block_dispatches = 0;
  ExportRegistry registry;
  Check(registry.Register("libTest", "yield",
                          [&](kajps5::hle::HleCallContext& context) {
                            ++yield_dispatches;
                            if (!scheduler.YieldCurrent()) {
                              return HleContextStatus::kInvalidArgument;
                            }
                            context.RequestYield();
                            context.SetReturn(0x10);
                            return HleContextStatus::kOk;
                          }) == ExportRegistryStatus::kOk,
        "yield export registration failed");
  Check(registry.Register("libTest", "block",
                          [&](kajps5::hle::HleCallContext& context) {
                            ++block_dispatches;
                            if (block_dispatches == 1) {
                              return scheduler.BlockCurrent("runner-block")
                                         ? HleContextStatus::kBlocked
                                         : HleContextStatus::kInvalidArgument;
                            }
                            context.SetReturn(0x20);
                            return HleContextStatus::kOk;
                          }) == ExportRegistryStatus::kOk,
        "blocking export registration failed");
  Check(registry.Register("libTest", "exit",
                          [&](kajps5::hle::HleCallContext& context) {
                            return pthreads.ExitCurrent(
                                       context.Argument(0).value_or(0))
                                       ? HleContextStatus::kGuestExit
                                       : HleContextStatus::kInvalidArgument;
                          }) == ExportRegistryStatus::kOk,
        "thread exit export registration failed");
  kajps5::cpu::NativeHleTrampoline yield_trampoline(
      *memory, registry, "yield", std::vector<std::string>{"libTest"}, 0,
      &execution_context);
  kajps5::cpu::NativeHleTrampoline block_trampoline(
      *memory, registry, "block", std::vector<std::string>{"libTest"}, 0,
      &execution_context);
  kajps5::cpu::NativeHleTrampoline exit_trampoline(
      *memory, registry, "exit", std::vector<std::string>{"libTest"}, 0,
      &execution_context);
  Check(yield_trampoline.status() ==
                kajps5::cpu::NativeHleTrampolineStatus::kOk &&
            block_trampoline.status() ==
                kajps5::cpu::NativeHleTrampolineStatus::kOk &&
            exit_trampoline.status() ==
                kajps5::cpu::NativeHleTrampolineStatus::kOk,
        "native HLE trampoline creation failed");

  const auto yield_entry = BuildImportEntry(yield_trampoline.address());
  const auto block_entry = BuildImportEntry(block_trampoline.address());
  const auto exit_entry = BuildImportEntry(exit_trampoline.address());
  const std::vector<std::byte> peer_entry = {std::byte{0x48}, std::byte{0x89},
                                             std::byte{0xf8}, std::byte{0xc3}};
  const std::vector<std::byte> process_entry = {
      std::byte{0x48}, std::byte{0x89}, std::byte{0xf8}, std::byte{0x48},
      std::byte{0x01}, std::byte{0xf0}, std::byte{0xc3}};
  Check(memory->Initialize(yield_code, yield_entry) &&
            memory->Initialize(peer_code, peer_entry) &&
            memory->Initialize(block_code, block_entry) &&
            memory->Initialize(exit_code, exit_entry) &&
            memory->Initialize(process_code, process_entry) &&
            memory->Protect(
                base, 0x1000,
                GuestMemoryProtection::kRead | GuestMemoryProtection::kExecute),
        "guest thread code installation failed");

  NativeGuestThreadRunner runner(*memory, scheduler, pthreads,
                                 execution_context);
  const auto yielding = pthreads.CreateThread("yielding", 0, yield_code, 5);
  const auto peer = pthreads.CreateThread("peer", 0, peer_code, 7);
  Check(yielding && peer &&
            runner.RegisterThread(yielding.handle, stack_one, stack_size) ==
                NativeGuestThreadRegistrationStatus::kOk &&
            runner.RegisterThread(peer.handle, stack_two, stack_size) ==
                NativeGuestThreadRegistrationStatus::kOk &&
            runner.RegisterThread(peer.handle, stack_two, stack_size) ==
                NativeGuestThreadRegistrationStatus::kThreadAlreadyRegistered,
        "guest thread registration failed");
  const auto overlapping =
      pthreads.CreateThread("overlapping", 0, peer_code, 0);
  Check(overlapping &&
            runner.RegisterThread(overlapping.handle, stack_one + 0x1000,
                                  stack_size) ==
                NativeGuestThreadRegistrationStatus::
                    kGuestStackAlreadyRegistered &&
            pthreads.DiscardReadyThread(overlapping.handle),
        "overlapping guest thread stack was accepted");

  const auto yielded = runner.RunNext();
  const auto yielding_snapshot = scheduler.Snapshot(yielding.handle);
  Check(
      yielded.status == NativeGuestThreadRunStatus::kThreadYielded &&
          yielded.slices == 1 && yielded.thread == yielding.handle &&
          yielding_snapshot &&
          yielding_snapshot->state == kajps5::kernel::GuestThreadState::kReady,
      "yielding guest thread did not park");
  const auto peer_exit = runner.RunNext();
  const auto peer_snapshot = scheduler.Snapshot(peer.handle);
  Check(peer_exit.status == NativeGuestThreadRunStatus::kThreadExited &&
            peer_exit.thread == peer.handle && peer_snapshot &&
            peer_snapshot->state == kajps5::kernel::GuestThreadState::kExited &&
            peer_snapshot->exit_value == 7,
        "peer guest thread did not run and exit");
  const auto yield_exit = runner.RunNext();
  const auto yield_snapshot = scheduler.Snapshot(yielding.handle);
  Check(yield_exit.status == NativeGuestThreadRunStatus::kThreadExited &&
            yield_snapshot && yield_snapshot->exit_value == 0x15 &&
            yield_dispatches == 1,
        "yielded guest thread did not resume exactly once");

  const auto blocking = pthreads.CreateThread("blocking", 0, block_code, 3);
  Check(blocking &&
            runner.RegisterThread(blocking.handle, stack_three, stack_size) ==
                NativeGuestThreadRegistrationStatus::kOk,
        "blocking guest thread registration failed");
  const auto blocked = runner.RunNext();
  Check(blocked.status == NativeGuestThreadRunStatus::kThreadBlocked &&
            runner.RunNext().status == NativeGuestThreadRunStatus::kIdle &&
            scheduler.WakeBlockedThreads("runner-block", 1) == 1,
        "blocked guest thread did not park and wake");
  const auto block_exit = runner.RunNext();
  const auto block_snapshot = scheduler.Snapshot(blocking.handle);
  Check(block_exit.status == NativeGuestThreadRunStatus::kThreadExited &&
            block_snapshot && block_snapshot->exit_value == 0x23 &&
            block_dispatches == 2 && runner.registered_thread_count() == 0 &&
            runner.RunNext().status == NativeGuestThreadRunStatus::kIdle,
        "blocked guest thread did not retry and exit");

  const auto forced_exit =
      pthreads.CreateThread("forced-exit", 0, exit_code, 0x44);
  Check(forced_exit &&
            runner.RegisterThread(forced_exit.handle, stack_four, stack_size) ==
                NativeGuestThreadRegistrationStatus::kOk,
        "forced-exit guest thread registration failed");
  const auto forced_exit_result = runner.RunNext();
  const auto forced_exit_snapshot = scheduler.Snapshot(forced_exit.handle);
  Check(
      forced_exit_result.status == NativeGuestThreadRunStatus::kThreadExited &&
          forced_exit_snapshot &&
          forced_exit_snapshot->state ==
              kajps5::kernel::GuestThreadState::kExited &&
          forced_exit_snapshot->exit_value == 0x44 &&
          runner.registered_thread_count() == 0,
      "guest thread exit request was not recorded");

  const auto pump_one = pthreads.CreateThread("pump-one", 0, peer_code, 9);
  const auto pump_two = pthreads.CreateThread("pump-two", 0, peer_code, 10);
  Check(pump_one && pump_two &&
            runner.RegisterThread(pump_one.handle, stack_one, stack_size) ==
                NativeGuestThreadRegistrationStatus::kOk &&
            runner.RegisterThread(pump_two.handle, stack_two, stack_size) ==
                NativeGuestThreadRegistrationStatus::kOk,
        "guest thread pump registration failed");
  const auto limited_pump = runner.RunUntilIdle(1);
  Check(limited_pump.status == NativeGuestThreadRunStatus::kSliceLimitReached &&
            limited_pump.slices == 1 && runner.registered_thread_count() == 1,
        "guest thread pump did not enforce its slice limit");
  const auto pumped = runner.RunUntilIdle(8);
  const auto pump_one_snapshot = scheduler.Snapshot(pump_one.handle);
  const auto pump_two_snapshot = scheduler.Snapshot(pump_two.handle);
  Check(pumped.status == NativeGuestThreadRunStatus::kIdle &&
            pumped.slices == 1 && pump_one_snapshot && pump_two_snapshot &&
            pump_one_snapshot->exit_value == 9 &&
            pump_two_snapshot->exit_value == 10 &&
            runner.registered_thread_count() == 0,
        "bounded guest thread pump did not drain ready work");

  const auto stack_attribute = pthreads.CreateAttribute();
  Check(stack_attribute && pthreads.SetAttributeStackSize(
                               stack_attribute.handle, stack_size) ==
                               kajps5::kernel::KernelStatus::kOk,
        "automatic guest stack attribute setup failed");
  const auto automatic = pthreads.CreateThread(
      "automatic", stack_attribute.handle, peer_code, 0x55);
  const auto allocation =
      runner.AllocateAndRegisterThread(automatic.handle, base);
  const auto guard = memory->QueryRegion(allocation.guard_address);
  std::array<std::byte, 16> initial_stack{};
  Check(automatic && allocation && allocation.stack_size == stack_size &&
            allocation.guard_size == memory->mapping_granularity() && guard &&
            guard->protection == GuestMemoryProtection::kNone &&
            memory->CanAccess(allocation.stack_address, allocation.stack_size,
                              read_write) &&
            memory->Read(allocation.stack_address, initial_stack),
        "automatic guest stack mapping is invalid");
  for (const auto value : initial_stack) {
    Check(value == std::byte{0}, "automatic guest stack was not zeroed");
  }
  const auto automatic_exit = runner.RunNext();
  const auto automatic_snapshot = scheduler.Snapshot(automatic.handle);
  Check(automatic_exit.status == NativeGuestThreadRunStatus::kThreadExited &&
            automatic_snapshot && automatic_snapshot->exit_value == 0x55 &&
            !memory->IsMapped(allocation.guard_address,
                              allocation.guard_size + allocation.stack_size),
        "automatic guest stack was not released after thread exit");

  const auto automatic_range = memory->FindUnmappedRange(
      base, memory->mapping_granularity() + stack_size,
      memory->mapping_granularity());
  const auto self_prepared = pthreads.CreateThread(
      "self-prepared", stack_attribute.handle, yield_code, 0x66);
  const auto self_prepared_yield = runner.RunNext();
  const auto self_prepared_guard =
      automatic_range ? memory->QueryRegion(*automatic_range) : std::nullopt;
  Check(self_prepared && automatic_range &&
            self_prepared_yield.status ==
                NativeGuestThreadRunStatus::kThreadYielded &&
            self_prepared_yield.thread == self_prepared.handle &&
            runner.registered_thread_count() == 1 && self_prepared_guard &&
            self_prepared_guard->protection == GuestMemoryProtection::kNone,
        "ready pthread did not receive an automatic guarded stack");
  const auto self_prepared_exit = runner.RunNext();
  const auto self_prepared_snapshot = scheduler.Snapshot(self_prepared.handle);
  Check(
      self_prepared_exit.status == NativeGuestThreadRunStatus::kThreadExited &&
          self_prepared_snapshot &&
          self_prepared_snapshot->exit_value == 0x76 &&
          runner.registered_thread_count() == 0 &&
          !memory->IsMapped(*automatic_range,
                            memory->mapping_granularity() + stack_size) &&
          pthreads.DestroyAttribute(stack_attribute.handle) ==
              kajps5::kernel::KernelStatus::kOk,
      "automatic pthread stack was not released after resumed exit");

  const auto process =
      scheduler.CreateThread("process-entry", 700, process_code, 0);
  Check(process && runner.RegisterProcessThread(process.handle, stack_one,
                                                stack_size, stack_one, 0x22) ==
                       NativeGuestThreadRegistrationStatus::kOk,
        "process entry registration failed");
  const auto process_exit = runner.RunNext();
  const auto process_snapshot = scheduler.Snapshot(process.handle);
  Check(process_exit.status == NativeGuestThreadRunStatus::kThreadExited &&
            process_snapshot &&
            process_snapshot->state ==
                kajps5::kernel::GuestThreadState::kExited &&
            process_snapshot->exit_value == stack_one + 0x22 &&
            runner.registered_thread_count() == 0,
        "process entry did not use its parameter and exit-handler arguments");

  const auto invalid_process =
      scheduler.CreateThread("invalid-process", 700, process_code, 0);
  Check(invalid_process &&
            runner.RegisterProcessThread(invalid_process.handle, stack_one,
                                         stack_size, base + 0x1000, 0) ==
                NativeGuestThreadRegistrationStatus::
                    kGuestParametersNotReadable &&
            scheduler.DiscardReadyThread(invalid_process.handle),
        "process entry accepted an unreadable parameter block");

  const auto oversized = pthreads.CreateThread("oversized", 0, peer_code, 0);
  const auto oversized_allocation = runner.RunNext();
  const auto oversized_snapshot = scheduler.Snapshot(oversized.handle);
  Check(oversized &&
            oversized_allocation.status ==
                NativeGuestThreadRunStatus::kThreadStackRegistrationFailed &&
            oversized_allocation.registration_status ==
                NativeGuestThreadRegistrationStatus::
                    kGuestStackAllocationFailed &&
            oversized_snapshot &&
            oversized_snapshot->state ==
                kajps5::kernel::GuestThreadState::kReady &&
            pthreads.DiscardReadyThread(oversized.handle),
        "failed guest stack allocation changed scheduler state");

  const auto unregistered =
      scheduler.CreateThread("unregistered", 700, peer_code, 0);
  const auto missing = runner.RunNext();
  const auto missing_snapshot = scheduler.Snapshot(unregistered.handle);
  Check(
      unregistered &&
          missing.status == NativeGuestThreadRunStatus::kThreadNotRegistered &&
          missing_snapshot &&
          missing_snapshot->state == kajps5::kernel::GuestThreadState::kReady &&
          scheduler.DiscardReadyThread(unregistered.handle),
      "unregistered thread was not returned to the ready queue");

  return 0;
#endif
}
