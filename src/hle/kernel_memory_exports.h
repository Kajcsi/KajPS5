// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"

namespace kajps5::kernel {
class DirectMemoryService;
}

namespace kajps5::hle {

inline constexpr auto kKernelMprotectName = "sceKernelMprotect";
inline constexpr auto kKernelMprotectNid = "vSMAm3cxYTY";
inline constexpr auto kPosixMprotectName = "mprotect";
inline constexpr auto kPosixMprotectNid = "YQOfxL4QfeU";
inline constexpr auto kKernelMunmapName = "sceKernelMunmap";
inline constexpr auto kKernelMunmapNid = "cQke9UuBQOk";
inline constexpr auto kKernelMapNamedFlexibleMemoryName =
    "sceKernelMapNamedFlexibleMemory";
inline constexpr auto kKernelMapNamedFlexibleMemoryNid = "mL8NDH86iQI";
inline constexpr auto kKernelMapFlexibleMemoryName =
    "sceKernelMapFlexibleMemory";
inline constexpr auto kKernelMapFlexibleMemoryNid = "IWIBBdTHit4";
inline constexpr auto kKernelMapFlexibleMemoryInternalName =
    "sceKernelMapNamedFlexibleMemoryInternal";
inline constexpr auto kKernelMapFlexibleMemoryInternalNid = "4h6F1LLbTiw";
inline constexpr auto kKernelGetDirectMemorySizeName =
    "sceKernelGetDirectMemorySize";
inline constexpr auto kKernelGetDirectMemorySizeNid = "pO96TwzOm5E";
inline constexpr auto kKernelAvailableDirectMemorySizeName =
    "sceKernelAvailableDirectMemorySize";
inline constexpr auto kKernelAvailableDirectMemorySizeNid = "C0f7TJcbfac";
inline constexpr auto kKernelAllocateDirectMemoryName =
    "sceKernelAllocateDirectMemory";
inline constexpr auto kKernelAllocateDirectMemoryNid = "rTXw65xmLIA";
inline constexpr auto kKernelAllocateMainDirectMemoryName =
    "sceKernelAllocateMainDirectMemory";
inline constexpr auto kKernelAllocateMainDirectMemoryNid = "B+vc2AO2Zrc";
inline constexpr auto kKernelReleaseDirectMemoryName =
    "sceKernelReleaseDirectMemory";
inline constexpr auto kKernelReleaseDirectMemoryNid = "MBuItvba6z8";
inline constexpr auto kKernelCheckedReleaseDirectMemoryName =
    "sceKernelCheckedReleaseDirectMemory";
inline constexpr auto kKernelCheckedReleaseDirectMemoryNid = "hwVSPCmp5tM";
inline constexpr auto kPosixMunmapName = "munmap";
inline constexpr auto kPosixMunmapNid = "UqDGjXA5yUM";
inline constexpr auto kPosixGetPageSizeName = "getpagesize";
inline constexpr auto kPosixGetPageSizeNid = "k+AXqu2-eBc";
inline constexpr auto kKernelQueryMemoryProtectionName =
    "sceKernelQueryMemoryProtection";
inline constexpr auto kKernelQueryMemoryProtectionNid = "WFcfL2lzido";
inline constexpr std::uint64_t kKernelMemoryPageSize = 0x4000;
inline constexpr std::uint32_t kKernelProtectionCpuRead = 0x01;
inline constexpr std::uint32_t kKernelProtectionCpuWrite = 0x02;
inline constexpr std::uint32_t kKernelProtectionCpuExecute = 0x04;
inline constexpr std::uint32_t kKernelProtectionGpuRead = 0x10;
inline constexpr std::uint32_t kKernelProtectionGpuWrite = 0x20;
inline constexpr std::uint32_t kKernelMapFixed = 0x10;
inline constexpr std::uint32_t kKernelMapNoOverwrite = 0x80;

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelMemoryExports(
    kernel::DirectMemoryService& direct_memory);
}

[[nodiscard]] ExportRegistryStatus RegisterKernelMemoryExports(
    ExportRegistry& registry, kernel::DirectMemoryService& direct_memory);

}  // namespace kajps5::hle
