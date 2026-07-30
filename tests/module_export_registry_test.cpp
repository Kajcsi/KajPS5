// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "core/memory/guest_memory.h"
#include "loader/elf.h"
#include "loader/module_export_registry.h"
#include "loader/relocator.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "module_export_registry_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ElfMetadata MakeExportModule(
    std::uint16_t module_id, std::string module_name,
    std::string encoded_module_id, std::uint64_t symbol_value) {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.export_libraries.push_back(
      {0x1234, 0x0100, "libGame"});
  metadata.dynamic_info.export_modules.push_back(
      {module_id, 1, 2, std::move(module_name)});
  metadata.dynamic_info.symbols.resize(2);
  auto& symbol = metadata.dynamic_info.symbols[1];
  symbol.info = 0x12;
  symbol.section_index = 1;
  symbol.value = symbol_value;
  symbol.name = "call#BI0#" + encoded_module_id;
  return metadata;
}

kajps5::loader::ElfMetadata MakeImportModule() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.import_libraries.push_back(
      {0x1234, 0x0100, "libGame"});
  metadata.dynamic_info.import_modules.push_back(
      {0x0040, 1, 2, "gameModule"});
  metadata.dynamic_info.symbols.resize(2);
  metadata.dynamic_info.symbols[1].info = 0x12;
  metadata.dynamic_info.symbols[1].name = "call#BI0#BA";
  metadata.dynamic_info.relocations.push_back(
      {0x2000, (std::uint64_t{1} << 32U) | 7U, 0});
  return metadata;
}

std::uint64_t Read64(const kajps5::memory::GuestMemory& memory,
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
  using kajps5::loader::ApplyRelocations;
  using kajps5::loader::ModuleExportRegistry;
  using kajps5::loader::ModuleExportRegistryStatus;
  using kajps5::memory::GuestMemory;

  ModuleExportRegistry registry;
  const auto producer = MakeExportModule(0x0040, "gameModule", "BA", 0x200);
  const auto registered = registry.RegisterModule(producer, 0x1000);
  Check(registered && registered.registered_count == 1 &&
            registered.skipped_count == 1 && registry.size() == 1,
        "valid module export was not registered");
  Check(registry.ResolveScopedImport(
            "call", producer.dynamic_info.export_libraries[0],
            producer.dynamic_info.export_modules[0]) == 0x1200,
        "exact module export lookup failed");

  const auto consumer = MakeImportModule();
  GuestMemory memory(0x2000, 8);
  const auto linked = ApplyRelocations(consumer, memory, registry);
  Check(linked && linked.applied_count == 1 &&
            linked.resolved_import_count == 1 &&
            linked.unresolved_import_count == 0 &&
            Read64(memory, 0x2000) == 0x1200,
        "consumer module did not link to the producer export");

  auto wrong_version = consumer;
  ++wrong_version.dynamic_info.import_libraries[0].version;
  GuestMemory wrong_version_memory(0x2000, 8);
  const auto wrong_version_link =
      ApplyRelocations(wrong_version, wrong_version_memory, registry);
  Check(wrong_version_link && wrong_version_link.applied_count == 0 &&
            wrong_version_link.unresolved_import_count == 1,
        "module export lookup ignored the library version");

  const std::array<std::string, 1> library_scope = {"libGame"};
  Check(registry.ResolveImport("call", library_scope) == 0x1200,
        "unique library-scoped export lookup failed");

  const auto second =
      MakeExportModule(0x0041, "otherModule", "BB", 0x300);
  const auto second_registered = registry.RegisterModule(second, 0x1000);
  Check(second_registered && second_registered.registered_count == 1 &&
            registry.size() == 2,
        "second module export was not registered");
  Check(registry.ResolveScopedImport(
            "call", second.dynamic_info.export_libraries[0],
            second.dynamic_info.export_modules[0]) == 0x1300,
        "second exact module export lookup failed");
  Check(!registry.ResolveImport("call", library_scope).has_value() &&
            !registry.ResolveImport("call", {}).has_value(),
        "ambiguous unscoped module export was resolved");

  auto duplicate = producer;
  duplicate.dynamic_info.symbols.resize(3);
  duplicate.dynamic_info.symbols[1].name = "new#BI0#BA";
  duplicate.dynamic_info.symbols[1].value = 0x400;
  duplicate.dynamic_info.symbols[2] = producer.dynamic_info.symbols[1];
  const auto duplicate_result = registry.RegisterModule(duplicate, 0x1000);
  Check(duplicate_result.status ==
                ModuleExportRegistryStatus::kDuplicateExport &&
            duplicate_result.registered_count == 0 &&
            duplicate_result.failed_symbol_index == 2 &&
            registry.size() == 2 &&
            !registry.ResolveScopedImport(
                 "new", producer.dynamic_info.export_libraries[0],
                 producer.dynamic_info.export_modules[0])
                 .has_value(),
        "duplicate module registration was not transactional");

  auto overflow = producer;
  overflow.dynamic_info.symbols[1].name = "overflow#BI0#BA";
  overflow.dynamic_info.symbols[1].value =
      std::numeric_limits<std::uint64_t>::max();
  const auto overflow_result = registry.RegisterModule(overflow, 1);
  Check(overflow_result.status ==
                ModuleExportRegistryStatus::kAddressOverflow &&
            overflow_result.failed_symbol_index == 1 && registry.size() == 2,
        "overflowing module export address was accepted");

  auto filtered = producer;
  filtered.dynamic_info.symbols.resize(7);
  filtered.dynamic_info.symbols[1].info = 0x02;
  filtered.dynamic_info.symbols[1].value = 0x100;
  filtered.dynamic_info.symbols[1].name = "local#BI0#BA";
  filtered.dynamic_info.symbols[2].info = 0x13;
  filtered.dynamic_info.symbols[2].value = 0x100;
  filtered.dynamic_info.symbols[2].name = "section#BI0#BA";
  filtered.dynamic_info.symbols[3].info = 0x12;
  filtered.dynamic_info.symbols[3].name = "import#BI0#BA";
  filtered.dynamic_info.symbols[4].info = 0x12;
  filtered.dynamic_info.symbols[4].value = 0x100;
  filtered.dynamic_info.symbols[4].name = "malformed";
  filtered.dynamic_info.symbols[5].info = 0x12;
  filtered.dynamic_info.symbols[5].value = 0x100;
  filtered.dynamic_info.symbols[5].name = "unknown#BI0#BC";
  filtered.dynamic_info.symbols[6].info = 0x22;
  filtered.dynamic_info.symbols[6].value = 0x500;
  filtered.dynamic_info.symbols[6].name = "weak#BI0#BA";
  const auto filtered_result = registry.RegisterModule(filtered, 0x1000);
  Check(filtered_result && filtered_result.registered_count == 1 &&
            filtered_result.skipped_count == 6 && registry.size() == 3 &&
            registry.ResolveScopedImport(
                "weak", filtered.dynamic_info.export_libraries[0],
                filtered.dynamic_info.export_modules[0]) == 0x1500,
        "module export filtering rejected or exposed the wrong symbols");

  Check(kajps5::loader::ModuleExportRegistryStatusName(
            ModuleExportRegistryStatus::kDuplicateExport) ==
            "duplicate-export",
        "module export status name is unstable");
  return failures == 0 ? 0 : 1;
}
