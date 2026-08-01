// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/static_tls_layout.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "static_tls_layout_test: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using kajps5::loader::StaticTlsLayout;
  using kajps5::loader::StaticTlsLayoutStatus;

  StaticTlsLayout layout;
  const auto first = layout.RegisterModule(1, 0x20, 0x10);
  const auto second = layout.RegisterModule(2, 0x18, 0x20, 8);
  const auto found = layout.FindModule(2);
  Check(first && first.module.static_offset == 0x20 && second &&
            second.module.static_offset == 0x38 && found &&
            found->static_offset == second.module.static_offset &&
            ((std::uint64_t{0} - found->static_offset) & 0x1f) == 8 &&
            layout.total_size() == 0x38 && layout.maximum_alignment() == 0x20 &&
            layout.module_count() == 2,
        "Variant II module layout is incorrect");

  const auto duplicate = layout.RegisterModule(2, 0x18, 0x20, 8);
  Check(duplicate && duplicate.module.static_offset == 0x38 &&
            layout.module_count() == 2,
        "matching TLS module registration was not stable");
  Check(layout.RegisterModule(2, 0x19, 0x20, 8).status ==
            StaticTlsLayoutStatus::kModuleConflict,
        "conflicting TLS module registration was accepted");
  Check(
      layout.RegisterModule(1, 0x20, 0x10).status == StaticTlsLayoutStatus::kOk,
      "matching earlier TLS module registration was rejected");
  Check(layout.RegisterModule(1, 0x21, 0x10).status ==
            StaticTlsLayoutStatus::kModuleConflict,
        "conflicting earlier TLS module registration was accepted");
  StaticTlsLayout ordered;
  Check(ordered.RegisterModule(2, 1, 1) &&
            ordered.RegisterModule(1, 1, 1).status ==
                StaticTlsLayoutStatus::kModuleOrderInvalid,
        "out-of-order TLS module registration was accepted");
  Check(layout.RegisterModule(0, 1, 1).status ==
                StaticTlsLayoutStatus::kInvalidModuleId &&
            layout.RegisterModule(3, 0, 1).status ==
                StaticTlsLayoutStatus::kInvalidMemorySize &&
            layout.RegisterModule(3, 1, 3).status ==
                StaticTlsLayoutStatus::kInvalidAlignment,
        "invalid TLS layout input was accepted");

  StaticTlsLayout limited(0x20);
  Check(limited.RegisterModule(1, 0x20, 0x10) &&
            limited.RegisterModule(2, 1, 1).status ==
                StaticTlsLayoutStatus::kReservationExceeded,
        "static TLS reservation was not enforced");
  StaticTlsLayout overflow(std::numeric_limits<std::uint64_t>::max());
  Check(overflow.RegisterModule(1, std::numeric_limits<std::uint64_t>::max(), 2)
                .status == StaticTlsLayoutStatus::kLayoutOverflow,
        "static TLS layout overflow was accepted");
  Check(kajps5::loader::StaticTlsLayoutStatusName(
            StaticTlsLayoutStatus::kReservationExceeded) ==
            "reservation-exceeded",
        "static TLS status name is unstable");
  return 0;
}
