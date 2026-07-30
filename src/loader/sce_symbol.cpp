// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/sce_symbol.h"

#include <algorithm>
#include <limits>

namespace kajps5::loader {
namespace {

std::optional<std::uint16_t> DecodeSceId(std::string_view value) noexcept {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
  if (value.empty() || value.size() > 3) {
    return std::nullopt;
  }

  std::uint32_t decoded = 0;
  for (const auto character : value) {
    const auto digit = alphabet.find(character);
    if (digit == std::string_view::npos) {
      return std::nullopt;
    }
    decoded = (decoded << 6U) | static_cast<std::uint32_t>(digit);
  }
  if (decoded > std::numeric_limits<std::uint16_t>::max() ||
      (value.size() == 2 && decoded < 0x40U) ||
      (value.size() == 3 && decoded < 0x1000U)) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(decoded);
}

template <typename Identity>
const Identity* FindIdentity(std::uint16_t id,
                             const std::vector<Identity>& imports,
                             const std::vector<Identity>& exports) noexcept {
  const auto find = [id](const std::vector<Identity>& identities) {
    const auto identity = std::find_if(
        identities.begin(), identities.end(),
        [id](const Identity& candidate) { return candidate.id == id; });
    return identity == identities.end() ? nullptr : &*identity;
  };
  if (const auto* identity = find(imports); identity != nullptr) {
    return identity;
  }
  return find(exports);
}

}  // namespace

std::optional<SceSymbolReference> ParseSceSymbolReference(
    std::string_view symbol) noexcept {
  const auto first_separator = symbol.find('#');
  if (first_separator == std::string_view::npos || first_separator == 0) {
    return std::nullopt;
  }
  const auto second_separator = symbol.find('#', first_separator + 1);
  if (second_separator == std::string_view::npos ||
      symbol.find('#', second_separator + 1) != std::string_view::npos) {
    return std::nullopt;
  }

  const auto library_id = DecodeSceId(symbol.substr(
      first_separator + 1, second_separator - first_separator - 1));
  const auto module_id = DecodeSceId(symbol.substr(second_separator + 1));
  if (!library_id.has_value() || !module_id.has_value()) {
    return std::nullopt;
  }
  return SceSymbolReference{symbol.substr(0, first_separator), *library_id,
                            *module_id};
}

ElfImportReference ResolveElfImportReference(
    const ElfMetadata& metadata, std::string_view symbol) noexcept {
  const auto first_separator = symbol.find('#');
  if (first_separator == std::string_view::npos) {
    return {symbol, nullptr, nullptr, !symbol.empty()};
  }
  const auto parsed = ParseSceSymbolReference(symbol);
  if (!parsed.has_value()) {
    return {symbol.substr(0, first_separator), nullptr, nullptr, false};
  }

  const auto* library = FindIdentity(
      parsed->library_id, metadata.dynamic_info.import_libraries,
      metadata.dynamic_info.export_libraries);
  const auto* module = FindIdentity(
      parsed->module_id, metadata.dynamic_info.import_modules,
      metadata.dynamic_info.export_modules);
  return {parsed->nid, library, module,
          library != nullptr && module != nullptr};
}

}  // namespace kajps5::loader
