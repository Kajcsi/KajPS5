// Copyright (C) 2026 KajPS5 contributors
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

  Check(kajps5::loader::RelocationStatusName(
            RelocationStatus::kInvalidSymbolIndex) == "invalid-symbol-index",
        "import-link status name is unstable");
  return failures == 0 ? 0 : 1;
}
