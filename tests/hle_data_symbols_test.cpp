// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/data_symbols.h"
#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/relocator.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_data_symbols_test: " << message << '\n';
    ++failures;
  }
}

template <typename Value>
Value ReadLittleEndian(kajps5::memory::GuestMemory& memory,
                       std::uint64_t address) {
  std::array<std::byte, sizeof(Value)> bytes{};
  if (!memory.Read(address, bytes)) {
    ++failures;
    return 0;
  }
  Value value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<Value>(std::to_integer<std::uint8_t>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

kajps5::loader::ElfMetadata MakeDataImportMetadata() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.import_libraries.push_back(
      {0x1234, 0x0100, "libc"});
  metadata.dynamic_info.import_modules.push_back(
      {0x0040, 1, 2, "libcModule"});
  metadata.dynamic_info.symbols.resize(2);
  metadata.dynamic_info.symbols[1].name =
      std::string(kajps5::hle::kHleLibcNeedFlagNid) + "#BI0#BA";
  metadata.dynamic_info.relocations.push_back(
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0});
  return metadata;
}

}  // namespace

int main() {
  using kajps5::hle::HleDataStatus;
  using kajps5::hle::ImportRegistry;
  using kajps5::hle::ImportRegistryStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(0x1000, 0x9000, GuestMemoryProtection::kNone);
  Check(memory.Map(0x1000, 8,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "relocation target setup failed");
  ImportRegistry registry;
  const auto installed =
      kajps5::hle::InstallHleDataSymbols(registry, memory, 0x4000, {});
  Check(installed && installed.page_address == 0x4000 &&
            registry.size() == 4,
        "default data-symbol page did not install");
  Check(ReadLittleEndian<std::uint64_t>(memory,
                                        installed.stack_guard_address) ==
                kajps5::hle::kHleStackGuardValue &&
            ReadLittleEndian<std::uint64_t>(
                memory, installed.stack_guard_address + 8) ==
                kajps5::hle::kHleStackGuardValue,
        "stack guard copies are incorrect");
  Check(ReadLittleEndian<std::uint32_t>(
            memory, installed.libc_need_flag_address) == 1 &&
            ReadLittleEndian<std::uint32_t>(
                memory, installed.libc_internal_need_flag_address) == 1,
        "libc need flags are incorrect");
  const auto program_name = ReadLittleEndian<std::uint64_t>(
      memory, installed.program_name_pointer_address);
  std::array<std::byte, 10> default_name{};
  Check(memory.Read(program_name, default_name) &&
            default_name[0] == std::byte{'e'} &&
            default_name[8] == std::byte{'n'} &&
            default_name[9] == std::byte{0},
        "default process name is not guest-owned or terminated");

  const std::vector<std::string> kernel_scope = {"libkernel"};
  const std::vector<std::string> libc_scope = {"libc"};
  const std::vector<std::string> internal_scope = {"LibcInternal"};
  Check(registry.Resolve(kajps5::hle::kHleStackGuardNid, kernel_scope)
                .target_address == installed.stack_guard_address &&
            registry.Resolve(kajps5::hle::kHleProgramNameNid, kernel_scope)
                .target_address == installed.program_name_pointer_address &&
            registry.Resolve(kajps5::hle::kHleLibcNeedFlagNid, libc_scope)
                .target_address == installed.libc_need_flag_address &&
            registry
                    .Resolve(kajps5::hle::kHleLibcInternalNeedFlagNid,
                             internal_scope)
                    .target_address ==
                installed.libc_internal_need_flag_address,
        "data symbols do not use their exact library scopes");

  const auto relocated = kajps5::loader::ApplyRelocations(
      MakeDataImportMetadata(), memory, registry);
  Check(relocated && relocated.resolved_import_count == 1 &&
            relocated.unresolved_import_count == 0 &&
            ReadLittleEndian<std::uint64_t>(memory, 0x1000) ==
                installed.libc_need_flag_address,
        "scoped data import did not relocate to guest memory");

  GuestMemory long_name_memory(0x10000, 0x8000,
                               GuestMemoryProtection::kNone);
  ImportRegistry long_name_registry;
  const std::string long_name(600, 'x');
  const auto long_name_installed = kajps5::hle::InstallHleDataSymbols(
      long_name_registry, long_name_memory, 0x14000, long_name);
  const auto long_name_address = ReadLittleEndian<std::uint64_t>(
      long_name_memory, long_name_installed.program_name_pointer_address);
  std::array<std::byte, 512> bounded_name{};
  Check(long_name_installed &&
            long_name_memory.Read(long_name_address, bounded_name) &&
            bounded_name[510] == std::byte{'x'} &&
            bounded_name[511] == std::byte{0},
        "long process name was not bounded and terminated");

  GuestMemory conflict_memory(0x20000, 0x8000,
                              GuestMemoryProtection::kNone);
  ImportRegistry conflict_registry;
  Check(conflict_registry.Register("libkernel",
                                   kajps5::hle::kHleStackGuardNid, 0x2222) ==
            ImportRegistryStatus::kOk,
        "registry conflict setup failed");
  const auto conflict = kajps5::hle::InstallHleDataSymbols(
      conflict_registry, conflict_memory, 0x24000);
  Check(conflict.status == HleDataStatus::kRegistryConflict &&
            conflict_registry.size() == 1 &&
            !conflict_memory.IsMapped(0x24000,
                                      kajps5::hle::kHleDataPageSize),
        "registry conflict left a partial guest page or import batch");

  GuestMemory invalid_memory(0x30000, 0x8000,
                             GuestMemoryProtection::kNone);
  ImportRegistry invalid_registry;
  const std::string embedded_null("x\0y", 3);
  Check(kajps5::hle::InstallHleDataSymbols(
            invalid_registry, invalid_memory, 0x30001)
                .status == HleDataStatus::kInvalidArgument &&
            kajps5::hle::InstallHleDataSymbols(
                invalid_registry, invalid_memory, 0x34000, embedded_null)
                    .status == HleDataStatus::kInvalidArgument &&
            invalid_registry.size() == 0,
        "invalid data-symbol input changed guest state");

  Check(kajps5::hle::HleDataStatusName(HleDataStatus::kRegistryConflict) ==
            "registry-conflict",
        "data-symbol status name is incorrect");

  return failures == 0 ? 0 : 1;
}
