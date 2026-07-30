// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"

namespace kajps5::kernel {
class GuestScheduler;
class PthreadService;
}

namespace kajps5::hle {

inline constexpr auto kPosixPthreadSelfName = "pthread_self";
inline constexpr auto kPosixPthreadSelfNid = "EotR8a3ASf4";
inline constexpr auto kKernelPthreadSelfName = "scePthreadSelf";
inline constexpr auto kKernelPthreadSelfNid = "aI+OeCz8xrQ";
inline constexpr auto kPosixPthreadEqualName = "pthread_equal";
inline constexpr auto kPosixPthreadEqualNid = "7Xl257M4VNI";
inline constexpr auto kKernelPthreadEqualName = "scePthreadEqual";
inline constexpr auto kKernelPthreadEqualNid = "3PtV6p3QNX4";
inline constexpr auto kPosixPthreadYieldName = "pthread_yield";
inline constexpr auto kPosixPthreadYieldNid = "B5GmVDKwpn0";
inline constexpr auto kKernelPthreadYieldName = "scePthreadYield";
inline constexpr auto kKernelPthreadYieldNid = "T72hz6ffq08";
inline constexpr auto kPosixPthreadCreateName = "pthread_create";
inline constexpr auto kPosixPthreadCreateNid = "OxhIB8LB-PQ";
inline constexpr auto kPosixPthreadCreateNameNpName =
    "pthread_create_name_np";
inline constexpr auto kPosixPthreadCreateNameNpNid = "Jmi+9w9u0E4";
inline constexpr auto kKernelPthreadCreateName = "scePthreadCreate";
inline constexpr auto kKernelPthreadCreateNid = "6UgtwV+0zb4";
inline constexpr auto kPosixPthreadJoinName = "pthread_join";
inline constexpr auto kPosixPthreadJoinNid = "h9CcP3J0oVM";
inline constexpr auto kKernelPthreadJoinName = "scePthreadJoin";
inline constexpr auto kKernelPthreadJoinNid = "onNY9Byn-W8";
inline constexpr auto kPosixPthreadExitName = "pthread_exit";
inline constexpr auto kPosixPthreadExitNid = "FJrT5LuUBAU";
inline constexpr auto kKernelPthreadExitName = "scePthreadExit";
inline constexpr auto kKernelPthreadExitNid = "3kg7rT0NQIs";
inline constexpr auto kLibScePosixName = "libScePosix";

inline constexpr auto kPosixPthreadAttrInitName = "pthread_attr_init";
inline constexpr auto kPosixPthreadAttrInitNid = "wtkt-teR1so";
inline constexpr auto kKernelPthreadAttrInitName = "scePthreadAttrInit";
inline constexpr auto kKernelPthreadAttrInitNid = "nsYoNRywwNg";
inline constexpr auto kPosixPthreadAttrDestroyName = "pthread_attr_destroy";
inline constexpr auto kPosixPthreadAttrDestroyNid = "zHchY8ft5pk";
inline constexpr auto kKernelPthreadAttrDestroyName = "scePthreadAttrDestroy";
inline constexpr auto kKernelPthreadAttrDestroyNid = "62KCwEMmzcM";
inline constexpr auto kPosixPthreadAttrSetstacksizeName =
    "pthread_attr_setstacksize";
inline constexpr auto kPosixPthreadAttrSetstacksizeNid = "2Q0z6rnBrTE";
inline constexpr auto kKernelPthreadAttrSetstacksizeName =
    "scePthreadAttrSetstacksize";
inline constexpr auto kKernelPthreadAttrSetstacksizeNid = "UTXzJbWhhTE";
inline constexpr auto kPosixPthreadAttrGetstacksizeName =
    "pthread_attr_getstacksize";
inline constexpr auto kPosixPthreadAttrGetstacksizeNid = "0qOtCR-ZHck";
inline constexpr auto kKernelPthreadAttrGetstacksizeName =
    "scePthreadAttrGetstacksize";
inline constexpr auto kKernelPthreadAttrGetstacksizeNid = "-fA+7ZlGDQs";

inline constexpr auto kPosixPthreadKeyCreateName = "pthread_key_create";
inline constexpr auto kPosixPthreadKeyCreateNid = "mqULNdimTn0";
inline constexpr auto kKernelPthreadKeyCreateName = "scePthreadKeyCreate";
inline constexpr auto kKernelPthreadKeyCreateNid = "geDaqgH9lTg";
inline constexpr auto kPosixPthreadKeyDeleteName = "pthread_key_delete";
inline constexpr auto kPosixPthreadKeyDeleteNid = "6BpEZuDT7YI";
inline constexpr auto kKernelPthreadKeyDeleteName = "scePthreadKeyDelete";
inline constexpr auto kKernelPthreadKeyDeleteNid = "PrdHuuDekhY";
inline constexpr auto kPosixPthreadSetspecificName = "pthread_setspecific";
inline constexpr auto kPosixPthreadSetspecificNid = "WrOLvHU0yQM";
inline constexpr auto kKernelPthreadSetspecificName = "scePthreadSetspecific";
inline constexpr auto kKernelPthreadSetspecificNid = "+BzXYkqYeLE";
inline constexpr auto kPosixPthreadGetspecificName = "pthread_getspecific";
inline constexpr auto kPosixPthreadGetspecificNid = "0-KXaS70xy4";
inline constexpr auto kKernelPthreadGetspecificName = "scePthreadGetspecific";
inline constexpr auto kKernelPthreadGetspecificNid = "eoht7mQOCmo";

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelPthreadExports(
    kernel::PthreadService& pthreads, kernel::GuestScheduler& scheduler);
}

// The pthread service and scheduler must outlive registry dispatches.
[[nodiscard]] ExportRegistryStatus RegisterKernelPthreadExports(
    ExportRegistry& registry, kernel::PthreadService& pthreads,
    kernel::GuestScheduler& scheduler);

}  // namespace kajps5::hle
