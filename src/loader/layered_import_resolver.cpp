// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/layered_import_resolver.h"

namespace kajps5::loader {

LayeredImportResolver::LayeredImportResolver(
    const ImportResolver& primary, const ImportResolver& fallback) noexcept
    : primary_(primary), fallback_(fallback) {}

std::optional<std::uint64_t> LayeredImportResolver::ResolveImport(
    std::string_view symbol,
    std::span<const std::string> library_order) const {
  auto resolved = primary_.ResolveImport(symbol, library_order);
  if (resolved.has_value()) {
    return resolved;
  }
  return fallback_.ResolveImport(symbol, library_order);
}

std::optional<std::uint64_t> LayeredImportResolver::ResolveScopedImport(
    std::string_view symbol, const ElfLibraryIdentity& library,
    const ElfModuleIdentity& module) const {
  auto resolved = primary_.ResolveScopedImport(symbol, library, module);
  if (resolved.has_value()) {
    return resolved;
  }
  return fallback_.ResolveScopedImport(symbol, library, module);
}

}  // namespace kajps5::loader
