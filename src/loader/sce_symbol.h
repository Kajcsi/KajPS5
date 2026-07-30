// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "loader/elf.h"

namespace kajps5::loader {

struct SceSymbolReference {
  std::string_view nid;
  std::uint16_t library_id = 0;
  std::uint16_t module_id = 0;
};

struct ElfImportReference {
  std::string_view nid;
  const ElfLibraryIdentity* library = nullptr;
  const ElfModuleIdentity* module = nullptr;
  bool valid = false;
};

[[nodiscard]] std::optional<SceSymbolReference> ParseSceSymbolReference(
    std::string_view symbol) noexcept;
[[nodiscard]] ElfImportReference ResolveElfImportReference(
    const ElfMetadata& metadata, std::string_view symbol) noexcept;

}  // namespace kajps5::loader
