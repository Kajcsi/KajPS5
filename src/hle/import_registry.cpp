// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/import_registry.h"

#include <utility>

namespace kajps5::hle {
namespace {

bool IsValidName(std::string_view name, std::size_t maximum_length) noexcept {
  return !name.empty() && name.size() <= maximum_length &&
         name.find('\0') == std::string_view::npos;
}

}  // namespace

ImportRegistryStatus ImportRegistry::Register(std::string library,
                                              std::string symbol,
                                              std::uint64_t target_address) {
  if (!IsValidName(library, kMaximumImportLibraryLength) ||
      !IsValidName(symbol, kMaximumImportSymbolLength) || target_address == 0) {
    return ImportRegistryStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  const auto [entry, inserted] = entries_.emplace(
      Key{std::move(library), std::move(symbol)}, target_address);
  (void)entry;
  return inserted ? ImportRegistryStatus::kOk
                  : ImportRegistryStatus::kAlreadyExists;
}

ImportLookupResult ImportRegistry::Resolve(
    std::string_view symbol,
    std::span<const std::string> library_order) const {
  if (!IsValidName(symbol, kMaximumImportSymbolLength)) {
    return {ImportRegistryStatus::kInvalidArgument, 0, {}};
  }

  std::lock_guard lock(mutex_);
  for (const auto& library : library_order) {
    if (!IsValidName(library, kMaximumImportLibraryLength)) {
      return {ImportRegistryStatus::kInvalidArgument, 0, {}};
    }
    const auto found = entries_.find(Key{library, std::string(symbol)});
    if (found != entries_.end()) {
      return {ImportRegistryStatus::kOk, found->second, found->first.first};
    }
  }
  if (!library_order.empty()) {
    return {ImportRegistryStatus::kNotFound, 0, {}};
  }

  ImportLookupResult result{ImportRegistryStatus::kNotFound, 0, {}};
  for (const auto& [key, target_address] : entries_) {
    if (key.second != symbol) {
      continue;
    }
    if (result.status == ImportRegistryStatus::kOk) {
      return {ImportRegistryStatus::kAmbiguous, 0, {}};
    }
    result = {ImportRegistryStatus::kOk, target_address, key.first};
  }
  return result;
}

std::optional<std::uint64_t> ImportRegistry::ResolveImport(
    std::string_view symbol,
    std::span<const std::string> library_order) const {
  const auto result = Resolve(symbol, library_order);
  return result ? std::optional<std::uint64_t>(result.target_address)
                : std::nullopt;
}

std::size_t ImportRegistry::size() const {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

std::string_view ImportRegistryStatusName(
    ImportRegistryStatus status) noexcept {
  switch (status) {
    case ImportRegistryStatus::kOk: return "ok";
    case ImportRegistryStatus::kInvalidArgument: return "invalid-argument";
    case ImportRegistryStatus::kAlreadyExists: return "already-exists";
    case ImportRegistryStatus::kNotFound: return "not-found";
    case ImportRegistryStatus::kAmbiguous: return "ambiguous";
  }
  return "unknown";
}

}  // namespace kajps5::hle
