// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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
};

struct RelocationResult {
  RelocationStatus status = RelocationStatus::kOk;
  std::size_t applied_count = 0;
  std::size_t unresolved_import_count = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == RelocationStatus::kOk;
  }
};

[[nodiscard]] RelocationResult ApplyRelativeRelocations(
    const ElfMetadata& metadata, memory::GuestMemory& memory,
    std::uint64_t load_bias = 0);
[[nodiscard]] std::string_view RelocationStatusName(
    RelocationStatus status) noexcept;

}  // namespace kajps5::loader
