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
#include <string_view>

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

}  // namespace

int main() {
#if !defined(_M_X64) && !defined(__x86_64__)
  return 0;
#else
  using kajps5::cpu::NativeGuestExecutionContext;
  using kajps5::cpu::NativeGuestProcessLauncher;
  using kajps5::cpu::NativeGuestProcessLaunchStatus;
  using kajps5::cpu::NativeGuestThreadRegistrationStatus;
  using kajps5::cpu::NativeGuestThreadRunner;
  using kajps5::cpu::NativeGuestThreadRunStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  auto memory = GuestMemory::CreateHostMapped(0x20000);
  Check(memory != nullptr, "host-mapped guest memory allocation failed");
  if (!memory) {
    return 1;
  }
  const auto base = memory->base_address();
  constexpr auto read_write =
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite;
  const std::array<std::byte, 7> process_entry = {
      std::byte{0x48}, std::byte{0x89}, std::byte{0xf8}, std::byte{0x48},
      std::byte{0x01}, std::byte{0xf0}, std::byte{0xc3}};
  Check(memory->Map(base, memory->mapping_granularity(), read_write) &&
            memory->Initialize(base, process_entry) &&
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

  kajps5::loader::ExecutableLaunchMetadata missing_entry;
  Check(launcher.Launch(missing_entry, "public.elf", base).status ==
            NativeGuestProcessLaunchStatus::kEntryPointMissing,
        "launch accepted missing entry metadata");
  Check(launcher.Launch(metadata, "", base).status ==
            NativeGuestProcessLaunchStatus::kInvalidArgument,
        "launch accepted an empty process image name");

  metadata.entry_point = base + memory->mapping_granularity();
  const auto rejected =
      launcher.Launch(metadata, "public.elf", base, {}, 0, 0x4000);
  Check(rejected.status ==
                NativeGuestProcessLaunchStatus::kThreadRegistrationFailed &&
            rejected.registration_status ==
                NativeGuestThreadRegistrationStatus::kGuestCodeNotExecutable &&
            rejected.thread == kajps5::kernel::kInvalidKernelHandle &&
            scheduler.SnapshotAll().size() == 1,
        "failed main-thread registration did not roll back");

  return 0;
#endif
}
