// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/sce_symbol.h"

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

}  // namespace kajps5::loader
