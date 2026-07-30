// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include "loader/elf.h"
#include "loader/relocator.h"

namespace kajps5::loader {

inline constexpr std::size_t kMaximumModuleExports = 262144;

enum class ModuleExportRegistryStatus {
  kOk,
  kAddressOverflow,
  kDuplicateExport,
  kCapacityExceeded,
};

struct ModuleExportRegistrationResult {
  ModuleExportRegistryStatus status = ModuleExportRegistryStatus::kOk;
  std::size_t registered_count = 0;
  std::size_t skipped_count = 0;
  std::optional<std::uint32_t> failed_symbol_index;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ModuleExportRegistryStatus::kOk;
  }
};

class ModuleExportRegistry final : public ImportResolver {
 public:
  [[nodiscard]] ModuleExportRegistrationResult RegisterModule(
      const ElfMetadata& metadata, std::uint64_t load_bias = 0);
  [[nodiscard]] std::optional<std::uint64_t> ResolveImport(
      std::string_view symbol,
      std::span<const std::string> library_order) const override;
  [[nodiscard]] std::optional<std::uint64_t> ResolveScopedImport(
      std::string_view symbol, const ElfLibraryIdentity& library,
      const ElfModuleIdentity& module) const override;
  [[nodiscard]] std::size_t size() const;

 private:
  using Key =
      std::tuple<std::string, std::uint16_t, std::string, std::uint8_t,
                 std::uint8_t, std::string>;

  mutable std::mutex mutex_;
  std::map<Key, std::uint64_t> entries_;
};

[[nodiscard]] std::string_view ModuleExportRegistryStatusName(
    ModuleExportRegistryStatus status) noexcept;

}  // namespace kajps5::loader
