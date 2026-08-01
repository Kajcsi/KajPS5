// Copyright (C) 2026 KajPS5 contributors
// Architecture and behavior reference: KytyPS5
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "hle/export_registry.h"

namespace kajps5::gpu {
class GpuRuntime;
}

namespace kajps5::hle {

inline constexpr auto kAgcLibraryName = "libSceAgc";
inline constexpr auto kAgcCbNopNid = "LtTouSCZjHM";
inline constexpr auto kAgcCbNopGetSizeNid = "t7PlZ9nt5Lc";
inline constexpr auto kAgcCbDispatchNid = "k3GhuSNmBLU";
inline constexpr auto kAgcCbDispatchGetSizeNid = "Abendgtz+3o";
inline constexpr auto kAgcGetPacketSizeNid = "Lkf86B98qPc";
inline constexpr auto kAgcSetPacketPredicationNid = "w6Dj1VJt5qY";
inline constexpr std::size_t kRegisteredAgcFunctionCount = 6;

[[nodiscard]] ExportRegistryStatus RegisterAgcExports(
    ExportRegistry& registry, gpu::GpuRuntime& gpu_runtime);

}  // namespace kajps5::hle
