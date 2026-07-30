// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/module_export_registry.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "loader/sce_symbol.h"

namespace kajps5::loader {
namespace {

constexpr std::uint8_t kSymbolBindingGlobal = 1;
constexpr std::uint8_t kSymbolBindingWeak = 2;
constexpr std::uint8_t kSymbolTypeNone = 0;
constexpr std::uint8_t kSymbolTypeObject = 1;
constexpr std::uint8_t kSymbolTypeFunction = 2;
constexpr std::size_t kMaximumScopeNameLength = 127;
constexpr std::size_t kMaximumSymbolNameLength = 255;

bool IsValidName(std::string_view name, std::size_t maximum_length) noexcept {
  return !name.empty() && name.size() <= maximum_length &&
         name.find('\0') == std::string_view::npos;
}

bool IsExportSymbol(const ElfSymbol& symbol) noexcept {
  const auto binding = symbol.binding();
  const auto type = symbol.type();
  return symbol.value != 0 &&
         (binding == kSymbolBindingGlobal || binding == kSymbolBindingWeak) &&
         (type == kSymbolTypeNone || type == kSymbolTypeObject ||
          type == kSymbolTypeFunction);
}

template <typename Identity>
const Identity* FindIdentity(std::uint16_t id,
                             const std::vector<Identity>& identities) {
  for (const auto& identity : identities) {
    if (identity.id == id) {
      return &identity;
    }
  }
  return nullptr;
}

}  // namespace

ModuleExportRegistrationResult ModuleExportRegistry::RegisterModule(
    const ElfMetadata& metadata, std::uint64_t load_bias) {
  struct CandidateExport {
    Key key;
    std::uint64_t address = 0;
    std::uint32_t symbol_index = 0;
  };
  std::vector<CandidateExport> candidates;
  candidates.reserve(std::min(metadata.dynamic_info.symbols.size(),
                              kMaximumModuleExports));
  std::size_t skipped_count = 0;

  for (std::size_t index = 0;
       index < metadata.dynamic_info.symbols.size(); ++index) {
    const auto& symbol = metadata.dynamic_info.symbols[index];
    if (!IsExportSymbol(symbol)) {
      ++skipped_count;
      continue;
    }
    const auto reference = ParseSceSymbolReference(symbol.name);
    if (!reference.has_value() ||
        !IsValidName(reference->nid, kMaximumSymbolNameLength)) {
      ++skipped_count;
      continue;
    }
    const auto* library = FindIdentity(
        reference->library_id, metadata.dynamic_info.export_libraries);
    const auto* module = FindIdentity(
        reference->module_id, metadata.dynamic_info.export_modules);
    if (library == nullptr || module == nullptr ||
        !IsValidName(library->name, kMaximumScopeNameLength) ||
        !IsValidName(module->name, kMaximumScopeNameLength)) {
      ++skipped_count;
      continue;
    }
    if (symbol.value >
        std::numeric_limits<std::uint64_t>::max() - load_bias) {
      return {ModuleExportRegistryStatus::kAddressOverflow, 0, skipped_count,
              static_cast<std::uint32_t>(index)};
    }
    if (candidates.size() == kMaximumModuleExports) {
      return {ModuleExportRegistryStatus::kCapacityExceeded, 0,
              skipped_count, static_cast<std::uint32_t>(index)};
    }

    CandidateExport candidate;
    candidate.key = {library->name,
                     library->version,
                     module->name,
                     module->version_major,
                     module->version_minor,
                     std::string(reference->nid)};
    candidate.address = load_bias + symbol.value;
    candidate.symbol_index = static_cast<std::uint32_t>(index);
    candidates.push_back(std::move(candidate));
  }

  std::lock_guard lock(mutex_);
  if (entries_.size() > kMaximumModuleExports ||
      candidates.size() > kMaximumModuleExports - entries_.size()) {
    return {ModuleExportRegistryStatus::kCapacityExceeded, 0, skipped_count,
            std::nullopt};
  }
  auto updated = entries_;
  for (auto& candidate : candidates) {
    const auto [entry, inserted] =
        updated.emplace(std::move(candidate.key), candidate.address);
    (void)entry;
    if (!inserted) {
      return {ModuleExportRegistryStatus::kDuplicateExport, 0,
              skipped_count, candidate.symbol_index};
    }
  }
  entries_.swap(updated);
  return {ModuleExportRegistryStatus::kOk, candidates.size(), skipped_count,
          std::nullopt};
}

std::optional<std::uint64_t> ModuleExportRegistry::ResolveImport(
    std::string_view symbol,
    std::span<const std::string> library_order) const {
  if (!IsValidName(symbol, kMaximumSymbolNameLength)) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);
  for (const auto& library : library_order) {
    if (!IsValidName(library, kMaximumScopeNameLength)) {
      return std::nullopt;
    }
    std::optional<std::uint64_t> found;
    for (const auto& [key, address] : entries_) {
      if (std::get<0>(key) != library || std::get<5>(key) != symbol) {
        continue;
      }
      if (found.has_value()) {
        return std::nullopt;
      }
      found = address;
    }
    if (found.has_value()) {
      return found;
    }
  }
  if (!library_order.empty()) {
    return std::nullopt;
  }

  std::optional<std::uint64_t> found;
  for (const auto& [key, address] : entries_) {
    if (std::get<5>(key) != symbol) {
      continue;
    }
    if (found.has_value()) {
      return std::nullopt;
    }
    found = address;
  }
  return found;
}

std::optional<std::uint64_t> ModuleExportRegistry::ResolveScopedImport(
    std::string_view symbol, const ElfLibraryIdentity& library,
    const ElfModuleIdentity& module) const {
  if (!IsValidName(symbol, kMaximumSymbolNameLength) ||
      !IsValidName(library.name, kMaximumScopeNameLength) ||
      !IsValidName(module.name, kMaximumScopeNameLength)) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);
  const auto found = entries_.find(
      Key{library.name, library.version, module.name, module.version_major,
          module.version_minor, std::string(symbol)});
  return found == entries_.end()
             ? std::nullopt
             : std::optional<std::uint64_t>(found->second);
}

std::size_t ModuleExportRegistry::size() const {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

std::string_view ModuleExportRegistryStatusName(
    ModuleExportRegistryStatus status) noexcept {
  switch (status) {
    case ModuleExportRegistryStatus::kOk: return "ok";
    case ModuleExportRegistryStatus::kAddressOverflow:
      return "address-overflow";
    case ModuleExportRegistryStatus::kDuplicateExport:
      return "duplicate-export";
    case ModuleExportRegistryStatus::kCapacityExceeded:
      return "capacity-exceeded";
  }
  return "unknown";
}

}  // namespace kajps5::loader
