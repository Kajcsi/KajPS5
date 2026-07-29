// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"
#include "kernel/file.h"

namespace kajps5::hle {

inline constexpr auto kKernelOpenName = "sceKernelOpen";
inline constexpr auto kKernelOpenNid = "1G3lF1Gg1k8";
inline constexpr auto kKernelCloseName = "sceKernelClose";
inline constexpr auto kKernelCloseNid = "UK2Tl2DWUns";

// The file service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelFileExports(
    ExportRegistry& registry, kernel::FileService& files);

}  // namespace kajps5::hle
