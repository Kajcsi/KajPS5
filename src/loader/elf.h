// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"

namespace kajps5::loader {

enum class ElfError {
  kNone,
  kImageTooSmall,
  kInvalidMagic,
  kUnsupportedClass,
  kUnsupportedEndianness,
  kUnsupportedIdentVersion,
  kUnsupportedAbi,
  kUnsupportedFileType,
  kUnsupportedMachine,
  kUnsupportedVersion,
  kInvalidHeaderSize,
  kInvalidProgramHeaderSize,
  kProgramHeaderTableOutOfRange,
  kMultipleDynamicSegments,
  kDynamicSegmentFileRangeOutOfRange,
  kInvalidDynamicSegmentSize,
  kUnterminatedDynamicTable,
  kIncompleteDynamicStringTable,
  kDynamicStringTableNotFileBacked,
  kDynamicStringOffsetOutOfRange,
  kUnterminatedDynamicString,
  kSegmentFileSizeExceedsMemorySize,
  kSegmentFileRangeOutOfRange,
  kSegmentAddressRangeOverflow,
  kInvalidSegmentAlignment,
  kOverlappingLoadSegments,
  kGuestRangeOutOfRange,
  kGuestMappingConflict,
  kLoadSizeOverflow,
};

struct ElfProgramHeader {
  std::uint32_t type = 0;
  std::uint32_t flags = 0;
  std::uint64_t offset = 0;
  std::uint64_t virtual_address = 0;
  std::uint64_t physical_address = 0;
  std::uint64_t file_size = 0;
  std::uint64_t memory_size = 0;
  std::uint64_t alignment = 0;
};

struct ElfDynamicEntry {
  std::int64_t tag = 0;
  std::uint64_t value = 0;
};

struct ElfDynamicInfo {
  std::optional<std::uint64_t> string_table_address;
  std::optional<std::uint64_t> string_table_size;
  std::vector<std::string> needed_libraries;
  std::optional<std::string> shared_object_name;
};

struct ElfMetadata {
  std::uint8_t os_abi = 0;
  std::uint8_t abi_version = 0;
  std::uint16_t type = 0;
  std::uint16_t machine = 0;
  std::uint32_t version = 0;
  std::uint64_t entry_point = 0;
  std::vector<ElfProgramHeader> program_headers;
  std::vector<ElfDynamicEntry> dynamic_entries;
  ElfDynamicInfo dynamic_info;
};

struct ElfParseResult {
  ElfError error = ElfError::kNone;
  ElfMetadata metadata;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ElfError::kNone;
  }
};

struct ElfLoadResult {
  ElfError error = ElfError::kNone;
  ElfMetadata metadata;
  std::uint64_t loaded_segment_count = 0;
  std::uint64_t loaded_file_bytes = 0;
  std::uint64_t zero_filled_bytes = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ElfError::kNone;
  }
};

struct ElfLoadRangeResult {
  ElfError error = ElfError::kNone;
  std::uint64_t base_address = 0;
  std::uint64_t size = 0;
  std::uint64_t load_segment_count = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ElfError::kNone;
  }
};

[[nodiscard]] ElfParseResult ParseElf64(
    std::span<const std::byte> image);
[[nodiscard]] ElfLoadRangeResult CalculateElfLoadRange(
    const ElfMetadata& metadata) noexcept;
[[nodiscard]] ElfLoadResult LoadElf64(std::span<const std::byte> image,
                                      memory::GuestMemory& memory);
[[nodiscard]] std::string_view ElfErrorName(ElfError error) noexcept;

}  // namespace kajps5::loader
