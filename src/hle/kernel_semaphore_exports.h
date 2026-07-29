// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"
#include "kernel/semaphore.h"

namespace kajps5::hle {

inline constexpr auto kKernelCreateSemaName = "sceKernelCreateSema";
inline constexpr auto kKernelCreateSemaNid = "188x57JYp0g";
inline constexpr auto kKernelDeleteSemaName = "sceKernelDeleteSema";
inline constexpr auto kKernelDeleteSemaNid = "R1Jvn8bSCW8";
inline constexpr auto kKernelPollSemaName = "sceKernelPollSema";
inline constexpr auto kKernelPollSemaNid = "12wOHk8ywb0";
inline constexpr auto kKernelSignalSemaName = "sceKernelSignalSema";
inline constexpr auto kKernelSignalSemaNid = "4czppHBiriw";

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelSemaphoreExports(
    kernel::SemaphoreService& semaphores);
}

// The semaphore service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelSemaphoreExports(
    ExportRegistry& registry, kernel::SemaphoreService& semaphores);

}  // namespace kajps5::hle
