// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"

namespace kajps5::kernel {
class CxaGuardService;
class LibcHeapService;
class ProcessLifecycleService;
}

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::hle {

inline constexpr auto kLibcName = "libc";
inline constexpr auto kCxaGuardAcquireName = "__cxa_guard_acquire";
inline constexpr auto kCxaGuardAcquireNid = "3GPpjQdAMTw";
inline constexpr auto kCxaGuardReleaseName = "__cxa_guard_release";
inline constexpr auto kCxaGuardReleaseNid = "9rAeANT2tyE";
inline constexpr auto kCxaGuardAbortName = "__cxa_guard_abort";
inline constexpr auto kCxaGuardAbortNid = "2emaaluWzUw";
inline constexpr auto kCxaPureVirtualName = "__cxa_pure_virtual";
inline constexpr auto kCxaPureVirtualNid = "zr094EQ39Ww";
inline constexpr auto kLibcInitEnvName = "_init_env";
inline constexpr auto kLibcInitEnvNid = "bzQExy189ZI";
inline constexpr auto kLibcAtexitName = "atexit";
inline constexpr auto kLibcAtexitNid = "8G2LB+A3rzg";
inline constexpr auto kLibcCxaAtexitName = "__cxa_atexit";
inline constexpr auto kLibcCxaAtexitNid = "tsvEmnenz48";
inline constexpr auto kLibcCatchReturnFromMainName = "catchReturnFromMain";
inline constexpr auto kLibcCatchReturnFromMainNid = "XKRegsFpEpk";
inline constexpr auto kLibcExitName = "exit";
inline constexpr auto kLibcExitNid = "uMei1W9uyNo";
inline constexpr auto kLibcAbortName = "abort";
inline constexpr auto kLibcAbortNid = "L1SBTkC+Cvw";
inline constexpr auto kLibcMallocName = "malloc";
inline constexpr auto kLibcMallocNid = "gQX+4GDQjpM";
inline constexpr auto kLibcFreeName = "free";
inline constexpr auto kLibcFreeNid = "tIhsqj0qsFE";
inline constexpr auto kLibcCallocName = "calloc";
inline constexpr auto kLibcCallocNid = "2X5agFjKxMc";
inline constexpr auto kLibcReallocName = "realloc";
inline constexpr auto kLibcReallocNid = "Y7aJ1uydPMo";
inline constexpr auto kLibcMemalignName = "memalign";
inline constexpr auto kLibcMemalignNid = "Ujf3KzMvRmI";
inline constexpr auto kLibcAlignedAllocName = "aligned_alloc";
inline constexpr auto kLibcAlignedAllocNid = "2Btkg8k24Zg";
inline constexpr auto kLibcPosixMemalignName = "posix_memalign";
inline constexpr auto kLibcPosixMemalignNid = "cVSk9y8URbc";
inline constexpr auto kLibcMallocUsableSizeName = "malloc_usable_size";
inline constexpr auto kLibcMallocUsableSizeNid = "NDcSfcYZRC8";
inline constexpr auto kLibcMemsetName = "memset";
inline constexpr auto kLibcMemsetNid = "8zTFvBIAIN8";
inline constexpr auto kLibcMemcpyName = "memcpy";
inline constexpr auto kLibcMemcpyNid = "Q3VBxCXhUHs";
inline constexpr auto kLibcMemmoveName = "memmove";
inline constexpr auto kLibcMemmoveNid = "+P6FRGH4LfA";
inline constexpr auto kLibcStrcmpName = "strcmp";
inline constexpr auto kLibcStrcmpNid = "Ovb2dSJOAuE";
inline constexpr auto kLibcStrlenName = "strlen";
inline constexpr auto kLibcStrlenNid = "j4ViWNHEgww";
inline constexpr auto kLibcWcscmpName = "wcscmp";
inline constexpr auto kLibcWcscmpNid = "pNtJdE3x49E";
inline constexpr auto kLibcAtofName = "atof";
inline constexpr auto kLibcAtofNid = "SRI6S9B+-a4";
inline constexpr auto kLibcExp2fName = "exp2f";
inline constexpr auto kLibcExp2fNid = "wuAQt-j+p4o";
inline constexpr auto kLibcSinfName = "sinf";
inline constexpr auto kLibcSinfNid = "Q4rRL34CEeE";
inline constexpr auto kLibcCosfName = "cosf";
inline constexpr auto kLibcCosfNid = "-P6FNMzk2Kc";
inline constexpr auto kLibcAtanName = "atan";
inline constexpr auto kLibcAtanNid = "OXmauLdQ8kY";
inline constexpr auto kLibcAtan2Name = "atan2";
inline constexpr auto kLibcAtan2Nid = "HUbZmOnT-Dg";
inline constexpr auto kLibcAcosfName = "acosf";
inline constexpr auto kLibcAcosfNid = "QI-x0SL8jhw";
inline constexpr auto kLibcTanfName = "tanf";
inline constexpr auto kLibcTanfNid = "ZE6RNL+eLbk";
inline constexpr auto kLibcSincosName = "sincos";
inline constexpr auto kLibcSincosNid = "jMB7EFyu30Y";
inline constexpr auto kLibcSincosfName = "sincosf";
inline constexpr auto kLibcSincosfNid = "pztV4AF18iI";
inline constexpr auto kLibcOperatorNewName = "_Znwm";
inline constexpr auto kLibcOperatorNewNid = "fJnpuVVBbKk";
inline constexpr auto kLibcOperatorDeleteName = "_ZdlPv";
inline constexpr auto kLibcOperatorDeleteNid = "z+P+xCnWLBk";
inline constexpr auto kLibcOperatorNewArrayName = "_Znam";
inline constexpr auto kLibcOperatorNewArrayNid = "hdm0YfMa7TQ";
inline constexpr auto kLibcOperatorDeleteArrayName = "_ZdaPv";
inline constexpr auto kLibcOperatorDeleteArrayNid = "MLWl90SFWNE";
inline constexpr auto kLibcMspaceCreateName = "sceLibcMspaceCreate";
inline constexpr auto kLibcMspaceCreateNid = "-hn1tcVHq5Q";
inline constexpr auto kLibcMspaceDestroyName = "sceLibcMspaceDestroy";
inline constexpr auto kLibcMspaceDestroyNid = "W6SiVSiCDtI";
inline constexpr auto kLibcMspaceMallocName = "sceLibcMspaceMalloc";
inline constexpr auto kLibcMspaceMallocNid = "OJjm-QOIHlI";
inline constexpr auto kLibcMspaceFreeName = "sceLibcMspaceFree";
inline constexpr auto kLibcMspaceFreeNid = "Vla-Z+eXlxo";
inline constexpr auto kLibcMspaceCallocName = "sceLibcMspaceCalloc";
inline constexpr auto kLibcMspaceCallocNid = "LYo3GhIlB38";
inline constexpr auto kLibcMspaceReallocName = "sceLibcMspaceRealloc";
inline constexpr auto kLibcMspaceReallocNid = "gigoVHZvVPE";
inline constexpr auto kLibcMspaceMemalignName = "sceLibcMspaceMemalign";
inline constexpr auto kLibcMspaceMemalignNid = "iF1iQHzxBJU";
inline constexpr auto kLibcMspacePosixMemalignName =
    "sceLibcMspacePosixMemalign";
inline constexpr auto kLibcMspacePosixMemalignNid = "qWESlyXMI3E";
inline constexpr auto kLibcMspaceReallocalignName =
    "sceLibcMspaceReallocalign";
inline constexpr auto kLibcMspaceReallocalignNid = "p6lrRW8-MLY";
inline constexpr auto kLibcMspaceMallocUsableSizeName =
    "sceLibcMspaceMallocUsableSize";
inline constexpr auto kLibcMspaceMallocUsableSizeNid = "fEoW6BJsPt4";
inline constexpr auto kLibcMspaceIsHeapEmptyName =
    "sceLibcMspaceIsHeapEmpty";
inline constexpr auto kLibcMspaceIsHeapEmptyNid = "pzUa7KEoydw";

[[nodiscard]] ExportRegistryStatus RegisterLibcExports(
    ExportRegistry& registry, kernel::CxaGuardService& guards,
    kernel::ProcessLifecycleService& lifecycle,
    kernel::LibcHeapService& heap, memory::GuestMemory& memory);

}  // namespace kajps5::hle
