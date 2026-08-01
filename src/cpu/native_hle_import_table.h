// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "cpu/native_hle_trampoline.h"
#include "hle/import_registry.h"
#include "loader/elf.h"

namespace kajps5::cpu {

enum class NativeHleImportTableStatus {
  kOk,
  kAlreadyBuilt,
  kInvalidSymbolIndex,
  kEmptyImportSymbol,
  kTrampolineBuildFailed,
  kRegistryFailure,
};

struct NativeHleImportTableResult {
  NativeHleImportTableStatus status = NativeHleImportTableStatus::kOk;
  std::size_t import_count = 0;
  std::size_t resolved_import_count = 0;
  std::size_t unresolved_import_count = 0;
  std::size_t trampoline_count = 0;
  NativeHleTrampolineStatus trampoline_status = NativeHleTrampolineStatus::kOk;
  hle::ImportRegistryStatus registry_status = hle::ImportRegistryStatus::kOk;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == NativeHleImportTableStatus::kOk;
  }
};

class NativeHleImportTable final : public loader::ImportResolver {
 public:
  NativeHleImportTable(memory::GuestMemory& memory,
                       const hle::ExportRegistry& registry) noexcept;

  [[nodiscard]] NativeHleImportTableResult Build(
      const loader::ElfMetadata& metadata,
      std::size_t stack_argument_count = 0);
  [[nodiscard]] std::optional<std::uint64_t> ResolveImport(
      std::string_view symbol,
      std::span<const std::string> library_order) const override;
  [[nodiscard]] const NativeHleTrampoline* Find(
      std::string_view symbol,
      std::span<const std::string> library_order) const;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  using Key = std::pair<std::string, std::string>;

  memory::GuestMemory& memory_;
  const hle::ExportRegistry& registry_;
  hle::ImportRegistry targets_;
  std::map<Key, std::unique_ptr<NativeHleTrampoline>> trampolines_;
  bool built_ = false;
};

[[nodiscard]] std::string_view NativeHleImportTableStatusName(
    NativeHleImportTableStatus status) noexcept;

}  // namespace kajps5::cpu
