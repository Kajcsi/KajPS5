// Copyright (C) 2026 KajPS5 contributors
// Architecture adapted from KytyPS5 src/libs/libVideoOut.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203. Behavior adapted from SharpEmu
// src/SharpEmu.Libs/VideoOut/VideoOutExports.cs at
// 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>

#include "hle/export_registry.h"

namespace kajps5::gpu {
class VideoOutService;
}

namespace kajps5::hle {

inline constexpr auto kVideoOutLibraryName = "libSceVideoOut";
inline constexpr auto kVideoOutOpenNid = "Up36PTk687E";
inline constexpr auto kVideoOutCloseNid = "uquVH4-Du78";
inline constexpr auto kVideoOutSetFlipRateNid = "CBiu4mCE1DA";
inline constexpr auto kVideoOutSetBufferAttributeNid = "i6-sR91Wt-4";
inline constexpr auto kVideoOutSetBufferAttribute2Nid = "PjS5uASwcV8";
inline constexpr auto kVideoOutRegisterBuffersNid = "w3BY+tAEiQY";
inline constexpr auto kVideoOutRegisterBuffers2Nid = "rKBUtgRrtbk";
inline constexpr auto kVideoOutUnregisterBuffersNid = "N5KDtkIjjJ4";
inline constexpr auto kVideoOutAddFlipEventNid = "HXzjK9yI30k";
inline constexpr auto kVideoOutDeleteFlipEventNid = "-Ozn0F1AFRg";
inline constexpr auto kVideoOutSubmitFlipNid = "U46NwOiJpys";
inline constexpr auto kVideoOutGetFlipStatusNid = "SbU3dwp80lQ";
inline constexpr auto kVideoOutIsFlipPendingNid = "zgXifHT9ErY";
inline constexpr std::size_t kRegisteredVideoOutFunctionCount = 13;

[[nodiscard]] ExportRegistryStatus RegisterVideoOutExports(
    ExportRegistry& registry, gpu::VideoOutService& video_out);

}  // namespace kajps5::hle
