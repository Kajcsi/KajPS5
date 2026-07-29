// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"
#include "kernel/clock.h"

namespace kajps5::hle {

inline constexpr auto kLibKernelName = "libKernel";
inline constexpr auto kKernelGetProcessTimeName = "sceKernelGetProcessTime";
inline constexpr auto kKernelGetProcessTimeCounterName =
    "sceKernelGetProcessTimeCounter";
inline constexpr auto kKernelGetProcessTimeCounterFrequencyName =
    "sceKernelGetProcessTimeCounterFrequency";

// The clock service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelClockExports(
    ExportRegistry& registry, kernel::KernelClockService& clock);

}  // namespace kajps5::hle
