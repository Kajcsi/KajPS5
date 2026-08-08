// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_hle_import_table.h"

#include <vector>

#include "hle/import_coverage.h"
#include "loader/sce_symbol.h"

namespace kajps5::cpu {

NativeHleImportTable::NativeHleImportTable(
    memory::GuestMemory& memory, const hle::ExportRegistry& registry,
    NativeGuestExecutionContext* execution_context) noexcept
    : memory_(memory),
      registry_(registry),
      execution_context_(execution_context) {}

NativeHleImportTableResult NativeHleImportTable::Build(
    const loader::ElfMetadata& metadata, std::size_t stack_argument_count) {
  const loader::ElfMetadata* metadata_pointer = &metadata;
  return BuildBatch(
      std::span<const loader::ElfMetadata* const>(&metadata_pointer, 1),
      stack_argument_count);
}

NativeHleImportTableResult NativeHleImportTable::BuildBatch(
    std::span<const loader::ElfMetadata* const> metadata,
    std::size_t stack_argument_count) {
  NativeHleImportTableResult result;
  if (built_) {
    result.status = NativeHleImportTableStatus::kAlreadyBuilt;
    return result;
  }
  if (metadata.size() > kMaximumNativeHleImages) {
    result.status = NativeHleImportTableStatus::kCapacityExceeded;
    return result;
  }

  std::map<Key, std::unique_ptr<NativeHleTrampoline>> pending;
  std::vector<hle::ImportDefinition> definitions;
  for (const auto* image_metadata : metadata) {
    if (image_metadata == nullptr) {
      result.status = NativeHleImportTableStatus::kInvalidMetadata;
      return result;
    }
    const auto coverage =
        hle::AnalyzeImportCoverage(*image_metadata, registry_);
    if (coverage.unique_import_count >
        kMaximumNativeHleImports - result.import_count) {
      result.status = NativeHleImportTableStatus::kCapacityExceeded;
      return result;
    }
    result.import_count += coverage.unique_import_count;
    if (!coverage) {
      result.status =
          coverage.status == hle::ImportCoverageStatus::kInvalidSymbolIndex
              ? NativeHleImportTableStatus::kInvalidSymbolIndex
              : NativeHleImportTableStatus::kEmptyImportSymbol;
      return result;
    }

    if (coverage.resolved_unique_import_count >
        kMaximumNativeHleImports - definitions.size()) {
      result.status = NativeHleImportTableStatus::kCapacityExceeded;
      return result;
    }
    definitions.reserve(definitions.size() +
                        coverage.resolved_unique_import_count);
    for (const auto& entry : coverage.imports) {
      const auto& symbol =
          image_metadata->dynamic_info.symbols[entry.symbol_index];
      const auto reference =
          loader::ResolveElfImportReference(*image_metadata, symbol.name);
      if (!reference.valid) {
        ++result.unresolved_import_count;
        continue;
      }

      std::vector<std::string> requested_libraries;
      if (reference.library != nullptr && reference.module != nullptr) {
        requested_libraries.push_back(reference.library->name);
      }
      const std::span<const std::string> library_order =
          requested_libraries.empty()
              ? std::span<const std::string>(
                    image_metadata->dynamic_info.needed_libraries)
              : std::span<const std::string>(requested_libraries);
      const auto lookup = registry_.Lookup(reference.nid, library_order);
      if (!lookup) {
        ++result.unresolved_import_count;
        continue;
      }

      ++result.resolved_import_count;
      Key key{lookup.library, std::string(reference.nid)};
      if (pending.contains(key)) {
        continue;
      }

      auto trampoline = std::make_unique<NativeHleTrampoline>(
          memory_, registry_, key.second, std::vector<std::string>{key.first},
          stack_argument_count, execution_context_);
      if (trampoline->status() != NativeHleTrampolineStatus::kOk) {
        result.status = NativeHleImportTableStatus::kTrampolineBuildFailed;
        result.trampoline_status = trampoline->status();
        return result;
      }
      definitions.push_back({key.first, key.second, trampoline->address()});
      pending.emplace(std::move(key), std::move(trampoline));
    }
  }

  if (!definitions.empty()) {
    result.registry_status = targets_.RegisterBatch(std::move(definitions));
    if (result.registry_status != hle::ImportRegistryStatus::kOk) {
      result.status = NativeHleImportTableStatus::kRegistryFailure;
      return result;
    }
  }
  result.trampoline_count = pending.size();
  trampolines_ = std::move(pending);
  built_ = true;
  return result;
}

std::optional<std::uint64_t> NativeHleImportTable::ResolveImport(
    std::string_view symbol, std::span<const std::string> library_order) const {
  return targets_.ResolveImport(symbol, library_order);
}

const NativeHleTrampoline* NativeHleImportTable::Find(
    std::string_view symbol, std::span<const std::string> library_order) const {
  const auto target = targets_.Resolve(symbol, library_order);
  if (!target) {
    return nullptr;
  }
  const auto found =
      trampolines_.find(Key{target.library, std::string(symbol)});
  return found == trampolines_.end() ? nullptr : found->second.get();
}

std::optional<NativeHleDispatchSnapshot>
NativeHleImportTable::active_dispatch() const {
  for (const auto& [key, trampoline] : trampolines_) {
    (void)key;
    const auto snapshot = trampoline->active_dispatch();
    if (snapshot.active) {
      return snapshot;
    }
  }
  return std::nullopt;
}

std::size_t NativeHleImportTable::size() const noexcept {
  return trampolines_.size();
}

std::string_view NativeHleImportTableStatusName(
    NativeHleImportTableStatus status) noexcept {
  switch (status) {
    case NativeHleImportTableStatus::kOk:
      return "ok";
    case NativeHleImportTableStatus::kAlreadyBuilt:
      return "already-built";
    case NativeHleImportTableStatus::kInvalidMetadata:
      return "invalid-metadata";
    case NativeHleImportTableStatus::kCapacityExceeded:
      return "capacity-exceeded";
    case NativeHleImportTableStatus::kInvalidSymbolIndex:
      return "invalid-symbol-index";
    case NativeHleImportTableStatus::kEmptyImportSymbol:
      return "empty-import-symbol";
    case NativeHleImportTableStatus::kTrampolineBuildFailed:
      return "trampoline-build-failed";
    case NativeHleImportTableStatus::kRegistryFailure:
      return "registry-failure";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
