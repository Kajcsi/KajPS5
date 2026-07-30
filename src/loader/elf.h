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
  kSelfHeaderOutOfRange,
  kUnsupportedSelfHeader,
  kSelfEmbeddedElfOutOfRange,
  kSelfSegmentMappingNotFound,
  kMultipleSelfSegmentMappings,
  kSelfSegmentFileRangeOutOfRange,
  kUnsupportedEncryptedSelfSegment,
  kUnsupportedCompressedSelfSegment,
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
  kMultipleSceDynlibDataSegments,
  kDynamicSegmentFileRangeOutOfRange,
  kSceDynlibDataSegmentFileRangeOutOfRange,
  kInvalidDynamicSegmentSize,
  kUnterminatedDynamicTable,
  kIncompleteDynamicStringTable,
  kDynamicStringTableNotFileBacked,
  kDynamicStringOffsetOutOfRange,
  kUnterminatedDynamicString,
  kDecodedStringBudgetExceeded,
  kIncompleteRelaMetadata,
  kInvalidRelaEntrySize,
  kRelaTableNotFileBacked,
  kRelocationTargetOutOfRange,
  kUnsupportedPltRelocationFormat,
  kIncompleteDynamicSymbolMetadata,
  kInvalidSymbolEntrySize,
  kInvalidSymbolTableSize,
  kHashTableNotFileBacked,
  kSymbolTableNotFileBacked,
  kSymbolNameOffsetOutOfRange,
  kUnterminatedSymbolName,
  kSegmentFileSizeExceedsMemorySize,
  kSegmentFileRangeOutOfRange,
  kSegmentAddressRangeOverflow,
  kInvalidSegmentAlignment,
  kOverlappingLoadSegments,
  kGuestRangeOutOfRange,
  kGuestMappingConflict,
  kLoadSizeOverflow,
};

enum class ElfContainerKind {
  kElf,
  kSelf,
};

struct ElfProgramHeader {
  std::uint32_t type = 0;
  std::uint32_t flags = 0;
  std::uint64_t offset = 0;
  std::uint64_t file_offset = 0;
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

struct ElfRelaEntry {
  std::uint64_t offset = 0;
  std::uint64_t info = 0;
  std::int64_t addend = 0;

  [[nodiscard]] std::uint32_t symbol() const noexcept {
    return static_cast<std::uint32_t>(info >> 32U);
  }
  [[nodiscard]] std::uint32_t type() const noexcept {
    return static_cast<std::uint32_t>(info);
  }
};

struct ElfSymbol {
  std::uint32_t name_offset = 0;
  std::uint8_t info = 0;
  std::uint8_t other = 0;
  std::uint16_t section_index = 0;
  std::uint64_t value = 0;
  std::uint64_t size = 0;
  std::string name;

  [[nodiscard]] std::uint8_t binding() const noexcept { return info >> 4U; }
  [[nodiscard]] std::uint8_t type() const noexcept { return info & 0x0fU; }
};

enum class ElfDynamicDataSource {
  kNone,
  kLoadSegment,
  kSceDynlibData,
};

struct ElfModuleIdentity {
  std::uint16_t id = 0;
  std::uint8_t version_major = 0;
  std::uint8_t version_minor = 0;
  std::string name;
};

struct ElfLibraryIdentity {
  std::uint16_t id = 0;
  std::uint16_t version = 0;
  std::string name;
};

struct ElfDynamicInfo {
  ElfDynamicDataSource string_table_source = ElfDynamicDataSource::kNone;
  std::optional<std::uint64_t> string_table_file_offset;
  std::optional<std::uint64_t> string_table_size;
  std::vector<std::string> needed_libraries;
  std::optional<std::string> shared_object_name;
  std::vector<ElfModuleIdentity> import_modules;
  std::vector<ElfModuleIdentity> export_modules;
  std::vector<ElfLibraryIdentity> import_libraries;
  std::vector<ElfLibraryIdentity> export_libraries;
  std::vector<ElfRelaEntry> relocations;
  std::vector<ElfRelaEntry> plt_relocations;
  std::vector<ElfSymbol> symbols;
  std::optional<std::uint64_t> init_function;
  std::optional<std::uint64_t> fini_function;
  std::optional<std::uint64_t> preinit_array_address;
  std::optional<std::uint64_t> preinit_array_size;
  std::optional<std::uint64_t> init_array_address;
  std::optional<std::uint64_t> init_array_size;
  std::optional<std::uint64_t> fini_array_address;
  std::optional<std::uint64_t> fini_array_size;
};

struct ElfMetadata {
  ElfContainerKind container = ElfContainerKind::kElf;
  std::uint64_t elf_file_offset = 0;
  std::uint16_t self_segment_count = 0;
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
[[nodiscard]] ElfParseResult ParseExecutable64(
    std::span<const std::byte> image);
[[nodiscard]] ElfLoadRangeResult CalculateElfLoadRange(
    const ElfMetadata& metadata) noexcept;
[[nodiscard]] ElfLoadResult LoadElf64(std::span<const std::byte> image,
                                      memory::GuestMemory& memory,
                                      std::uint64_t load_bias = 0);
[[nodiscard]] ElfLoadResult LoadExecutable64(std::span<const std::byte> image,
                                             memory::GuestMemory& memory,
                                             std::uint64_t load_bias = 0);
[[nodiscard]] std::string_view ElfErrorName(ElfError error) noexcept;

}  // namespace kajps5::loader
