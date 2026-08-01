// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/static_tls_layout.h"

#include <algorithm>
#include <limits>

namespace kajps5::loader {

StaticTlsLayout::StaticTlsLayout(std::uint64_t reservation) noexcept
    : reservation_(reservation) {}

StaticTlsRegistrationResult StaticTlsLayout::RegisterModule(
    std::uint64_t module_id, std::uint64_t memory_size, std::uint64_t alignment,
    std::uint64_t alignment_bias) {
  if (module_id == 0) {
    return {StaticTlsLayoutStatus::kInvalidModuleId};
  }
  if (memory_size == 0) {
    return {StaticTlsLayoutStatus::kInvalidMemorySize};
  }
  const auto normalized_alignment = alignment == 0 ? 1 : alignment;
  if ((normalized_alignment & (normalized_alignment - 1)) != 0) {
    return {StaticTlsLayoutStatus::kInvalidAlignment};
  }
  const auto normalized_bias = alignment_bias & (normalized_alignment - 1);

  if (const auto found = modules_.find(module_id); found != modules_.end()) {
    const auto& module = found->second;
    if (module.memory_size != memory_size ||
        module.alignment != normalized_alignment ||
        module.alignment_bias != normalized_bias) {
      return {StaticTlsLayoutStatus::kModuleConflict, module};
    }
    return {StaticTlsLayoutStatus::kOk, module};
  }
  if (!modules_.empty() && module_id <= modules_.rbegin()->first) {
    return {StaticTlsLayoutStatus::kModuleOrderInvalid};
  }

  if (memory_size > std::numeric_limits<std::uint64_t>::max() - total_size_ ||
      normalized_alignment - 1 > std::numeric_limits<std::uint64_t>::max() -
                                     total_size_ - memory_size) {
    return {StaticTlsLayoutStatus::kLayoutOverflow};
  }
  const auto candidate = total_size_ + memory_size + normalized_alignment - 1;
  const auto remainder =
      ((candidate & (normalized_alignment - 1)) + normalized_bias) &
      (normalized_alignment - 1);
  const auto static_offset = candidate - remainder;
  if (static_offset > reservation_) {
    return {StaticTlsLayoutStatus::kReservationExceeded};
  }

  const StaticTlsModule module{module_id, memory_size, normalized_alignment,
                               normalized_bias, static_offset};
  modules_.emplace(module_id, module);
  total_size_ = static_offset;
  maximum_alignment_ = std::max(maximum_alignment_, normalized_alignment);
  return {StaticTlsLayoutStatus::kOk, module};
}

std::optional<StaticTlsModule> StaticTlsLayout::FindModule(
    std::uint64_t module_id) const noexcept {
  const auto found = modules_.find(module_id);
  if (found == modules_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::uint64_t StaticTlsLayout::total_size() const noexcept {
  return total_size_;
}

std::uint64_t StaticTlsLayout::maximum_alignment() const noexcept {
  return maximum_alignment_;
}

std::size_t StaticTlsLayout::module_count() const noexcept {
  return modules_.size();
}

std::string_view StaticTlsLayoutStatusName(
    StaticTlsLayoutStatus status) noexcept {
  switch (status) {
    case StaticTlsLayoutStatus::kOk:
      return "ok";
    case StaticTlsLayoutStatus::kInvalidModuleId:
      return "invalid-module-id";
    case StaticTlsLayoutStatus::kInvalidMemorySize:
      return "invalid-memory-size";
    case StaticTlsLayoutStatus::kInvalidAlignment:
      return "invalid-alignment";
    case StaticTlsLayoutStatus::kLayoutOverflow:
      return "layout-overflow";
    case StaticTlsLayoutStatus::kReservationExceeded:
      return "reservation-exceeded";
    case StaticTlsLayoutStatus::kModuleOrderInvalid:
      return "module-order-invalid";
    case StaticTlsLayoutStatus::kModuleConflict:
      return "module-conflict";
  }
  return "unknown";
}

}  // namespace kajps5::loader
