// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "loader/relocator.h"

namespace kajps5::loader {

class LayeredImportResolver final : public ImportResolver {
 public:
  LayeredImportResolver(const ImportResolver& primary,
                        const ImportResolver& fallback) noexcept;

  [[nodiscard]] std::optional<std::uint64_t> ResolveImport(
      std::string_view symbol,
      std::span<const std::string> library_order) const override;
  [[nodiscard]] std::optional<std::uint64_t> ResolveScopedImport(
      std::string_view symbol, const ElfLibraryIdentity& library,
      const ElfModuleIdentity& module) const override;

 private:
  const ImportResolver& primary_;
  const ImportResolver& fallback_;
};

}  // namespace kajps5::loader
