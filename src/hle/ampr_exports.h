// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "hle/export_registry.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::kernel {
class AmprCommandBufferService;
}

namespace kajps5::hle {

inline constexpr auto kAmprLibraryName = "libSceAmpr";
inline constexpr auto kAmprCommandBufferConstructorNid = "8aI7R7WaOlc";
inline constexpr auto kAmprAprCommandBufferConstructorNid = "a8uLzYY--tM";
inline constexpr auto kAmprAprCommandBufferDestructorNid = "Qs1xtplKo0U";
inline constexpr auto kAmprCommandBufferDestructorNid = "GuchCTefuZw";
inline constexpr auto kAmprCommandBufferSetBufferNid = "N-FSPA4S3nI";
inline constexpr auto kAmprCommandBufferResetNid = "baQO9ez2gL4";
inline constexpr auto kAmprCommandBufferClearBufferNid = "ULvXMDz56po";
inline constexpr auto kAmprAprCommandBufferReadFileNid = "mQ16-QdKv7k";
inline constexpr auto kAmprMeasureCommandSizeReadFileNid = "vWU-odnS+fU";
inline constexpr auto kAmprMeasureCommandSizeWriteEventQueue0400Nid =
    "sSAUCCU1dv4";
inline constexpr auto kAmprMeasureCommandSizeWriteEventQueueCompletionNid =
    "Zi3dBUjgyXI";
inline constexpr auto kAmprMeasureCommandSizeWriteAddressCompletionNid =
    "C+IEj+BsAFM";
inline constexpr auto kAmprMeasureCommandSizeWriteAddress0400Nid =
    "4fgtGfXDrFc";
inline constexpr auto kAmprCommandBufferGetSizeNid = "tZDDEo2tE5k";
inline constexpr auto kAmprCommandBufferGetCurrentOffsetNid = "GnxKOHEawhk";
inline constexpr auto kAmprCommandBufferGetNumCommandsNid = "gzndltBEzWc";
inline constexpr auto kAmprCommandBufferWriteEventQueue0400Nid = "H896Pt-yB4I";
inline constexpr auto kAmprCommandBufferWriteEventQueueCompletionNid =
    "o67gODLFpls";
inline constexpr auto kAmprCommandBufferWriteAddressCompletionNid =
    "sJXyWHjP-F8";
inline constexpr auto kAmprCommandBufferWriteAddress0400Nid = "j0+3uJMxYJY";

inline constexpr std::size_t kRegisteredAmprFunctionCount = 19;

[[nodiscard]] ExportRegistryStatus RegisterAmprExports(
    ExportRegistry& registry, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory);

}  // namespace kajps5::hle
