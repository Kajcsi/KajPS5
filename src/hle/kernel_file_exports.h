// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"
#include "kernel/file.h"

namespace kajps5::hle {

inline constexpr auto kKernelOpenName = "sceKernelOpen";
inline constexpr auto kKernelOpenNid = "1G3lF1Gg1k8";
inline constexpr auto kKernelCloseName = "sceKernelClose";
inline constexpr auto kKernelCloseNid = "UK2Tl2DWUns";
inline constexpr auto kKernelReadName = "sceKernelRead";
inline constexpr auto kKernelReadNid = "Cg4srZ6TKbU";
inline constexpr auto kKernelPreadName = "sceKernelPread";
inline constexpr auto kKernelPreadNid = "+r3rMFwItV4";
inline constexpr auto kKernelLseekName = "sceKernelLseek";
inline constexpr auto kKernelLseekNid = "oib76F-12fk";
inline constexpr auto kKernelStatName = "sceKernelStat";
inline constexpr auto kKernelStatNid = "eV9wAD2riIA";
inline constexpr auto kKernelFstatName = "sceKernelFstat";
inline constexpr auto kKernelFstatNid = "kBwCPsYX-m4";
inline constexpr std::size_t kKernelStatSize = 120;

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelFileExports(
    kernel::FileService& files);
}

// The file service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelFileExports(
    ExportRegistry& registry, kernel::FileService& files);

}  // namespace kajps5::hle
