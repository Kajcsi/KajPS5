// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "cpu/native_hle_trampoline.h"
#include "hle/export_registry.h"
#include "kernel/clock.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "native_guest_executor_test: " << message << '\n';
    ++failures;
  }
}

void AppendUInt64(std::vector<std::byte>& code, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    code.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

std::uint64_t ReadUInt64(const kajps5::memory::GuestMemory& memory,
                         std::uint64_t address) {
  std::array<std::byte, sizeof(std::uint64_t)> bytes{};
  if (!memory.Read(address, bytes)) {
    ++failures;
    return 0;
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

kajps5::hle::HleVectorValue MakeVector(std::uint64_t low_bits) {
  kajps5::hle::HleVectorValue value{};
  for (std::size_t index = 0; index < sizeof(low_bits); ++index) {
    value[index] = static_cast<std::byte>((low_bits >> (index * 8U)) & 0xffU);
  }
  return value;
}

std::uint64_t ReadVector(const kajps5::hle::HleVectorValue& value) {
  std::uint64_t low_bits = 0;
  for (std::size_t index = 0; index < sizeof(low_bits); ++index) {
    low_bits |= static_cast<std::uint64_t>(value[index]) << (index * 8U);
  }
  return low_bits;
}

}  // namespace

int main() {
  using kajps5::cpu::NativeGuestExecutionStatus;
  using kajps5::cpu::NativeGuestExecutor;
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleContextStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

#if !defined(_M_X64) && !defined(__x86_64__)
  return 0;
#else
  auto memory = GuestMemory::CreateHostMapped(0x10000);
  Check(memory != nullptr, "host-mapped guest memory allocation failed");
  if (!memory) {
    return 1;
  }
  const auto base = memory->base_address();
  const auto code_address = base;
  const auto hle_code_address = base + 0x100;
  const auto memory_fault_code_address = base + 0x200;
  const auto instruction_fault_code_address = base + 0x220;
  const auto blocked_hle_code_address = base + 0x300;
  const auto stack_address = base + 0x4000;
  const auto stack_size = std::uint64_t{0x4000};
  const auto parameters_address = stack_address + 0x100;
  const auto exit_handler_address = std::uint64_t{0x123456789abcdef0};

  Check(memory->Map(
            code_address, 0x1000,
            GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite) &&
            memory->Map(
                stack_address, stack_size,
                GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite),
        "guest entry mappings failed");

  NativeGuestExecutor executor;
  kajps5::cpu::NativeGuestExecutionContext execution_context;
  kajps5::kernel::HandleTable handles;
  kajps5::kernel::KernelClockService clock;
  kajps5::kernel::GuestScheduler scheduler(handles, clock);
  auto nested_status = NativeGuestExecutionStatus::kOk;
  std::size_t blocking_dispatch_count = 0;
  ExportRegistry registry;
  Check(registry.Register(
            "libTest", "addOne",
            [&](kajps5::hle::HleCallContext& call_context) {
              nested_status =
                  executor
                      .Execute(*memory, code_address, stack_address, stack_size,
                               parameters_address, 0, &execution_context)
                      .status;
              call_context.SetReturn(call_context.Argument(0).value_or(0) + 1);
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "HLE guest entry handler registration failed");
  Check(registry.Register(
            "libTest", "blockUntilWake",
            [&](kajps5::hle::HleCallContext& call_context) {
              ++blocking_dispatch_count;
              const auto vector_argument = call_context.VectorArgument(0);
              Check(vector_argument &&
                        ReadVector(*vector_argument) == 0x0102030405060708ULL,
                    "blocked HLE call changed its vector argument");
              if (blocking_dispatch_count == 1) {
                return scheduler.BlockCurrent("native-hle-test")
                           ? HleContextStatus::kBlocked
                           : HleContextStatus::kInvalidArgument;
              }
              call_context.SetReturn(0x55);
              Check(call_context.SetVectorReturn(0, MakeVector(0x100)),
                    "resumed HLE call rejected its vector return");
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "blocking HLE handler registration failed");
  kajps5::cpu::NativeHleTrampoline trampoline(
      *memory, registry, "addOne", std::vector<std::string>{"libTest"}, 0,
      &execution_context);
  Check(trampoline.status() == kajps5::cpu::NativeHleTrampolineStatus::kOk,
        "HLE guest entry trampoline creation failed");
  kajps5::cpu::NativeHleTrampoline blocking_trampoline(
      *memory, registry, "blockUntilWake", std::vector<std::string>{"libTest"},
      0, &execution_context);
  Check(blocking_trampoline.status() ==
            kajps5::cpu::NativeHleTrampolineStatus::kOk,
        "blocking HLE trampoline creation failed");
  using RawTrampoline = std::uint64_t (*)();
  Check(reinterpret_cast<RawTrampoline>(
            static_cast<std::uintptr_t>(trampoline.address()))() == 0,
        "inactive guest execution context changed the host stack");

  const std::array<std::byte, 17> entry_code = {
      std::byte{0x48}, std::byte{0x89}, std::byte{0x27}, std::byte{0x48},
      std::byte{0x89}, std::byte{0x77}, std::byte{0x08}, std::byte{0x48},
      std::byte{0xb8}, std::byte{0x88}, std::byte{0x77}, std::byte{0x66},
      std::byte{0x55}, std::byte{0x44}, std::byte{0x33}, std::byte{0x22},
      std::byte{0x11}};
  std::vector<std::byte> complete_entry(entry_code.begin(), entry_code.end());
  complete_entry.push_back(std::byte{0xc3});

  std::vector<std::byte> hle_entry = {
      std::byte{0x48}, std::byte{0xbf}, std::byte{0x29}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x48}, std::byte{0xb8}};
  AppendUInt64(hle_entry, trampoline.address());
  hle_entry.push_back(std::byte{0x48});
  hle_entry.push_back(std::byte{0x83});
  hle_entry.push_back(std::byte{0xec});
  hle_entry.push_back(std::byte{0x08});
  hle_entry.push_back(std::byte{0xff});
  hle_entry.push_back(std::byte{0xd0});
  hle_entry.push_back(std::byte{0x48});
  hle_entry.push_back(std::byte{0x83});
  hle_entry.push_back(std::byte{0xc4});
  hle_entry.push_back(std::byte{0x08});
  hle_entry.push_back(std::byte{0xc3});

  std::vector<std::byte> blocked_hle_entry = {std::byte{0x48}, std::byte{0xbb}};
  AppendUInt64(blocked_hle_entry, 1);
  blocked_hle_entry.insert(blocked_hle_entry.end(),
                           {std::byte{0x48}, std::byte{0xbd}});
  AppendUInt64(blocked_hle_entry, 2);
  blocked_hle_entry.insert(blocked_hle_entry.end(),
                           {std::byte{0x49}, std::byte{0xbc}});
  AppendUInt64(blocked_hle_entry, 3);
  blocked_hle_entry.insert(blocked_hle_entry.end(),
                           {std::byte{0x49}, std::byte{0xbd}});
  AppendUInt64(blocked_hle_entry, 4);
  blocked_hle_entry.insert(blocked_hle_entry.end(),
                           {std::byte{0x49}, std::byte{0xbe}});
  AppendUInt64(blocked_hle_entry, 5);
  blocked_hle_entry.insert(blocked_hle_entry.end(),
                           {std::byte{0x49}, std::byte{0xbf}});
  AppendUInt64(blocked_hle_entry, 6);
  blocked_hle_entry.insert(blocked_hle_entry.end(),
                           {std::byte{0x48}, std::byte{0xb8}});
  AppendUInt64(blocked_hle_entry, 0x0102030405060708ULL);
  blocked_hle_entry.insert(
      blocked_hle_entry.end(),
      {std::byte{0x66}, std::byte{0x48}, std::byte{0x0f}, std::byte{0x6e},
       std::byte{0xc0}, std::byte{0x48}, std::byte{0xb8}});
  AppendUInt64(blocked_hle_entry, blocking_trampoline.address());
  blocked_hle_entry.push_back(std::byte{0x48});
  blocked_hle_entry.push_back(std::byte{0x83});
  blocked_hle_entry.push_back(std::byte{0xec});
  blocked_hle_entry.push_back(std::byte{0x08});
  blocked_hle_entry.push_back(std::byte{0xff});
  blocked_hle_entry.push_back(std::byte{0xd0});
  blocked_hle_entry.push_back(std::byte{0x48});
  blocked_hle_entry.push_back(std::byte{0x83});
  blocked_hle_entry.push_back(std::byte{0xc4});
  blocked_hle_entry.push_back(std::byte{0x08});
  blocked_hle_entry.insert(
      blocked_hle_entry.end(),
      {std::byte{0x48}, std::byte{0x01}, std::byte{0xd8}, std::byte{0x48},
       std::byte{0x01}, std::byte{0xe8}, std::byte{0x4c}, std::byte{0x01},
       std::byte{0xe0}, std::byte{0x4c}, std::byte{0x01}, std::byte{0xe8},
       std::byte{0x4c}, std::byte{0x01}, std::byte{0xf0}, std::byte{0x4c},
       std::byte{0x01}, std::byte{0xf8}, std::byte{0x66}, std::byte{0x48},
       std::byte{0x0f}, std::byte{0x7e}, std::byte{0xc1}, std::byte{0x48},
       std::byte{0x01}, std::byte{0xc8}});
  blocked_hle_entry.push_back(std::byte{0xc3});

  const std::array<std::byte, 14> memory_fault_entry = {
      std::byte{0x48}, std::byte{0xb8}, std::byte{0x01}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x48}, std::byte{0x8b},
      std::byte{0x00}, std::byte{0xc3}};
  const std::array<std::byte, 2> instruction_fault_entry = {std::byte{0x0f},
                                                            std::byte{0x0b}};

  Check(memory->Initialize(code_address, complete_entry) &&
            memory->Initialize(hle_code_address, hle_entry) &&
            memory->Initialize(memory_fault_code_address, memory_fault_entry) &&
            memory->Initialize(instruction_fault_code_address,
                               instruction_fault_entry) &&
            memory->Initialize(blocked_hle_code_address, blocked_hle_entry) &&
            memory->Protect(
                code_address, 0x1000,
                GuestMemoryProtection::kRead | GuestMemoryProtection::kExecute),
        "guest entry code installation failed");

  const auto direct = executor.Execute(
      *memory, code_address, stack_address, stack_size, parameters_address,
      exit_handler_address, &execution_context);
  const auto observed_stack = ReadUInt64(*memory, parameters_address);
  Check(direct.status == NativeGuestExecutionStatus::kOk &&
            direct.return_value == 0x1122334455667788 &&
            observed_stack >= stack_address &&
            observed_stack < stack_address + stack_size &&
            observed_stack % 16 == 8 &&
            ReadUInt64(*memory, parameters_address + 8) == exit_handler_address,
        "guest entry did not use the System V arguments and guest stack");

  const auto hle =
      executor.Execute(*memory, hle_code_address, stack_address, stack_size,
                       parameters_address, 0, &execution_context);
  const auto hle_snapshot = trampoline.last_dispatch();
  Check(hle.status == NativeGuestExecutionStatus::kOk &&
            hle.return_value == 42 &&
            hle_snapshot.lookup_status == ExportRegistryStatus::kOk &&
            hle_snapshot.handler_status == HleContextStatus::kOk &&
            nested_status == NativeGuestExecutionStatus::kInvalidArgument &&
            !execution_context.active(),
        "guest entry did not return through the checked HLE trampoline");

  const auto waiter = scheduler.CreateThread("native-waiter", 0);
  Check(waiter && scheduler.SelectNext() == waiter.handle,
        "native wait test thread did not start");
  const auto blocked =
      executor.Execute(*memory, blocked_hle_code_address, stack_address,
                       stack_size, parameters_address, 0, &execution_context);
  const auto blocked_thread = scheduler.Snapshot(waiter.handle);
  Check(blocked.status == NativeGuestExecutionStatus::kHleBlocked &&
            NativeGuestExecutionStatusName(blocked.status) == "hle-blocked" &&
            blocked.hle_status == HleContextStatus::kBlocked &&
            execution_context.suspended() && blocked_thread &&
            blocked_thread->state == kajps5::kernel::GuestThreadState::kBlocked,
        "blocked HLE call did not suspend its guest continuation");
  Check(executor.Execute(*memory, code_address, stack_address, stack_size,
                         parameters_address, 0, &execution_context)
                .status == NativeGuestExecutionStatus::kInvalidArgument,
        "suspended execution context accepted a new guest entry");
  Check(scheduler.WakeBlockedThreads("native-hle-test", 1) == 1 &&
            scheduler.SelectNext() == waiter.handle,
        "blocked native guest thread did not wake");
  const auto resumed = executor.Resume(*memory, execution_context);
  Check(resumed.status == NativeGuestExecutionStatus::kOk &&
            resumed.return_value == 0x16a && blocking_dispatch_count == 2 &&
            !execution_context.active() && !execution_context.suspended(),
        "woken HLE call did not resume the saved guest continuation");
  Check(executor.Resume(*memory, execution_context).status ==
            NativeGuestExecutionStatus::kInvalidArgument,
        "completed guest continuation resumed twice");
  Check(scheduler.ExitCurrent(resumed.return_value),
        "resumed native guest thread did not exit cleanly");

#if defined(_WIN32)
  const auto memory_fault =
      executor.Execute(*memory, memory_fault_code_address, stack_address,
                       stack_size, parameters_address, 0, &execution_context);
  Check(
      memory_fault.status == NativeGuestExecutionStatus::kGuestMemoryFault &&
          NativeGuestExecutionStatusName(memory_fault.status) ==
              "guest-memory-fault" &&
          memory_fault.host_exception_code == 0xc0000005U &&
          memory_fault.fault_instruction_pointer >= memory_fault_code_address &&
          memory_fault.fault_instruction_pointer <
              memory_fault_code_address + memory_fault_entry.size() &&
          memory_fault.fault_address == 1 && !execution_context.active(),
      "guest memory fault did not return through the Windows boundary");

  const auto instruction_fault =
      executor.Execute(*memory, instruction_fault_code_address, stack_address,
                       stack_size, parameters_address, 0, &execution_context);
  Check(
      instruction_fault.status ==
              NativeGuestExecutionStatus::kGuestInstructionFault &&
          NativeGuestExecutionStatusName(instruction_fault.status) ==
              "guest-instruction-fault" &&
          instruction_fault.host_exception_code == 0xc000001dU &&
          instruction_fault.fault_instruction_pointer ==
              instruction_fault_code_address &&
          !execution_context.active(),
      "illegal guest instruction did not return through the Windows boundary");

  Check(executor.Execute(*memory, code_address, stack_address, stack_size,
                         parameters_address, 0, &execution_context)
                .status == NativeGuestExecutionStatus::kOk,
        "guest execution did not recover after a contained fault");
#endif

  GuestMemory copied(0x1000, 0x4000,
                     GuestMemoryProtection::kRead |
                         GuestMemoryProtection::kWrite |
                         GuestMemoryProtection::kExecute);
  Check(executor.Execute(copied, 0x1000, 0x2000, 0x1000, 0x2000, 0).status ==
            NativeGuestExecutionStatus::kHostMappingRequired,
        "copied guest memory was accepted for direct entry execution");
  Check(executor.Execute(*memory, stack_address, stack_address, stack_size,
                         parameters_address, 0)
                .status == NativeGuestExecutionStatus::kGuestCodeNotExecutable,
        "non-executable guest entry was accepted");
  Check(executor.Execute(*memory, code_address, code_address, 0x1000,
                         parameters_address, 0)
                .status == NativeGuestExecutionStatus::kGuestStackNotAccessible,
        "non-writable guest stack was accepted");
  Check(executor.Execute(*memory, code_address, stack_address, stack_size,
                         base + 0x9000, 0)
                .status ==
            NativeGuestExecutionStatus::kGuestParametersNotReadable,
        "unmapped guest entry parameters were accepted");

  return failures == 0 ? 0 : 1;
#endif
}
