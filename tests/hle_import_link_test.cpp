// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "core/memory/guest_memory.h"
#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/relocator.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_import_link_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ElfMetadata MakeImportMetadata() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.needed_libraries.push_back("libkernel");
  metadata.dynamic_info.symbols.resize(2);
  metadata.dynamic_info.symbols[1].name = "open";
  metadata.dynamic_info.relocations.push_back(
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0});
  metadata.dynamic_info.plt_relocations.push_back(
      {0x1008, (std::uint64_t{1} << 32U) | 7U, 0});
  return metadata;
}

kajps5::loader::ElfMetadata MakeSceImportMetadata(std::string symbol) {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.import_libraries.push_back(
      {0x1234, 0x0100, "libkernel"});
  metadata.dynamic_info.import_modules.push_back(
      {0x0040, 1, 2, "kernelModule"});
  metadata.dynamic_info.symbols.resize(2);
  metadata.dynamic_info.symbols[1].name = std::move(symbol);
  metadata.dynamic_info.relocations.push_back(
      {0x3000, (std::uint64_t{1} << 32U) | 6U, 0});
  return metadata;
}

}  // namespace

int main() {
  using kajps5::hle::ImportRegistry;
  using kajps5::hle::ImportRegistryStatus;
  using kajps5::loader::ApplyRelocations;
  using kajps5::loader::RelocationStatus;
  using kajps5::memory::GuestMemory;

  const auto metadata = MakeImportMetadata();
  ImportRegistry registry;
  Check(registry.Register("libkernel", "open", 0x1122334455667788) ==
            ImportRegistryStatus::kOk,
        "import registration failed");

  GuestMemory memory(0x1000, 16);
  const auto linked = ApplyRelocations(metadata, memory, registry);
  Check(linked && linked.applied_count == 2 &&
            linked.resolved_import_count == 2 &&
            linked.unresolved_import_count == 0,
        "resolved imports were not linked");
  std::array<std::byte, 16> values{};
  Check(memory.Read(0x1000, values), "linked targets could not be read");
  const std::array expected = {
      std::byte{0x88}, std::byte{0x77}, std::byte{0x66}, std::byte{0x55},
      std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11},
      std::byte{0x88}, std::byte{0x77}, std::byte{0x66}, std::byte{0x55},
      std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11}};
  Check(values == expected, "linked targets contain the wrong address");

  ImportRegistry empty_registry;
  GuestMemory unresolved_memory(0x1000, 16);
  const auto unresolved =
      ApplyRelocations(metadata, unresolved_memory, empty_registry);
  Check(unresolved && unresolved.applied_count == 0 &&
            unresolved.resolved_import_count == 0 &&
            unresolved.unresolved_import_count == 2 &&
            unresolved.unresolved_imports.size() == 2 &&
            unresolved.unresolved_imports[0].symbol == "open",
        "unresolved import diagnostics are incomplete");
  std::array<std::byte, 16> unchanged{};
  Check(unresolved_memory.Read(0x1000, unchanged) &&
            unchanged == std::array<std::byte, 16>{},
        "unresolved imports changed guest memory");

  ImportRegistry wrong_library;
  Check(wrong_library.Register("compat", "open", 0x2000) ==
            ImportRegistryStatus::kOk,
        "wrong-library setup failed");
  Check(ApplyRelocations(metadata, unresolved_memory, wrong_library)
            .unresolved_import_count == 2,
        "import lookup escaped needed-library scope");

  GuestMemory biased_memory(0x2000, 16);
  const auto biased =
      ApplyRelocations(metadata, biased_memory, empty_registry, 0x1000);
  Check(biased && biased.unresolved_imports.size() == 2 &&
            biased.unresolved_imports[0].target_address == 0x2000 &&
            biased.unresolved_imports[1].target_address == 0x2008,
        "unresolved import targets did not include the load bias");

  auto invalid_symbol = metadata;
  invalid_symbol.dynamic_info.relocations[0].info =
      (std::uint64_t{2} << 32U) | 6U;
  Check(ApplyRelocations(invalid_symbol, unresolved_memory, registry).status ==
            RelocationStatus::kInvalidSymbolIndex,
        "invalid import symbol index was accepted");

  auto unsupported = metadata;
  unsupported.dynamic_info.plt_relocations.push_back({0x1000, 2, 0});
  GuestMemory transactional_memory(0x1000, 16);
  Check(ApplyRelocations(unsupported, transactional_memory, registry).status ==
            RelocationStatus::kUnsupportedRelocation,
        "unsupported linked relocation was accepted");
  Check(transactional_memory.Read(0x1000, unchanged) &&
            unchanged == std::array<std::byte, 16>{},
        "failed import link changed guest memory");

  ImportRegistry sce_registry;
  Check(sce_registry.Register("libkernel", "open", 0x8877665544332211) ==
            ImportRegistryStatus::kOk,
        "SCE import registration failed");
  GuestMemory sce_memory(0x3000, 8);
  const auto sce_linked = ApplyRelocations(
      MakeSceImportMetadata("open#BI0#BA"), sce_memory, sce_registry);
  Check(sce_linked && sce_linked.applied_count == 1 &&
            sce_linked.resolved_import_count == 1 &&
            sce_linked.unresolved_import_count == 0,
        "scoped SCE import was not linked");
  std::array<std::byte, 8> sce_value{};
  const std::array expected_sce_value = {
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
      std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88}};
  Check(sce_memory.Read(0x3000, sce_value) && sce_value == expected_sce_value,
        "scoped SCE import wrote the wrong address");

  auto absolute_import = MakeImportMetadata();
  absolute_import.dynamic_info.relocations = {
      {0x4000, (std::uint64_t{1} << 32U) | 1U, 5}};
  absolute_import.dynamic_info.plt_relocations.clear();
  GuestMemory absolute_memory(0x4000, 8);
  const auto absolute_linked =
      ApplyRelocations(absolute_import, absolute_memory, registry);
  std::array<std::byte, 8> absolute_value{};
  const std::array expected_absolute_value = {
      std::byte{0x8d}, std::byte{0x77}, std::byte{0x66}, std::byte{0x55},
      std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11}};
  Check(absolute_linked && absolute_linked.applied_count == 1 &&
            absolute_linked.resolved_import_count == 1 &&
            absolute_memory.Read(0x4000, absolute_value) &&
            absolute_value == expected_absolute_value,
        "absolute import relocation did not apply its addend");

  kajps5::loader::ElfMetadata defined_symbol;
  defined_symbol.dynamic_info.symbols.resize(2);
  defined_symbol.dynamic_info.symbols[1].info = 0x10;
  defined_symbol.dynamic_info.symbols[1].section_index = 1;
  defined_symbol.dynamic_info.symbols[1].value = 0x200;
  defined_symbol.dynamic_info.relocations.push_back(
      {0x4000, (std::uint64_t{1} << 32U) | 1U, 0x10});
  GuestMemory defined_memory(0x5000, 8);
  const auto defined_linked =
      ApplyRelocations(defined_symbol, defined_memory, empty_registry, 0x1000);
  std::array<std::byte, 8> defined_value{};
  const std::array expected_defined_value = {
      std::byte{0x10}, std::byte{0x12}, std::byte{0}, std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0}, std::byte{0}};
  Check(defined_linked && defined_linked.applied_count == 1 &&
            defined_linked.resolved_import_count == 0 &&
            defined_memory.Read(0x5000, defined_value) &&
            defined_value == expected_defined_value,
        "defined absolute symbol did not use the load bias and addend");

  kajps5::loader::ElfMetadata weak_symbol;
  weak_symbol.dynamic_info.symbols.resize(2);
  weak_symbol.dynamic_info.symbols[1].info = 0x20;
  weak_symbol.dynamic_info.symbols[1].name = "optional";
  weak_symbol.dynamic_info.relocations.push_back(
      {0x6000, (std::uint64_t{1} << 32U) | 1U, 7});
  GuestMemory weak_memory(0x6000, 8);
  const auto weak_linked =
      ApplyRelocations(weak_symbol, weak_memory, empty_registry);
  std::array<std::byte, 8> weak_value{};
  const std::array expected_weak_value = {
      std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  Check(weak_linked && weak_linked.applied_count == 1 &&
            weak_linked.unresolved_import_count == 0 &&
            weak_memory.Read(0x6000, weak_value) &&
            weak_value == expected_weak_value,
        "unresolved weak absolute symbol did not use zero plus its addend");

  for (const auto malformed : {"open#A#BA", "open#AI0#BA",
                               "open#BI0#A", "open#BI0#B!",
                               "open#BI0#BA#extra"}) {
    GuestMemory rejected_memory(0x3000, 8);
    const auto rejected = ApplyRelocations(
        MakeSceImportMetadata(malformed), rejected_memory, sce_registry);
    Check(rejected && rejected.applied_count == 0 &&
              rejected.resolved_import_count == 0 &&
              rejected.unresolved_import_count == 1,
          "invalid SCE import scope escaped metadata validation");
    Check(rejected_memory.Read(0x3000, sce_value) &&
              sce_value == std::array<std::byte, 8>{},
          "invalid SCE import scope changed guest memory");
  }

  GuestMemory empty_nid_memory(0x3000, 8);
  Check(ApplyRelocations(MakeSceImportMetadata("#BI0#BA"), empty_nid_memory,
                         sce_registry)
            .status == RelocationStatus::kEmptyImportSymbol,
        "an empty SCE NID was accepted");

  Check(kajps5::loader::RelocationStatusName(
            RelocationStatus::kInvalidSymbolIndex) == "invalid-symbol-index",
        "import-link status name is unstable");
  return failures == 0 ? 0 : 1;
}
