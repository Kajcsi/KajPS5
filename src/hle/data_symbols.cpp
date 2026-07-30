// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/data_symbols.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hle/kernel_exports.h"
#include "hle/libc_exports.h"

namespace kajps5::hle {
namespace {

constexpr std::uint64_t kStackGuardOffset = 0;
constexpr std::uint64_t kLibcNeedFlagOffset = 16;
constexpr std::uint64_t kLibcInternalNeedFlagOffset = 20;
constexpr std::uint64_t kProgramNamePointerOffset = 24;
constexpr std::uint64_t kProgramNameBufferOffset = 32;
constexpr std::size_t kMaximumProgramNameBytes = 511;
constexpr std::string_view kDefaultProgramName = "eboot.bin";
constexpr std::string_view kLibcInternalName = "LibcInternal";

template <typename Value>
void WriteLittleEndian(std::span<std::byte> page, std::size_t offset,
                       Value value) {
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    page[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

HleDataResult Failure(HleDataStatus status) noexcept {
  HleDataResult result;
  result.status = status;
  return result;
}

}  // namespace

HleDataResult InstallHleDataSymbols(ImportRegistry& registry,
                                    memory::GuestMemory& memory,
                                    std::uint64_t page_address,
                                    std::string_view process_image_name) {
  if (page_address == 0 || (page_address % kHleDataPageSize) != 0 ||
      process_image_name.find('\0') != std::string_view::npos) {
    return Failure(HleDataStatus::kInvalidArgument);
  }
  if (process_image_name.empty()) {
    process_image_name = kDefaultProgramName;
  }
  if (!memory.Map(page_address, kHleDataPageSize,
                  memory::GuestMemoryProtection::kRead |
                      memory::GuestMemoryProtection::kWrite)) {
    return Failure(HleDataStatus::kMapFailed);
  }

  std::array<std::byte, kHleDataPageSize> page{};
  WriteLittleEndian(std::span(page), kStackGuardOffset, kHleStackGuardValue);
  WriteLittleEndian(std::span(page), kStackGuardOffset + sizeof(std::uint64_t),
                    kHleStackGuardValue);
  WriteLittleEndian(std::span(page), kLibcNeedFlagOffset, std::uint32_t{1});
  WriteLittleEndian(std::span(page), kLibcInternalNeedFlagOffset,
                    std::uint32_t{1});
  WriteLittleEndian(std::span(page), kProgramNamePointerOffset,
                    page_address + kProgramNameBufferOffset);
  const auto name_size =
      std::min(process_image_name.size(), kMaximumProgramNameBytes);
  std::transform(process_image_name.begin(),
                 process_image_name.begin() +
                     static_cast<std::ptrdiff_t>(name_size),
                 page.begin() + kProgramNameBufferOffset,
                 [](char value) {
                   return static_cast<std::byte>(
                       static_cast<unsigned char>(value));
                 });

  if (!memory.Initialize(page_address, page)) {
    (void)memory.Unmap(page_address, kHleDataPageSize);
    return Failure(HleDataStatus::kWriteFailed);
  }

  std::vector<ImportDefinition> definitions;
  definitions.reserve(4);
  definitions.push_back({kLibKernelName, kHleStackGuardNid,
                         page_address + kStackGuardOffset});
  definitions.push_back({kLibKernelName, kHleProgramNameNid,
                         page_address + kProgramNamePointerOffset});
  definitions.push_back({kLibcName, kHleLibcNeedFlagNid,
                         page_address + kLibcNeedFlagOffset});
  definitions.push_back({std::string(kLibcInternalName),
                         kHleLibcInternalNeedFlagNid,
                         page_address + kLibcInternalNeedFlagOffset});
  if (registry.RegisterBatch(std::move(definitions)) !=
      ImportRegistryStatus::kOk) {
    (void)memory.Unmap(page_address, kHleDataPageSize);
    return Failure(HleDataStatus::kRegistryConflict);
  }

  return {HleDataStatus::kOk,
          page_address,
          page_address + kStackGuardOffset,
          page_address + kProgramNamePointerOffset,
          page_address + kLibcNeedFlagOffset,
          page_address + kLibcInternalNeedFlagOffset};
}

std::string_view HleDataStatusName(HleDataStatus status) noexcept {
  switch (status) {
    case HleDataStatus::kOk: return "ok";
    case HleDataStatus::kInvalidArgument: return "invalid-argument";
    case HleDataStatus::kMapFailed: return "map-failed";
    case HleDataStatus::kWriteFailed: return "write-failed";
    case HleDataStatus::kRegistryConflict: return "registry-conflict";
  }
  return "unknown";
}

}  // namespace kajps5::hle
