// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"

namespace kajps5::kernel {
class CxaGuardService;
}

namespace kajps5::hle {

inline constexpr auto kLibcName = "libc";
inline constexpr auto kCxaGuardAcquireName = "__cxa_guard_acquire";
inline constexpr auto kCxaGuardAcquireNid = "3GPpjQdAMTw";
inline constexpr auto kCxaGuardReleaseName = "__cxa_guard_release";
inline constexpr auto kCxaGuardReleaseNid = "9rAeANT2tyE";
inline constexpr auto kCxaGuardAbortName = "__cxa_guard_abort";
inline constexpr auto kCxaGuardAbortNid = "2emaaluWzUw";

[[nodiscard]] ExportRegistryStatus RegisterLibcExports(
    ExportRegistry& registry, kernel::CxaGuardService& guards);

}  // namespace kajps5::hle
