// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_process_launcher.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "kernel/clock.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "kernel/pthread.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "native_guest_process_launcher_test: " << message << '\n';
    std::exit(1);
  }
}

void AppendUInt64(std::vector<std::byte>& code, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    code.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

std::vector<std::byte> BuildLifecycleEntry(std::uint64_t marker_address,
                                           bool increment) {
  std::vector<std::byte> code = {std::byte{0x48}, std::byte{0xb8}};
  AppendUInt64(code, marker_address);
  if (increment) {
    code.insert(code.end(),
                {std::byte{0x48}, std::byte{0xff}, std::byte{0x00}});
  } else {
    code.insert(code.end(), {std::byte{0x48}, std::byte{0xc7}, std::byte{0x00},
                             std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                             std::byte{0x00}});
  }
  code.insert(code.end(), {std::byte{0x48}, std::byte{0x89}, std::byte{0xf8},
                           std::byte{0x48}, std::byte{0x09}, std::byte{0xf0},
                           std::byte{0x48}, std::byte{0x09}, std::byte{0xd0},
                           std::byte{0xc3}});
  return code;
}

}  // namespace

int main() {
#if !defined(_M_X64) && !defined(__x86_64__)
  return 0;
#else
  using kajps5::cpu::NativeGuestExecutionContext;
  using kajps5::cpu::NativeGuestProcessLauncher;
  using kajps5::cpu::NativeGuestProcessLaunchStatus;
  using kajps5::cpu::NativeGuestProcessStartupStatus;
  using kajps5::cpu::NativeGuestThreadRegistrationStatus;
  using kajps5::cpu::NativeGuestThreadRunner;
  using kajps5::cpu::NativeGuestThreadRunStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  auto memory = GuestMemory::CreateHostMapped(0x400000);
  Check(memory != nullptr, "host-mapped guest memory allocation failed");
  if (!memory) {
    return 1;
  }
  const auto base = memory->base_address();
  const auto preinitializer_address = base + 0x100;
  const auto initializer_address = base + 0x200;
  const auto rejecting_initializer_address = base + 0x300;
  const auto marker_address = base + memory->mapping_granularity();
  constexpr auto read_write =
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite;
  const std::array<std::byte, 7> process_entry = {
      std::byte{0x48}, std::byte{0x89}, std::byte{0xf8}, std::byte{0x48},
      std::byte{0x01}, std::byte{0xf0}, std::byte{0xc3}};
  const auto preinitializer_entry = BuildLifecycleEntry(marker_address, false);
  const auto initializer_entry = BuildLifecycleEntry(marker_address, true);
  const std::array<std::byte, 6> rejecting_initializer_entry = {
      std::byte{0xb8}, std::byte{0x05}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xc3}};
  Check(memory->Map(base, memory->mapping_granularity(), read_write) &&
            memory->Map(marker_address, memory->mapping_granularity(),
                        read_write) &&
            memory->Initialize(base, process_entry) &&
            memory->Initialize(preinitializer_address, preinitializer_entry) &&
            memory->Initialize(initializer_address, initializer_entry) &&
            memory->Initialize(rejecting_initializer_address,
                               rejecting_initializer_entry) &&
            memory->Protect(
                base, memory->mapping_granularity(),
                GuestMemoryProtection::kRead | GuestMemoryProtection::kExecute),
        "process entry installation failed");

  kajps5::kernel::HandleTable handles;
  kajps5::kernel::KernelClockService clock;
  kajps5::kernel::GuestScheduler scheduler(handles, clock);
  kajps5::kernel::PthreadService pthreads(scheduler, clock);
  NativeGuestExecutionContext execution_context;
  NativeGuestThreadRunner runner(*memory, scheduler, pthreads,
                                 execution_context);
  NativeGuestProcessLauncher launcher(pthreads, runner);

  kajps5::loader::ExecutableLaunchMetadata metadata;
  metadata.entry_point = base;
  const std::array<std::string_view, 1> extra_arguments = {"--public-test"};
  const auto launched = launcher.Launch(metadata, "public.elf", base,
                                        extra_arguments, 0x44, 0x4000);
  Check(launched && launched.thread != kajps5::kernel::kInvalidKernelHandle &&
            launched.allocation.parameters_address != 0 &&
            pthreads.GetThread(launched.thread).has_value() &&
            runner.registered_thread_count() == 1,
        "checked launch metadata did not create the main guest thread");
  const auto exited = runner.RunUntilIdle(4);
  const auto snapshot = scheduler.Snapshot(launched.thread);
  Check(exited.status == NativeGuestThreadRunStatus::kIdle &&
            exited.slices == 1 && snapshot &&
            snapshot->state == kajps5::kernel::GuestThreadState::kExited &&
            snapshot->exit_value ==
                launched.allocation.parameters_address + 0x44 &&
            runner.registered_thread_count() == 0,
        "launched main guest thread did not run and exit");

  kajps5::loader::ExecutableLifecyclePlan lifecycle;
  lifecycle.preinitializers = {preinitializer_address};
  lifecycle.initializers = {initializer_address};
  const auto started = launcher.BeginStartup(
      metadata, lifecycle, "public.elf", base, extra_arguments, 0x55, 0x4000);
  Check(started.status == NativeGuestProcessStartupStatus::kPending &&
            started.lifecycle_kind ==
                kajps5::loader::LifecycleCallKind::kPreinitializer &&
            started.lifecycle_index == 0 && launcher.startup_active() &&
            runner.registered_thread_count() == 1,
        "checked process startup did not prepare its preinitializer");
  const auto ready = launcher.RunStartupUntilReady(4);
  std::uint64_t marker = 0;
  Check(ready && ready.slices == 2 && !launcher.startup_active() &&
            ready.launch.thread != kajps5::kernel::kInvalidKernelHandle &&
            memory->Read(marker_address,
                         std::as_writable_bytes(std::span{&marker, 1})) &&
            marker == 2 && runner.registered_thread_count() == 1,
        "lifecycle calls did not finish before main-thread creation");
  const auto lifecycle_main_exit = runner.RunUntilIdle(4);
  const auto lifecycle_main_snapshot = scheduler.Snapshot(ready.launch.thread);
  Check(lifecycle_main_exit.status == NativeGuestThreadRunStatus::kIdle &&
            lifecycle_main_exit.slices == 1 && lifecycle_main_snapshot &&
            lifecycle_main_snapshot->exit_value ==
                ready.launch.allocation.parameters_address + 0x55,
        "main entry did not run after lifecycle completion");

  kajps5::loader::ExecutableLifecyclePlan rejecting_lifecycle;
  rejecting_lifecycle.initializers = {rejecting_initializer_address};
  const auto rejecting_startup = launcher.BeginStartup(
      metadata, rejecting_lifecycle, "public.elf", base, {}, 0, 0x4000);
  const auto rejected_initializer = launcher.ContinueStartup();
  Check(rejecting_startup.status == NativeGuestProcessStartupStatus::kPending &&
            rejected_initializer.status ==
                NativeGuestProcessStartupStatus::kInitializerRejected &&
            rejected_initializer.run.execution.return_value == 5 &&
            !launcher.startup_active() && runner.registered_thread_count() == 0,
        "failed initializer did not stop process startup");

  kajps5::loader::ExecutableLaunchMetadata missing_entry;
  Check(launcher.Launch(missing_entry, "public.elf", base).status ==
            NativeGuestProcessLaunchStatus::kEntryPointMissing,
        "launch accepted missing entry metadata");
  Check(launcher.Launch(metadata, "", base).status ==
            NativeGuestProcessLaunchStatus::kInvalidArgument,
        "launch accepted an empty process image name");

  metadata.entry_point = base + memory->mapping_granularity();
  const auto thread_count_before_rejected = scheduler.SnapshotAll().size();
  const auto rejected =
      launcher.Launch(metadata, "public.elf", base, {}, 0, 0x4000);
  Check(rejected.status ==
                NativeGuestProcessLaunchStatus::kThreadRegistrationFailed &&
            rejected.registration_status ==
                NativeGuestThreadRegistrationStatus::kGuestCodeNotExecutable &&
            rejected.thread == kajps5::kernel::kInvalidKernelHandle &&
            scheduler.SnapshotAll().size() == thread_count_before_rejected,
        "failed main-thread registration did not roll back");

  return 0;
#endif
}
