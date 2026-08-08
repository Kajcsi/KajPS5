// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/title_session.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "cpu/native_hle_trampoline.h"
#include "hle/export_registry.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "title_session_test: " << message << '\n';
    std::exit(1);
  }
}

void AppendUInt64(std::vector<std::byte>& code, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    code.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

std::vector<std::byte> BuildOrderedEntry(std::uint64_t sequence_address,
                                         std::uint8_t expected,
                                         std::uint32_t return_value) {
  std::vector<std::byte> code = {std::byte{0x48}, std::byte{0xb8}};
  AppendUInt64(code, sequence_address);
  code.insert(code.end(), {std::byte{0x80}, std::byte{0x38},
                           static_cast<std::byte>(expected), std::byte{0x75},
                           std::byte{0x08}, std::byte{0xfe}, std::byte{0x00},
                           std::byte{0xb8}});
  for (std::size_t index = 0; index < sizeof(return_value); ++index) {
    code.push_back(
        static_cast<std::byte>((return_value >> (index * 8U)) & 0xffU));
  }
  code.insert(code.end(), {std::byte{0xc3}, std::byte{0xb8}, std::byte{0x01},
                           std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                           std::byte{0xc3}});
  return code;
}

std::vector<std::byte> BuildImportThenOrderedEntry(
    std::uint64_t trampoline, std::uint64_t sequence_address,
    std::uint8_t expected, std::uint32_t return_value) {
  std::vector<std::byte> code = {std::byte{0x48}, std::byte{0xb8}};
  AppendUInt64(code, trampoline);
  code.insert(code.end(), {std::byte{0x48}, std::byte{0x83}, std::byte{0xec},
                           std::byte{0x08}, std::byte{0xff}, std::byte{0xd0},
                           std::byte{0x48}, std::byte{0x83}, std::byte{0xc4},
                           std::byte{0x08}});
  const auto tail = BuildOrderedEntry(sequence_address, expected, return_value);
  code.insert(code.end(), tail.begin(), tail.end());
  return code;
}

}  // namespace

int main() {
#if !defined(_M_X64) && !defined(__x86_64__)
  return 0;
#else
  using kajps5::kernel::KernelStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;
  using kajps5::runtime::TitleSession;
  using kajps5::runtime::TitleSessionPhase;
  using kajps5::runtime::TitleSessionStatus;

  Check(!TitleSession::Create(std::make_unique<GuestMemory>(0x1000, 0x1000), {},
                              {}),
        "title session accepted memory without a native host mapping");

  auto hle_memory = GuestMemory::CreateHostMapped(0x10000);
  Check(hle_memory != nullptr, "title HLE memory allocation failed");
  if (!hle_memory) {
    return 1;
  }
  const auto hle_base =
      (hle_memory->base_address() + kajps5::hle::kHleDataPageSize - 1) &
      ~(kajps5::hle::kHleDataPageSize - 1);
  auto hle_session = TitleSession::Create(std::move(hle_memory));
  Check(hle_session != nullptr, "staged title session creation failed");
  if (!hle_session) {
    return 1;
  }
  Check(hle_session->Start("unconfigured.elf", hle_base).status ==
            TitleSessionStatus::kInvalidState,
        "unconfigured title session started");
  const std::array<kajps5::loader::ElfMetadata, 2> hle_metadata{};
  const std::array<const kajps5::loader::ElfMetadata*, 2>
      hle_metadata_pointers = {&hle_metadata[0], &hle_metadata[1]};
  const auto hle_setup = hle_session->PrepareHleBatch(hle_metadata_pointers,
                                                      hle_base, "public.elf");
  Check(hle_setup && hle_setup.data.page_address == hle_base &&
            hle_setup.imports.import_count == 0 &&
            hle_session->hle_data().size() == 4 &&
            hle_session->hle_exports().size() != 0 &&
            hle_session->hle_exports().Lookup("sceVideoOutOpen").status ==
                kajps5::hle::ExportRegistryStatus::kOk &&
            &hle_session->gpu_runtime() != nullptr &&
            &hle_session->video_out() != nullptr &&
            hle_session->hle_functions() != nullptr &&
            hle_session->hle_functions()->size() == 0,
        "title session did not own its default HLE runtime");
  Check(hle_session->PrepareHle({}, hle_base, "public.elf").status ==
                kajps5::runtime::TitleHleSetupStatus::kAlreadyAttempted &&
            kajps5::runtime::TitleHleSetupStatusName(
                kajps5::runtime::TitleHleSetupStatus::kDataSetupFailed) ==
                "data-setup-failed",
        "title HLE setup accepted a second attempt");
  Check(hle_session->Configure({}, {}) && !hle_session->Configure({}, {}),
        "title session configuration state changed");
  Check(hle_session->Start("invalid.elf", hle_base).status ==
            TitleSessionStatus::kStartupFailed,
        "invalid staged launch was not rejected");
  Check(hle_session->PrepareHle({}, hle_base, "public.elf").status ==
            kajps5::runtime::TitleHleSetupStatus::kInvalidState,
        "failed title session accepted HLE setup");

  auto memory = GuestMemory::CreateHostMapped(0x600000);
  Check(memory != nullptr, "host-mapped title memory allocation failed");
  if (!memory) {
    return 1;
  }
  const auto base = memory->base_address();
  const auto preinitializer = base;
  const auto initializer = base + 0x100;
  const auto main_entry = base + 0x200;
  const auto atexit_callback = base + 0x300;
  const auto finalizer = base + 0x400;
  const auto sequence_address = base + memory->mapping_granularity();
  constexpr auto read_write =
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite;
  const auto preinitializer_code = BuildOrderedEntry(sequence_address, 0, 0);
  const auto initializer_code = BuildOrderedEntry(sequence_address, 1, 0);
  const auto main_code = BuildOrderedEntry(sequence_address, 2, 42);
  const auto atexit_code = BuildOrderedEntry(sequence_address, 3, 0);
  const auto finalizer_code = BuildOrderedEntry(sequence_address, 4, 0);
  Check(memory->Map(base, memory->mapping_granularity(), read_write) &&
            memory->Map(sequence_address, memory->mapping_granularity(),
                        read_write) &&
            memory->Initialize(preinitializer, preinitializer_code) &&
            memory->Initialize(initializer, initializer_code) &&
            memory->Initialize(main_entry, main_code) &&
            memory->Initialize(atexit_callback, atexit_code) &&
            memory->Initialize(finalizer, finalizer_code) &&
            memory->Protect(
                base, memory->mapping_granularity(),
                GuestMemoryProtection::kRead | GuestMemoryProtection::kExecute),
        "title session fixture installation failed");

  kajps5::loader::ExecutableLaunchMetadata launch;
  launch.entry_point = main_entry;
  kajps5::loader::ExecutableLifecyclePlan lifecycle;
  lifecycle.preinitializers = {preinitializer};
  lifecycle.initializers = {initializer};
  lifecycle.finalizers = {finalizer};
  auto session =
      TitleSession::Create(std::move(memory), launch, std::move(lifecycle));
  Check(session != nullptr, "checked title session creation failed");
  if (!session) {
    return 1;
  }
  Check(session->kernel_runtime().process_lifecycle().RegisterAtexit(
            atexit_callback) == KernelStatus::kOk,
        "title atexit callback registration failed");

  const auto started = session->Start("public.elf", base, {}, 0, 0x4000);
  Check(started.status == TitleSessionStatus::kPending &&
            started.phase == TitleSessionPhase::kInitializing &&
            session->main_thread() == kajps5::kernel::kInvalidKernelHandle,
        "title session did not start in its initializer phase");
  Check(session->Start("public.elf", base).status ==
            TitleSessionStatus::kInvalidState,
        "title session accepted a second start");

  kajps5::runtime::TitleSessionResult completed;
  std::size_t completed_slices = 0;
  do {
    completed = session->Run(16);
    completed_slices += completed.slices;
  } while (completed.status == TitleSessionStatus::kPending);
  std::array<std::byte, 1> sequence{};
  Check(completed && completed.phase == TitleSessionPhase::kExited &&
            completed.exit_value == 42 && completed_slices == 5 &&
            session->exit_value() == 42 &&
            session->memory().Read(sequence_address, sequence) &&
            sequence[0] == std::byte{5} &&
            session->thread_runner().registered_thread_count() == 0,
        "title phases did not run in checked lifecycle order");
  Check(session->Run(1).status == TitleSessionStatus::kInvalidState,
        "exited title session resumed execution");
  Check(kajps5::runtime::TitleSessionStatusName(
            TitleSessionStatus::kSliceLimitReached) == "slice-limit-reached" &&
            kajps5::runtime::TitleSessionPhaseName(
                TitleSessionPhase::kFinalizing) == "finalizing",
        "title session names are unstable");

  auto blocked_memory = GuestMemory::CreateHostMapped(0x600000);
  Check(blocked_memory != nullptr, "blocking title memory allocation failed");
  if (!blocked_memory) {
    return 1;
  }
  const auto blocked_base = blocked_memory->base_address();
  const auto blocked_initializer = blocked_base;
  const auto waking_peer = blocked_base + 0x100;
  const auto blocked_main = blocked_base + 0x200;
  const auto blocked_sequence =
      blocked_base + blocked_memory->mapping_granularity();
  Check(blocked_memory->Map(blocked_base, blocked_memory->mapping_granularity(),
                            read_write) &&
            blocked_memory->Map(blocked_sequence,
                                blocked_memory->mapping_granularity(),
                                read_write),
        "blocking title mappings failed");

  kajps5::loader::ExecutableLaunchMetadata blocked_launch;
  blocked_launch.entry_point = blocked_main;
  kajps5::loader::ExecutableLifecyclePlan blocked_lifecycle;
  blocked_lifecycle.initializers = {blocked_initializer};
  auto blocked_session = TitleSession::Create(
      std::move(blocked_memory), blocked_launch, std::move(blocked_lifecycle));
  Check(blocked_session != nullptr, "blocking title session creation failed");
  if (!blocked_session) {
    return 1;
  }

  std::size_t block_dispatches = 0;
  std::size_t wake_dispatches = 0;
  kajps5::hle::ExportRegistry blocking_registry;
  Check(
      blocking_registry.Register(
          "libTitleTest", "block-init",
          [&](kajps5::hle::HleCallContext& context) {
            ++block_dispatches;
            if (block_dispatches == 1) {
              return blocked_session->kernel_runtime().scheduler().BlockCurrent(
                         "title-init")
                         ? kajps5::hle::HleContextStatus::kBlocked
                         : kajps5::hle::HleContextStatus::kInvalidArgument;
            }
            context.SetReturn(0);
            return kajps5::hle::HleContextStatus::kOk;
          }) == kajps5::hle::ExportRegistryStatus::kOk &&
          blocking_registry.Register(
              "libTitleTest", "wake-init",
              [&](kajps5::hle::HleCallContext& context) {
                ++wake_dispatches;
                const auto woken = blocked_session->kernel_runtime()
                                       .scheduler()
                                       .WakeBlockedThreads("title-init", 1);
                context.SetReturn(woken == 1 ? 0 : 1);
                return kajps5::hle::HleContextStatus::kOk;
              }) == kajps5::hle::ExportRegistryStatus::kOk,
      "blocking title exports failed");
  kajps5::cpu::NativeHleTrampoline block_trampoline(
      blocked_session->memory(), blocking_registry, "block-init",
      std::vector<std::string>{"libTitleTest"}, 0,
      &blocked_session->execution_context());
  kajps5::cpu::NativeHleTrampoline wake_trampoline(
      blocked_session->memory(), blocking_registry, "wake-init",
      std::vector<std::string>{"libTitleTest"}, 0,
      &blocked_session->execution_context());
  Check(block_trampoline.status() ==
                kajps5::cpu::NativeHleTrampolineStatus::kOk &&
            wake_trampoline.status() ==
                kajps5::cpu::NativeHleTrampolineStatus::kOk,
        "blocking title trampolines failed");
  const auto blocked_initializer_code = BuildImportThenOrderedEntry(
      block_trampoline.address(), blocked_sequence, 1, 0);
  const auto waking_peer_code = BuildImportThenOrderedEntry(
      wake_trampoline.address(), blocked_sequence, 0, 0);
  const auto blocked_main_code = BuildOrderedEntry(blocked_sequence, 2, 7);
  Check(
      blocked_session->memory().Initialize(blocked_initializer,
                                           blocked_initializer_code) &&
          blocked_session->memory().Initialize(waking_peer, waking_peer_code) &&
          blocked_session->memory().Initialize(blocked_main,
                                               blocked_main_code) &&
          blocked_session->memory().Protect(
              blocked_base, blocked_session->memory().mapping_granularity(),
              GuestMemoryProtection::kRead | GuestMemoryProtection::kExecute),
      "blocking title code installation failed");

  Check(blocked_session->Start("blocking.elf", blocked_base, {}, 0, 0x4000)
                .status == TitleSessionStatus::kPending,
        "blocking title startup failed");
  const auto blocked = blocked_session->Run(1);
  Check(blocked.status == TitleSessionStatus::kBlocked &&
            blocked.phase == TitleSessionPhase::kInitializing &&
            blocked.slices == 1 && block_dispatches == 1,
        "title initializer did not block through the shared scheduler");

  const auto peer = blocked_session->kernel_runtime().pthreads().CreateThread(
      "initializer-waker", 0, waking_peer, 0);
  Check(peer &&
            blocked_session->thread_runner().AllocateAndRegisterFunctionThread(
                peer.handle, blocked_base, {}),
        "initializer wake peer registration failed");
  kajps5::runtime::TitleSessionResult resumed;
  std::size_t resumed_slices = 0;
  do {
    resumed = blocked_session->Run(8);
    resumed_slices += resumed.slices;
  } while (resumed.status == TitleSessionStatus::kPending);
  std::array<std::byte, 1> blocked_sequence_value{};
  Check(resumed && resumed.exit_value == 7 && resumed_slices == 3 &&
            block_dispatches == 2 && wake_dispatches == 1 &&
            blocked_session->memory().Read(blocked_sequence,
                                           blocked_sequence_value) &&
            blocked_sequence_value[0] == std::byte{3},
        "title initializer did not resume after scheduled peer work");
  std::cout << "title session tests passed\n";
  return 0;
#endif
}
