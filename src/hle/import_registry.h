// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
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
#include <utility>

#include "loader/relocator.h"

namespace kajps5::hle {

inline constexpr std::size_t kMaximumImportLibraryLength = 127;
inline constexpr std::size_t kMaximumImportSymbolLength = 255;

enum class ImportRegistryStatus {
  kOk,
  kInvalidArgument,
  kAlreadyExists,
  kNotFound,
  kAmbiguous,
};

struct ImportLookupResult {
  ImportRegistryStatus status = ImportRegistryStatus::kOk;
  std::uint64_t target_address = 0;
  std::string library;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ImportRegistryStatus::kOk;
  }
};

class ImportRegistry final : public loader::ImportResolver {
 public:
  [[nodiscard]] ImportRegistryStatus Register(std::string library,
                                              std::string symbol,
                                              std::uint64_t target_address);
  [[nodiscard]] ImportLookupResult Resolve(
      std::string_view symbol,
      std::span<const std::string> library_order = {}) const;
  [[nodiscard]] std::optional<std::uint64_t> ResolveImport(
      std::string_view symbol,
      std::span<const std::string> library_order) const override;
  [[nodiscard]] std::size_t size() const;

 private:
  using Key = std::pair<std::string, std::string>;

  mutable std::mutex mutex_;
  std::map<Key, std::uint64_t> entries_;
};

[[nodiscard]] std::string_view ImportRegistryStatusName(
    ImportRegistryStatus status) noexcept;

}  // namespace kajps5::hle
