// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hle/export_registry.h"
#include "hle/import_coverage.h"
#include "hle/import_registry.h"
#include "loader/elf.h"

namespace kajps5::hle {

inline constexpr std::size_t kMaximumUnresolvedImportStubs = 16'384;
inline constexpr std::size_t kMaximumUnresolvedStubTraceRecords = 32;
inline constexpr std::size_t kMaximumUnresolvedImportModuleLength = 127;

struct UnresolvedImportStubRecord {
  std::string library;
  std::string module;
  std::size_t module_omitted_bytes = 0;
  std::string nid;
  std::uint64_t call_count = 0;
  std::array<std::uint64_t, 6> first_arguments{};
};

class UnresolvedImportStubStore final {
 public:
  [[nodiscard]] std::optional<std::size_t> Add(std::string library,
                                               std::string module,
                                               std::string nid);
  void RecordCall(std::size_t index,
                  const HleCallContext& context) noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::uint64_t total_calls() const noexcept;
  [[nodiscard]] std::span<const UnresolvedImportStubRecord> records()
      const noexcept;

 private:
  std::vector<UnresolvedImportStubRecord> records_;
  std::map<std::pair<std::string, std::string>, std::size_t> indices_;
  std::uint64_t total_calls_ = 0;
};

struct UnresolvedImportStubRegistrationResult {
  ExportRegistryStatus status = ExportRegistryStatus::kOk;
  ImportCoverageStatus coverage_status = ImportCoverageStatus::kOk;
  std::size_t registered_count = 0;
  std::size_t invalid_reference_count = 0;
  std::size_t omitted_count = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ExportRegistryStatus::kOk &&
           coverage_status == ImportCoverageStatus::kOk;
  }
};

[[nodiscard]] UnresolvedImportStubRegistrationResult
RegisterUnresolvedImportStubs(const loader::ElfMetadata& metadata,
                              ExportRegistry& registry,
                              const ImportRegistry& data_registry,
                              UnresolvedImportStubStore& store);
[[nodiscard]] std::string FormatUnresolvedImportStubTrace(
    const UnresolvedImportStubStore& store,
    std::string_view prefix = "title.unresolved_import_stub");

}  // namespace kajps5::hle
