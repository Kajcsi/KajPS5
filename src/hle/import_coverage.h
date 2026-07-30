// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hle/export_registry.h"
#include "loader/elf.h"

namespace kajps5::hle {

inline constexpr std::size_t kMaximumImportCoverageDetails = 32;
inline constexpr std::size_t kMaximumImportCoverageSymbolBytes = 128;

enum class ImportCoverageStatus {
  kOk,
  kInvalidSymbolIndex,
  kEmptyImportSymbol,
};

struct ImportCoverageEntry {
  std::uint32_t symbol_index = 0;
  std::size_t relocation_count = 0;
  std::string symbol;
  std::string requested_library;
  std::string requested_module;
  ExportRegistryStatus lookup_status = ExportRegistryStatus::kNotFound;
};

struct ImportCoverageResult {
  ImportCoverageStatus status = ImportCoverageStatus::kOk;
  std::size_t available_export_count = 0;
  std::size_t import_relocation_count = 0;
  std::size_t resolved_relocation_count = 0;
  std::size_t unresolved_relocation_count = 0;
  std::size_t unique_import_count = 0;
  std::size_t resolved_unique_import_count = 0;
  std::size_t unresolved_unique_import_count = 0;
  std::vector<ImportCoverageEntry> imports;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ImportCoverageStatus::kOk;
  }
};

[[nodiscard]] ImportCoverageResult AnalyzeImportCoverage(
    const loader::ElfMetadata& metadata, const ExportRegistry& registry);
[[nodiscard]] std::string FormatImportCoverageTrace(
    const ImportCoverageResult& result);
[[nodiscard]] std::string_view ImportCoverageStatusName(
    ImportCoverageStatus status) noexcept;

}  // namespace kajps5::hle
