// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "loader/elf.h"

namespace kajps5::loader {

enum class RelocationStatus {
  kOk,
  kUnsupportedRelocation,
  kInvalidRelativeSymbol,
  kTargetAddressOverflow,
  kTargetNotMapped,
  kWriteFailed,
  kInvalidSymbolIndex,
  kEmptyImportSymbol,
  kInvalidResolvedAddress,
};

struct UnresolvedImport {
  std::uint32_t symbol_index = 0;
  std::uint32_t relocation_type = 0;
  std::uint64_t target_address = 0;
  std::string symbol;
};

struct RelocationResult {
  RelocationStatus status = RelocationStatus::kOk;
  std::size_t applied_count = 0;
  std::size_t unresolved_import_count = 0;
  std::size_t resolved_import_count = 0;
  std::vector<UnresolvedImport> unresolved_imports;
  std::optional<std::uint32_t> unsupported_relocation_type;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == RelocationStatus::kOk;
  }
};

class ImportResolver {
 public:
  virtual ~ImportResolver() = default;

  [[nodiscard]] virtual std::optional<std::uint64_t> ResolveImport(
      std::string_view symbol,
      std::span<const std::string> library_order) const = 0;
};

[[nodiscard]] RelocationResult ApplyRelativeRelocations(
    const ElfMetadata& metadata, memory::GuestMemory& memory,
    std::uint64_t load_bias = 0);
[[nodiscard]] RelocationResult ApplyRelocations(
    const ElfMetadata& metadata, memory::GuestMemory& memory,
    const ImportResolver& resolver, std::uint64_t load_bias = 0);
[[nodiscard]] std::string_view RelocationStatusName(
    RelocationStatus status) noexcept;

}  // namespace kajps5::loader
