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

namespace kajps5::kernel {
class EventQueueService;
}

namespace kajps5::hle {

inline constexpr auto kAgcLibraryName = "libSceAgc";
inline constexpr auto kAgcDriverLibraryName = "libSceAgcDriver";
inline constexpr auto kAgcCbNopNid = "LtTouSCZjHM";
inline constexpr auto kAgcCbNopGetSizeNid = "t7PlZ9nt5Lc";
inline constexpr auto kAgcCbDispatchNid = "k3GhuSNmBLU";
inline constexpr auto kAgcCbDispatchGetSizeNid = "Abendgtz+3o";
inline constexpr auto kAgcGetPacketSizeNid = "Lkf86B98qPc";
inline constexpr auto kAgcSetPacketPredicationNid = "w6Dj1VJt5qY";
inline constexpr auto kAgcDcbSetShRegisterDirectNid = "pFLArOT53+w";
inline constexpr auto kAgcDcbSetCxRegisterDirectNid = "LHFXRrlTPD8";
inline constexpr auto kAgcDcbSetCxRegisterDirectGetSizeNid = "1DeUNpRIDDA";
inline constexpr auto kAgcDcbSetUcRegisterDirectNid = "w4-d0n60hdo";
inline constexpr auto kAgcDcbSetIndexSizeNid = "GIIW2J37e70";
inline constexpr auto kAgcDcbSetIndexBufferNid = "l4fM9K-Lyks";
inline constexpr auto kAgcDcbSetIndexCountNid = "8N2tmT3jmC8";
inline constexpr auto kAgcDcbSetNumInstancesNid = "tSBxhAPyytQ";
inline constexpr auto kAgcDcbSetNumInstancesGetSizeNid = "6DFuRKT4C9w";
inline constexpr auto kAgcDcbDrawIndexNid = "q88lQ+GP5Yk";
inline constexpr auto kAgcDcbDrawIndexGetSizeNid = "6ee9Hd3EWXQ";
inline constexpr auto kAgcDcbDrawIndexMultiInstancedNid = "Rlx+bykm0r0";
inline constexpr auto kAgcDcbDrawIndexMultiInstancedGetSizeNid =
    "mR9j7+SfM34";
inline constexpr auto kAgcDcbDrawIndexAutoNid = "Yw0jKSqop+E";
inline constexpr auto kAgcDcbDrawIndexAutoGetSizeNid = "WrdP9Zxx3lQ";
inline constexpr auto kAgcDcbDrawIndexOffsetNid = "B+aG9DUnTKA";
inline constexpr auto kAgcDcbDrawIndexOffsetGetSizeNid = "qMlfB1ZhMDc";
inline constexpr auto kAgcDcbSetBaseIndirectArgsNid = "RmaJwLtc8rY";
inline constexpr auto kAgcDcbDispatchIndirectNid = "CtB+A9-VxO0";
inline constexpr auto kAgcDcbDispatchIndirectGetSizeNid = "w8HVkEeXPv8";
inline constexpr auto kAgcDcbJumpNid = "xSAR0LTcRKM";
inline constexpr auto kAgcDcbJumpGetSizeNid = "VEGu4dixjUg";
inline constexpr auto kAgcDcbRewindGetSizeNid = "QIXCsbipds0";
inline constexpr auto kAgcDcbRewindNid = "zfcxg-ewMK8";
inline constexpr auto kAgcDcbSetPredicationNid = "bbFueFP+J4k";
inline constexpr auto kAgcDcbWriteDataNid = "i1jyy49AjXU";
inline constexpr auto kAgcDcbWriteDataGetSizeNid = "p9tI+yTvx68";
inline constexpr auto kAgcCbReleaseMemNid = "wr23dPKyWc0";
inline constexpr auto kAgcCbQueueEndOfPipeActionGetSizeNid = "hL7C0IRpWZI";
inline constexpr auto kAgcDcbEventWriteNid = "aJf+j5yntiU";
inline constexpr auto kAgcAcbEventWriteNid = "cFazmnXpJOE";
inline constexpr auto kAgcDcbGetLodStatsNid = "vuSXe69VILM";
inline constexpr auto kAgcDcbWaitRegMemNid = "VmW0Tdpy420";
inline constexpr auto kAgcDcbWaitOnAddressGetSizeNid = "43WJ08sSugE";
inline constexpr auto kAgcDriverSubmitDcbNid = "UglJIZjGssM";
inline constexpr auto kAgcDriverSubmitAcbNid = "gSRnr79F8tQ";
inline constexpr auto kAgcDriverSubmitMultiDcbsNid = "6UzEidRZwkg";
inline constexpr auto kAgcDriverAgrSubmitMultiDcbsNid = "+T8Xo6LtFJI";
inline constexpr auto kAgcDriverSubmitCommandBufferNid = "b4fpgH5ZXxQ";
inline constexpr auto kAgcDriverSubmitMultiCommandBuffersNid =
    "Fj7r9EHzF38";
inline constexpr auto kAgcDriverSubmitMultiAcbsNid = "HF3YllT3mXU";
inline constexpr auto kAgcDriverAddEqEventNid = "w2rJhmD+dsE";
inline constexpr auto kAgcDriverDeleteEqEventNid = "DL2RXaXOy88";
inline constexpr auto kAgcDriverGetEqEventTypeNid = "5CdQTZIQPxM";
inline constexpr auto kAgcDriverGetEqContextIdNid = "Zw7uUVPulbw";
inline constexpr std::size_t kRegisteredAgcFunctionCount = 51;

[[nodiscard]] ExportRegistryStatus RegisterAgcExports(
    ExportRegistry& registry, gpu::GpuRuntime& gpu_runtime,
    kernel::EventQueueService& event_queues);

}  // namespace kajps5::hle
