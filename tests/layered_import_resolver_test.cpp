// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include "core/memory/guest_memory.h"
#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/layered_import_resolver.h"
#include "loader/module_export_registry.h"
#include "loader/relocator.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "layered_import_resolver_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ElfMetadata MakeExportModule() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.export_libraries.push_back(
      {0x1234, 0x0100, "libGame"});
  metadata.dynamic_info.export_modules.push_back(
      {0x0040, 1, 2, "gameModule"});
  metadata.dynamic_info.symbols.resize(2);
  auto& symbol = metadata.dynamic_info.symbols[1];
  symbol.info = 0x12;
  symbol.section_index = 1;
  symbol.value = 0x200;
  symbol.name = "shared#BI0#BA";
  return metadata;
}

kajps5::loader::ElfMetadata MakeImportModule(std::string symbol) {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.import_libraries.push_back(
      {0x1234, 0x0100, "libGame"});
  metadata.dynamic_info.import_modules.push_back(
      {0x0040, 1, 2, "gameModule"});
  metadata.dynamic_info.symbols.resize(2);
  metadata.dynamic_info.symbols[1].name = std::move(symbol) + "#BI0#BA";
  metadata.dynamic_info.relocations.push_back(
      {0x2000, (std::uint64_t{1} << 32U) | 7U, 0});
  return metadata;
}

std::uint64_t Read64(const kajps5::memory::GuestMemory& memory) {
  std::array<std::byte, sizeof(std::uint64_t)> bytes{};
  if (!memory.Read(0x2000, bytes)) {
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
  using kajps5::hle::ImportRegistry;
  using kajps5::hle::ImportRegistryStatus;
  using kajps5::loader::ApplyRelocations;
  using kajps5::loader::LayeredImportResolver;
  using kajps5::loader::ModuleExportRegistry;
  using kajps5::memory::GuestMemory;

  ModuleExportRegistry modules;
  const auto producer = MakeExportModule();
  Check(static_cast<bool>(modules.RegisterModule(producer, 0x1000)),
        "module export setup failed");

  ImportRegistry hle;
  Check(hle.Register("libGame", "shared", 0x9000) ==
                ImportRegistryStatus::kOk &&
            hle.Register("libGame", "hleOnly", 0xa000) ==
                ImportRegistryStatus::kOk,
        "HLE fallback setup failed");
  const LayeredImportResolver resolver(modules, hle);

  GuestMemory module_memory(0x2000, 8);
  const auto module_link =
      ApplyRelocations(MakeImportModule("shared"), module_memory, resolver);
  Check(module_link && module_link.resolved_import_count == 1 &&
            module_link.unresolved_import_count == 0 &&
            Read64(module_memory) == 0x1200,
        "HLE fallback replaced an exact module export");

  GuestMemory hle_memory(0x2000, 8);
  const auto hle_link =
      ApplyRelocations(MakeImportModule("hleOnly"), hle_memory, resolver);
  Check(hle_link && hle_link.resolved_import_count == 1 &&
            hle_link.unresolved_import_count == 0 &&
            Read64(hle_memory) == 0xa000,
        "missing module export did not use the HLE fallback");

  GuestMemory unresolved_memory(0x2000, 8);
  const auto unresolved = ApplyRelocations(
      MakeImportModule("missing"), unresolved_memory, resolver);
  Check(unresolved && unresolved.applied_count == 0 &&
            unresolved.unresolved_import_count == 1 &&
            Read64(unresolved_memory) == 0,
        "unresolved layered import changed guest memory");

  const std::array<std::string, 1> library_scope = {"libGame"};
  Check(resolver.ResolveImport("shared", library_scope) == 0x1200 &&
            resolver.ResolveImport("hleOnly", library_scope) == 0xa000 &&
            !resolver.ResolveImport("missing", library_scope).has_value(),
        "unscoped layered lookup returned the wrong source");

  return failures == 0 ? 0 : 1;
}
