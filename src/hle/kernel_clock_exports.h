// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"
#include "kernel/clock.h"

namespace kajps5::hle {

inline constexpr auto kKernelClockGettimeName = "sceKernelClockGettime";
inline constexpr auto kKernelClockGettimeNid = "QBi7HCK03hw";
inline constexpr auto kKernelGettimeofdayName = "sceKernelGettimeofday";
inline constexpr auto kKernelGettimeofdayNid = "ejekcaNQNq0";
inline constexpr auto kKernelGetProcessTimeName = "sceKernelGetProcessTime";
inline constexpr auto kKernelGetProcessTimeNid = "4J2sUJmuHZQ";
inline constexpr auto kKernelGetProcessTimeCounterName =
    "sceKernelGetProcessTimeCounter";
inline constexpr auto kKernelGetProcessTimeCounterNid = "fgxnMeTNUtY";
inline constexpr auto kKernelGetProcessTimeCounterFrequencyName =
    "sceKernelGetProcessTimeCounterFrequency";
inline constexpr auto kKernelGetProcessTimeCounterFrequencyNid =
    "BNowx2l588E";

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelClockExports(
    kernel::KernelClockService& clock);
}

// The clock service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelClockExports(
    ExportRegistry& registry, kernel::KernelClockService& clock);

}  // namespace kajps5::hle
