// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kajps5::loader {

struct SceSymbolReference {
  std::string_view nid;
  std::uint16_t library_id = 0;
  std::uint16_t module_id = 0;
};

[[nodiscard]] std::optional<SceSymbolReference> ParseSceSymbolReference(
    std::string_view symbol) noexcept;

}  // namespace kajps5::loader
