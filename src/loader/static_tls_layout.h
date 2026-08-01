// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace kajps5::loader {

inline constexpr std::uint64_t kDefaultStaticTlsReservation = 0x20000;

enum class StaticTlsLayoutStatus {
  kOk,
  kInvalidModuleId,
  kInvalidMemorySize,
  kInvalidAlignment,
  kLayoutOverflow,
  kReservationExceeded,
  kModuleOrderInvalid,
  kModuleConflict,
};

struct StaticTlsModule {
  std::uint64_t module_id = 0;
  std::uint64_t memory_size = 0;
  std::uint64_t alignment = 1;
  std::uint64_t alignment_bias = 0;
  std::uint64_t static_offset = 0;
};

struct StaticTlsRegistrationResult {
  StaticTlsLayoutStatus status = StaticTlsLayoutStatus::kOk;
  StaticTlsModule module;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == StaticTlsLayoutStatus::kOk;
  }
};

class StaticTlsLayout final {
 public:
  explicit StaticTlsLayout(
      std::uint64_t reservation = kDefaultStaticTlsReservation) noexcept;

  [[nodiscard]] StaticTlsRegistrationResult RegisterModule(
      std::uint64_t module_id, std::uint64_t memory_size,
      std::uint64_t alignment, std::uint64_t alignment_bias = 0);
  [[nodiscard]] std::optional<StaticTlsModule> FindModule(
      std::uint64_t module_id) const noexcept;
  [[nodiscard]] std::uint64_t total_size() const noexcept;
  [[nodiscard]] std::uint64_t maximum_alignment() const noexcept;
  [[nodiscard]] std::size_t module_count() const noexcept;

 private:
  std::uint64_t reservation_ = kDefaultStaticTlsReservation;
  std::uint64_t total_size_ = 0;
  std::uint64_t maximum_alignment_ = 1;
  std::map<std::uint64_t, StaticTlsModule> modules_;
};

[[nodiscard]] std::string_view StaticTlsLayoutStatusName(
    StaticTlsLayoutStatus status) noexcept;

}  // namespace kajps5::loader
