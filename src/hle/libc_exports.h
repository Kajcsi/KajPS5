// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"

namespace kajps5::kernel {
class CxaGuardService;
class ProcessLifecycleService;
}

namespace kajps5::hle {

inline constexpr auto kLibcName = "libc";
inline constexpr auto kCxaGuardAcquireName = "__cxa_guard_acquire";
inline constexpr auto kCxaGuardAcquireNid = "3GPpjQdAMTw";
inline constexpr auto kCxaGuardReleaseName = "__cxa_guard_release";
inline constexpr auto kCxaGuardReleaseNid = "9rAeANT2tyE";
inline constexpr auto kCxaGuardAbortName = "__cxa_guard_abort";
inline constexpr auto kCxaGuardAbortNid = "2emaaluWzUw";
inline constexpr auto kCxaPureVirtualName = "__cxa_pure_virtual";
inline constexpr auto kCxaPureVirtualNid = "zr094EQ39Ww";
inline constexpr auto kLibcInitEnvName = "_init_env";
inline constexpr auto kLibcInitEnvNid = "bzQExy189ZI";
inline constexpr auto kLibcAtexitName = "atexit";
inline constexpr auto kLibcAtexitNid = "8G2LB+A3rzg";
inline constexpr auto kLibcCxaAtexitName = "__cxa_atexit";
inline constexpr auto kLibcCxaAtexitNid = "tsvEmnenz48";
inline constexpr auto kLibcCatchReturnFromMainName = "catchReturnFromMain";
inline constexpr auto kLibcCatchReturnFromMainNid = "XKRegsFpEpk";
inline constexpr auto kLibcExitName = "exit";
inline constexpr auto kLibcExitNid = "uMei1W9uyNo";
inline constexpr auto kLibcAbortName = "abort";
inline constexpr auto kLibcAbortNid = "L1SBTkC+Cvw";

[[nodiscard]] ExportRegistryStatus RegisterLibcExports(
    ExportRegistry& registry, kernel::CxaGuardService& guards,
    kernel::ProcessLifecycleService& lifecycle);

}  // namespace kajps5::hle
