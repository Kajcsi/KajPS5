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

#if defined(_WIN32) && defined(_M_X64)
#include <intrin.h>
#endif

#include "core/memory/guest_memory.h"
#include "cpu/native_hle_trampoline.h"
#include "hle/export_registry.h"
#include "kernel/clock.h"
#include "kernel/guest_scheduler.h"
#include "kernel/handle_table.h"
#include "loader/static_tls_instance.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "native_guest_executor_test: " << message << '\n';
    ++failures;
  }
}

std::uint64_t HostFsBaseForTest() noexcept {
#if defined(_WIN32) && defined(_M_X64)
  return _readfsbase_u64();
#else
  return 0;
#endif
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
  const auto yielded_hle_code_address = base + 0x500;
  const auto thread_code_address = base + 0x600;
  const auto function_code_address = base + 0x700;
  const auto tls_code_address = base + 0x800;
  const auto stack_address = base + 0x4000;
  const auto stack_size = std::uint64_t{0x4000};
  const auto parameters_address = stack_address + 0x100;
  const auto worker_stack_address = base + 0x8000;
  const auto worker_stack_size = std::uint64_t{0x4000};
  const auto worker_parameters_address = worker_stack_address + 0x100;
  const auto exit_handler_address = std::uint64_t{0x123456789abcdef0};

  Check(memory->Map(
            code_address, 0x1000,
            GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite) &&
            memory->Map(
                stack_address, stack_size,
                GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite) &&
            memory->Map(
                worker_stack_address, worker_stack_size,
                GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite),
        "guest entry mappings failed");

  NativeGuestExecutor executor;
  kajps5::cpu::NativeGuestExecutionContext execution_context;
  kajps5::kernel::HandleTable handles;
  kajps5::kernel::KernelClockService clock;
  kajps5::kernel::GuestScheduler scheduler(handles, clock);
  auto nested_status = NativeGuestExecutionStatus::kOk;
  std::size_t blocking_dispatch_count = 0;
  std::size_t yielding_dispatch_count = 0;
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
  Check(registry.Register("libTest", "yieldOnce",
                          [&](kajps5::hle::HleCallContext& call_context) {
                            ++yielding_dispatch_count;
                            if (!scheduler.YieldCurrent()) {
                              return HleContextStatus::kInvalidArgument;
                            }
                            call_context.RequestYield();
                            call_context.SetReturn(0x33);
                            return HleContextStatus::kOk;
                          }) == ExportRegistryStatus::kOk,
        "yielding HLE handler registration failed");
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
  kajps5::cpu::NativeHleTrampoline yielding_trampoline(
      *memory, registry, "yieldOnce", std::vector<std::string>{"libTest"}, 0,
      &execution_context);
  Check(yielding_trampoline.status() ==
            kajps5::cpu::NativeHleTrampolineStatus::kOk,
        "yielding HLE trampoline creation failed");
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

  std::vector<std::byte> yielded_hle_entry = {std::byte{0x48}, std::byte{0xb8}};
  AppendUInt64(yielded_hle_entry, yielding_trampoline.address());
  yielded_hle_entry.insert(
      yielded_hle_entry.end(),
      {std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, std::byte{0x08},
       std::byte{0xff}, std::byte{0xd0}, std::byte{0x48}, std::byte{0x83},
       std::byte{0xc4}, std::byte{0x08}, std::byte{0xc3}});

  const std::array<std::byte, 14> memory_fault_entry = {
      std::byte{0x48}, std::byte{0xb8}, std::byte{0x01}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x48}, std::byte{0x8b},
      std::byte{0x00}, std::byte{0xc3}};
  const std::array<std::byte, 2> instruction_fault_entry = {std::byte{0x0f},
                                                            std::byte{0x0b}};
  const std::array<std::byte, 4> thread_entry = {
      std::byte{0x48}, std::byte{0x89}, std::byte{0xf8}, std::byte{0xc3}};
  const std::array<std::byte, 19> function_entry = {
      std::byte{0x48}, std::byte{0x89}, std::byte{0xf8}, std::byte{0x48},
      std::byte{0x01}, std::byte{0xf0}, std::byte{0x48}, std::byte{0x01},
      std::byte{0xd0}, std::byte{0x48}, std::byte{0x01}, std::byte{0xc8},
      std::byte{0x4c}, std::byte{0x01}, std::byte{0xc0}, std::byte{0x4c},
      std::byte{0x01}, std::byte{0xc8}, std::byte{0xc3}};
  const std::array<std::byte, 14> tls_entry = {
      std::byte{0x64}, std::byte{0x48}, std::byte{0x8b}, std::byte{0x04},
      std::byte{0x25}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x48}, std::byte{0x8b}, std::byte{0x40},
      std::byte{0xf8}, std::byte{0xc3}};

  Check(memory->Initialize(code_address, complete_entry) &&
            memory->Initialize(hle_code_address, hle_entry) &&
            memory->Initialize(memory_fault_code_address, memory_fault_entry) &&
            memory->Initialize(instruction_fault_code_address,
                               instruction_fault_entry) &&
            memory->Initialize(blocked_hle_code_address, blocked_hle_entry) &&
            memory->Initialize(yielded_hle_code_address, yielded_hle_entry) &&
            memory->Initialize(thread_code_address, thread_entry) &&
            memory->Initialize(function_code_address, function_entry) &&
            memory->Initialize(tls_code_address, tls_entry) &&
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

  const auto thread_entry_result = executor.ExecuteThread(
      *memory, thread_code_address, stack_address, stack_size,
      0xcafebabedeadbeefULL, &execution_context);
  Check(thread_entry_result.status == NativeGuestExecutionStatus::kOk &&
            thread_entry_result.return_value == 0xcafebabedeadbeefULL,
        "guest thread entry did not receive its System V argument");

  const std::array<std::uint64_t, 6> function_arguments = {1, 2, 3, 4, 5, 6};
  const auto function_result = executor.ExecuteFunction(
      *memory, function_code_address, stack_address, stack_size,
      function_arguments, &execution_context);
  Check(function_result.status == NativeGuestExecutionStatus::kOk &&
            function_result.return_value == 21,
        "guest function did not receive all System V integer arguments");
  const std::array<std::uint64_t, 7> excessive_arguments{};
  Check(executor.ExecuteFunction(*memory, function_code_address, stack_address,
                                 stack_size, excessive_arguments,
                                 &execution_context)
                .status == NativeGuestExecutionStatus::kInvalidArgument,
        "guest function accepted too many register arguments");

  kajps5::loader::StaticTlsLayout tls_layout;
  const auto tls_module = tls_layout.RegisterModule(1, 8, 8);
  Check(tls_module && tls_module.module.static_offset == 8,
        "static TLS layout registration failed");
  const auto tls_template_address = base + 0xc000;
  const std::array tls_template = {
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
      std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88}};
  Check(memory->Map(tls_template_address, 0x1000,
                    GuestMemoryProtection::kRead |
                        GuestMemoryProtection::kWrite) &&
            memory->Initialize(tls_template_address, tls_template),
        "static TLS template installation failed");
  const std::array<kajps5::loader::StaticTlsTemplateModule, 1> tls_modules = {
      {{1, tls_template_address, 8, 8, tls_module.module.static_offset}}};
  const auto tls_instance = kajps5::loader::CreateStaticTlsInstance(
      *memory, tls_layout, tls_modules, base + 0xd000);
  Check(tls_instance &&
            (tls_instance.instance.thread_pointer &
             (kajps5::loader::kStaticTlsThreadPointerAlignment - 1)) == 0 &&
            tls_instance.instance.dtv_address ==
                tls_instance.instance.thread_pointer +
                    kajps5::loader::kStaticTlsThreadControlBlockBytes,
        "static TLS instance creation failed");
  const auto thread_pointer = tls_instance.instance.thread_pointer;
  const auto dtv_address = tls_instance.instance.dtv_address;
  Check(ReadUInt64(*memory, thread_pointer) == thread_pointer &&
            ReadUInt64(*memory, thread_pointer + 8) == dtv_address &&
            ReadUInt64(*memory, thread_pointer + 0x10) == thread_pointer &&
            ReadUInt64(*memory, thread_pointer + 0x28) ==
                kajps5::loader::kStaticTlsStackGuard &&
            ReadUInt64(*memory, thread_pointer + 0x60) == thread_pointer &&
            ReadUInt64(*memory, dtv_address) == 1 &&
            ReadUInt64(*memory, dtv_address + 8) == 1 &&
            ReadUInt64(*memory, dtv_address + 16) == thread_pointer - 8,
        "static TLS control block or DTV is incorrect");
  Check(ReadUInt64(*memory, thread_pointer - 8) == 0x8877665544332211,
        "static TLS template was not copied to the thread block");

  const auto host_fs_before = kajps5::cpu::NativeGuestFsBaseSwitchSupported()
                                  ? HostFsBaseForTest()
                                  : 0;
  const auto tls_result = executor.ExecuteFunction(
      *memory, tls_code_address, stack_address, stack_size,
      std::span<const std::uint64_t>{}, &execution_context, thread_pointer);
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(tls_result.status == NativeGuestExecutionStatus::kOk &&
              tls_result.return_value == 0x8877665544332211,
          "guest code did not read its thread pointer through fs");
    Check(HostFsBaseForTest() == host_fs_before,
          "host FS base was not restored after guest return");
  } else {
    Check(tls_result.status == NativeGuestExecutionStatus::kUnsupportedHost,
          "unsupported host accepted guest FS-base switching");
  }

  Check(executor.ExecuteFunction(*memory, tls_code_address, stack_address,
                                 stack_size, std::span<const std::uint64_t>{},
                                 &execution_context, thread_pointer + 8)
                .status == NativeGuestExecutionStatus::kInvalidArgument,
        "unaligned thread pointer was accepted");

  const std::array second_template = {
      std::byte{0x22}, std::byte{0x33}, std::byte{0x44}, std::byte{0x55},
      std::byte{0x66}, std::byte{0x77}, std::byte{0x88}, std::byte{0x99}};
  Check(memory->Initialize(tls_template_address + 0x100, second_template),
        "second static TLS template installation failed");
  const std::array<kajps5::loader::StaticTlsTemplateModule, 1> second_modules =
      {{{1, tls_template_address + 0x100, 8, 8,
         tls_module.module.static_offset}}};
  const auto second_instance = kajps5::loader::CreateStaticTlsInstance(
      *memory, tls_layout, second_modules, base + 0xe000);
  Check(second_instance &&
            second_instance.instance.thread_pointer != thread_pointer,
        "second static TLS instance creation failed");
  const auto second_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto second_result = executor.ExecuteFunction(
      *memory, tls_code_address, stack_address, stack_size,
      std::span<const std::uint64_t>{}, &execution_context,
      second_instance.instance.thread_pointer);
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(second_result.status == NativeGuestExecutionStatus::kOk &&
              second_result.return_value == 0x9988776655443322,
          "second guest thread did not receive its own TLS copy");
    Check(HostFsBaseForTest() == second_host_fs_before,
          "host FS base was not restored after a second guest return");
  } else {
    Check(second_result.status == NativeGuestExecutionStatus::kUnsupportedHost,
          "unsupported host accepted a second guest FS-base switch");
  }

  auto bad_layout = tls_layout;
  const std::array<kajps5::loader::StaticTlsTemplateModule, 1> bad_modules = {
      {{1, tls_template_address, 8, 8, 16}}};
  Check(kajps5::loader::CreateStaticTlsInstance(*memory, bad_layout,
                                                bad_modules, base + 0xf000)
                .status == kajps5::loader::StaticTlsInstanceStatus::
                               kInvalidArgument,
        "mismatched static TLS offsets were accepted");
  const std::array<kajps5::loader::StaticTlsTemplateModule, 1>
      unmapped_template_modules = {
          {{1, base + 0xf800, 8, 8, tls_module.module.static_offset}}};
  Check(kajps5::loader::CreateStaticTlsInstance(*memory, tls_layout,
                                                unmapped_template_modules,
                                                base + 0xf000)
                .status == kajps5::loader::StaticTlsInstanceStatus::
                               kInvalidArgument,
        "unmapped static TLS template was accepted");

  Check(kajps5::loader::DestroyStaticTlsInstance(*memory,
                                                 second_instance.instance) &&
            !memory->IsMapped(second_instance.instance.thread_pointer, 1),
        "second static TLS instance was not released");

  kajps5::loader::StaticTlsLayout multi_tls_layout;
  const auto multi_first = multi_tls_layout.RegisterModule(1, 16, 64);
  const auto multi_second = multi_tls_layout.RegisterModule(2, 32, 32);
  const std::array<std::byte, 8> multi_first_template = {
      std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd},
      std::byte{0xee}, std::byte{0xff}, std::byte{0x12}, std::byte{0x34}};
  const std::array<std::byte, 8> multi_second_template = {
      std::byte{0x21}, std::byte{0x43}, std::byte{0x65}, std::byte{0x87},
      std::byte{0x09}, std::byte{0xba}, std::byte{0xdc}, std::byte{0xfe}};
  Check(multi_first && multi_second &&
            memory->Initialize(tls_template_address + 0x100,
                               multi_first_template) &&
            memory->Initialize(tls_template_address + 0x200,
                               multi_second_template),
        "multi-module static TLS fixture installation failed");
  const std::array<kajps5::loader::StaticTlsTemplateModule, 2> multi_modules = {
      {{1, tls_template_address + 0x100, 8, 16,
        multi_first.module.static_offset},
       {2, tls_template_address + 0x200, 8, 32,
        multi_second.module.static_offset}}};
  const auto multi_instance = kajps5::loader::CreateStaticTlsInstance(
      *memory, multi_tls_layout, multi_modules, base + 0xe000);
  const auto multi_tp = multi_instance.instance.thread_pointer;
  Check(multi_instance && (multi_tp & 63) == 0 &&
            ReadUInt64(*memory, multi_tp + 8) == multi_instance.instance.dtv_address &&
            ReadUInt64(*memory, multi_instance.instance.dtv_address + 16) ==
                multi_tp - multi_first.module.static_offset &&
            ReadUInt64(*memory, multi_instance.instance.dtv_address + 24) ==
                multi_tp - multi_second.module.static_offset &&
            ReadUInt64(*memory, multi_tp - multi_first.module.static_offset) ==
                0x3412ffeeddccbbaa &&
            ReadUInt64(*memory, multi_tp - multi_first.module.static_offset + 8) == 0 &&
            ReadUInt64(*memory, multi_tp - multi_second.module.static_offset) ==
                0xfedcba0987654321 &&
            ReadUInt64(*memory, multi_tp - multi_second.module.static_offset + 8) == 0 &&
            kajps5::loader::DestroyStaticTlsInstance(*memory,
                                                     multi_instance.instance),
        "multi-module static TLS fields, copy, zero-fill, or teardown failed");

  const auto hle_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto hle_thread_pointer =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? thread_pointer : 0;
  const auto hle =
      executor.Execute(*memory, hle_code_address, stack_address, stack_size,
                       parameters_address, 0, &execution_context,
                       hle_thread_pointer);
  const auto hle_snapshot = trampoline.last_dispatch();
  Check(hle.status == NativeGuestExecutionStatus::kOk &&
            hle.return_value == 42 &&
            hle_snapshot.lookup_status == ExportRegistryStatus::kOk &&
            hle_snapshot.handler_status == HleContextStatus::kOk &&
            nested_status == NativeGuestExecutionStatus::kInvalidArgument &&
            !execution_context.active(),
        "guest entry did not return through the checked HLE trampoline");
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(HostFsBaseForTest() == hle_host_fs_before,
          "host FS base was not restored after an HLE return");
  }

  const auto waiter = scheduler.CreateThread("native-waiter", 0);
  Check(waiter && scheduler.SelectNext() == waiter.handle,
        "native wait test thread did not start");
  const auto blocked_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto blocked =
      executor.Execute(*memory, blocked_hle_code_address, stack_address,
                       stack_size, parameters_address, 0, &execution_context,
                       hle_thread_pointer);
  const auto blocked_thread = scheduler.Snapshot(waiter.handle);
  Check(blocked.status == NativeGuestExecutionStatus::kHleBlocked &&
            NativeGuestExecutionStatusName(blocked.status) == "hle-blocked" &&
            blocked.hle_status == HleContextStatus::kBlocked &&
            execution_context.suspended() && blocked_thread &&
            blocked_thread->state == kajps5::kernel::GuestThreadState::kBlocked,
        "blocked HLE call did not suspend its guest continuation");
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(HostFsBaseForTest() == blocked_host_fs_before,
          "host FS base was not restored after an HLE block");
  }
  Check(executor.Execute(*memory, code_address, stack_address, stack_size,
                         parameters_address, 0, &execution_context)
                .status == NativeGuestExecutionStatus::kInvalidArgument,
        "suspended execution context accepted a new guest entry");
  kajps5::cpu::NativeGuestContinuation continuation;
  Check(executor.TakeContinuation(execution_context, continuation) &&
            continuation.valid() && !execution_context.suspended(),
        "blocked guest continuation did not leave the shared execution lane");
  const auto worker = scheduler.CreateThread("native-worker", 0);
  Check(worker && scheduler.SelectNext() == worker.handle,
        "second native guest thread did not start");
  const auto worker_result = executor.Execute(
      *memory, code_address, worker_stack_address, worker_stack_size,
      worker_parameters_address, 0, &execution_context);
  Check(worker_result.status == NativeGuestExecutionStatus::kOk &&
            worker_result.return_value == 0x1122334455667788 &&
            scheduler.ExitCurrent(worker_result.return_value),
        "shared execution lane did not run a second guest thread");
  auto unrelated_memory = GuestMemory::CreateHostMapped(0x10000);
  Check(unrelated_memory &&
            executor.Resume(*unrelated_memory, continuation, execution_context)
                    .status == NativeGuestExecutionStatus::kInvalidArgument &&
            continuation.valid() && !execution_context.suspended(),
        "guest continuation accepted a different memory owner");
  Check(scheduler.WakeBlockedThreads("native-hle-test", 1) == 1 &&
            scheduler.SelectNext() == waiter.handle,
        "blocked native guest thread did not wake");
  const auto resumed_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto resumed = executor.Resume(*memory, continuation, execution_context,
                                       hle_thread_pointer);
  Check(resumed.status == NativeGuestExecutionStatus::kOk &&
            resumed.return_value == 0x16a && blocking_dispatch_count == 2 &&
            !continuation.valid() && !execution_context.active() &&
            !execution_context.suspended(),
        "woken HLE call did not resume the saved guest continuation");
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(HostFsBaseForTest() == resumed_host_fs_before,
          "host FS base was not restored after an HLE resume");
  }
  Check(executor.Resume(*memory, execution_context).status ==
            NativeGuestExecutionStatus::kInvalidArgument,
        "completed guest continuation resumed twice");
  Check(scheduler.ExitCurrent(resumed.return_value),
        "resumed native guest thread did not exit cleanly");

  const auto yielding_thread = scheduler.CreateThread("native-yield", 0);
  const auto peer_thread = scheduler.CreateThread("native-peer", 0);
  Check(yielding_thread && peer_thread &&
            scheduler.SelectNext() == yielding_thread.handle,
        "native yield test threads did not start");
  const auto yielded_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto yielded =
      executor.Execute(*memory, yielded_hle_code_address, stack_address,
                       stack_size, parameters_address, 0, &execution_context,
                       hle_thread_pointer);
  kajps5::cpu::NativeGuestContinuation yielded_continuation;
  Check(
      yielded.status == NativeGuestExecutionStatus::kHleYielded &&
          NativeGuestExecutionStatusName(yielded.status) == "hle-yielded" &&
          executor.TakeContinuation(execution_context, yielded_continuation) &&
          scheduler.SelectNext() == peer_thread.handle,
      "yielding HLE call did not return to the guest scheduler");
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(HostFsBaseForTest() == yielded_host_fs_before,
          "host FS base was not restored after an HLE yield");
  }
  const auto peer_result = executor.Execute(
      *memory, code_address, worker_stack_address, worker_stack_size,
      worker_parameters_address, 0, &execution_context);
  Check(peer_result.status == NativeGuestExecutionStatus::kOk &&
            scheduler.ExitCurrent(peer_result.return_value) &&
            scheduler.SelectNext() == yielding_thread.handle,
        "native peer thread did not run between yield and resume");
  const auto yield_resumed_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto yield_resumed = executor.Resume(
      *memory, yielded_continuation, execution_context, hle_thread_pointer);
  Check(yield_resumed.status == NativeGuestExecutionStatus::kOk &&
            yield_resumed.return_value == 0x33 &&
            yielding_dispatch_count == 1 &&
            scheduler.ExitCurrent(yield_resumed.return_value),
      "yielded HLE call was dispatched twice or did not resume");
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(HostFsBaseForTest() == yield_resumed_host_fs_before,
          "host FS base was not restored after a yielded HLE resume");
  }

#if defined(_WIN32)
  const auto fault_host_fs_before =
      kajps5::cpu::NativeGuestFsBaseSwitchSupported() ? HostFsBaseForTest() : 0;
  const auto memory_fault =
      executor.Execute(*memory, memory_fault_code_address, stack_address,
                       stack_size, parameters_address, 0, &execution_context,
                       hle_thread_pointer);
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
  if (kajps5::cpu::NativeGuestFsBaseSwitchSupported()) {
    Check(HostFsBaseForTest() == fault_host_fs_before,
          "host FS base was not restored after a guest fault");
  }

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

  Check(kajps5::loader::DestroyStaticTlsInstance(*memory,
                                                 tls_instance.instance) &&
            !memory->IsMapped(thread_pointer, 1),
        "static TLS block remained mapped after release");

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
                         base + 0xd000, 0)
                .status ==
            NativeGuestExecutionStatus::kGuestParametersNotReadable,
        "unmapped guest entry parameters were accepted");

  if (failures == 0) {
    std::cout << "native guest executor tests passed\n";
  }
  return failures == 0 ? 0 : 1;
#endif
}
