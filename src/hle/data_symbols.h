// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

#include "core/memory/guest_memory.h"
#include "hle/import_registry.h"

namespace kajps5::hle {

inline constexpr std::uint64_t kHleDataPageSize = 0x4000;
inline constexpr std::uint64_t kHleSanitizerMallocReplaceOffset = 0x300;
inline constexpr std::uint64_t kHleSanitizerMallocReplaceSize = 0x70;
inline constexpr std::uint64_t kHleSanitizerNewReplaceOffset = 0x400;
inline constexpr std::uint64_t kHleSanitizerNewReplaceSize = 0x68;
inline constexpr std::uint64_t kHleLibcHeapTraceStorageOffset = 0x500;
inline constexpr std::uint64_t kHleLibcHeapTraceStorageSize =
    sizeof(std::uint64_t) + 64 * sizeof(std::uint64_t);
inline constexpr std::uint64_t kHleStackGuardValue =
    0xc0dec0decafeba00;
inline constexpr auto kHleStackGuardNid = "f7uOxY9mM1U";
inline constexpr auto kHleProgramNameNid = "djxxOmW6-aw";
inline constexpr auto kHleLibcNeedFlagNid = "P330P3dFF68";
inline constexpr auto kHleLibcInternalNeedFlagNid = "ZT4ODD2Ts9o";

enum class HleDataStatus {
  kOk,
  kInvalidArgument,
  kMapFailed,
  kWriteFailed,
  kRegistryConflict,
};

struct HleDataResult {
  HleDataStatus status = HleDataStatus::kOk;
  std::uint64_t page_address = 0;
  std::uint64_t stack_guard_address = 0;
  std::uint64_t program_name_pointer_address = 0;
  std::uint64_t libc_need_flag_address = 0;
  std::uint64_t libc_internal_need_flag_address = 0;
  std::uint64_t sanitizer_malloc_replace_address = 0;
  std::uint64_t sanitizer_new_replace_address = 0;
  std::uint64_t libc_heap_trace_storage_address = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == HleDataStatus::kOk;
  }
};

[[nodiscard]] HleDataResult InstallHleDataSymbols(
    ImportRegistry& registry, memory::GuestMemory& memory,
    std::uint64_t page_address,
    std::string_view process_image_name = "eboot.bin");
[[nodiscard]] std::string_view HleDataStatusName(HleDataStatus status) noexcept;

}  // namespace kajps5::hle
