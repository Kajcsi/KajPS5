// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"

namespace kajps5::kernel {
class JsonValueService;
}

namespace kajps5::hle {

inline constexpr auto kJson2LibraryName = "libSceJson2";
inline constexpr auto kJsonLibraryName = "libSceJson";
inline constexpr auto kJsonValueConstructorName =
    "_ZN3sce4Json5ValueC1Ev";
inline constexpr auto kJsonValueConstructorNid = "qBMjqyBn3OM";
inline constexpr auto kJsonValueBaseConstructorName =
    "_ZN3sce4Json5ValueC2Ev";
inline constexpr auto kJsonValueBaseConstructorNid = "-wa17B7TGnw";
inline constexpr auto kJsonValueDestructorName = "_ZN3sce4Json5ValueD1Ev";
inline constexpr auto kJsonValueDestructorNid = "WTtYf+cNnXI";
inline constexpr auto kJsonValueBaseDestructorName =
    "_ZN3sce4Json5ValueD2Ev";
inline constexpr auto kJsonValueBaseDestructorNid = "0eUrW9JAxM0";

[[nodiscard]] ExportRegistryStatus RegisterJsonExports(
    ExportRegistry& registry, kernel::JsonValueService& values);

}  // namespace kajps5::hle
