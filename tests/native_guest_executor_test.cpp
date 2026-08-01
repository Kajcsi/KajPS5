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
  auto nested_status = NativeGuestExecutionStatus::kOk;
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
  kajps5::cpu::NativeHleTrampoline trampoline(
      *memory, registry, "addOne", std::vector<std::string>{"libTest"}, 0,
      &execution_context);
  Check(trampoline.status() == kajps5::cpu::NativeHleTrampolineStatus::kOk,
        "HLE guest entry trampoline creation failed");
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
