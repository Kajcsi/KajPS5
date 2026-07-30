// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/export_registry.h"

#include <utility>

namespace kajps5::hle {
namespace {

bool IsValidName(std::string_view name, std::size_t maximum_length) noexcept {
  return !name.empty() && name.size() <= maximum_length &&
         name.find('\0') == std::string_view::npos;
}

}  // namespace

ExportRegistryStatus ExportRegistry::Register(std::string library,
                                              std::string symbol,
                                              HleHandler handler) {
  if (!IsValidName(library, kMaximumExportLibraryLength) ||
      !IsValidName(symbol, kMaximumExportSymbolLength) || !handler) {
    return ExportRegistryStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  const auto [entry, inserted] = entries_.emplace(
      Key{std::move(library), std::move(symbol)}, std::move(handler));
  (void)entry;
  return inserted ? ExportRegistryStatus::kOk
                  : ExportRegistryStatus::kAlreadyExists;
}

ExportRegistryStatus ExportRegistry::RegisterBatch(
    std::vector<HleExportDefinition> exports) {
  if (exports.empty()) {
    return ExportRegistryStatus::kInvalidArgument;
  }
  for (const auto& definition : exports) {
    if (!IsValidName(definition.library, kMaximumExportLibraryLength) ||
        !IsValidName(definition.symbol, kMaximumExportSymbolLength) ||
        !definition.handler) {
      return ExportRegistryStatus::kInvalidArgument;
    }
  }

  std::lock_guard lock(mutex_);
  auto updated = entries_;
  for (auto& definition : exports) {
    const auto [entry, inserted] = updated.emplace(
        Key{std::move(definition.library), std::move(definition.symbol)},
        std::move(definition.handler));
    (void)entry;
    if (!inserted) {
      return ExportRegistryStatus::kAlreadyExists;
    }
  }
  entries_.swap(updated);
  return ExportRegistryStatus::kOk;
}

ExportRegistry::ResolvedExport ExportRegistry::ResolveLocked(
    std::string_view symbol,
    std::span<const std::string> library_order) const {
  if (!IsValidName(symbol, kMaximumExportSymbolLength)) {
    return {ExportRegistryStatus::kInvalidArgument, {}, {}};
  }

  ResolvedExport resolved;
  for (const auto& library : library_order) {
    if (!IsValidName(library, kMaximumExportLibraryLength)) {
      return {ExportRegistryStatus::kInvalidArgument, {}, {}};
    }
    const auto found = entries_.find(Key{library, std::string(symbol)});
    if (found != entries_.end()) {
      resolved = {ExportRegistryStatus::kOk, found->second,
                  found->first.first};
      break;
    }
  }

  if (resolved.status != ExportRegistryStatus::kOk &&
      !library_order.empty()) {
    return resolved;
  }
  if (library_order.empty()) {
    for (const auto& [key, handler] : entries_) {
      if (key.second != symbol) {
        continue;
      }
      if (resolved.status == ExportRegistryStatus::kOk) {
        return {ExportRegistryStatus::kAmbiguous, {}, {}};
      }
      resolved = {ExportRegistryStatus::kOk, handler, key.first};
    }
  }
  return resolved;
}

HleLookupResult ExportRegistry::Lookup(
    std::string_view symbol,
    std::span<const std::string> library_order) const {
  std::lock_guard lock(mutex_);
  const auto resolved = ResolveLocked(symbol, library_order);
  return {resolved.status, resolved.library};
}

HleLookupResult ExportRegistry::Lookup(std::string_view symbol) const {
  return Lookup(symbol, std::span<const std::string>{});
}

HleDispatchResult ExportRegistry::Dispatch(
    std::string_view symbol, std::span<const std::string> library_order,
    HleCallContext& context) const {
  ResolvedExport resolved;
  {
    std::lock_guard lock(mutex_);
    resolved = ResolveLocked(symbol, library_order);
  }

  if (resolved.status != ExportRegistryStatus::kOk) {
    return {resolved.status, HleContextStatus::kOk, {}};
  }
  const auto handler_status = resolved.handler(context);
  return {ExportRegistryStatus::kOk, handler_status,
          std::move(resolved.library)};
}

HleDispatchResult ExportRegistry::Dispatch(
    std::string_view symbol, HleCallContext& context) const {
  return Dispatch(symbol, std::span<const std::string>{}, context);
}

std::size_t ExportRegistry::size() const {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

std::string_view ExportRegistryStatusName(
    ExportRegistryStatus status) noexcept {
  switch (status) {
    case ExportRegistryStatus::kOk: return "ok";
    case ExportRegistryStatus::kInvalidArgument: return "invalid-argument";
    case ExportRegistryStatus::kAlreadyExists: return "already-exists";
    case ExportRegistryStatus::kNotFound: return "not-found";
    case ExportRegistryStatus::kAmbiguous: return "ambiguous";
  }
  return "unknown";
}

}  // namespace kajps5::hle
