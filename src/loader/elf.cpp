// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/elf.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace kajps5::loader {
namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kElfDataLittleEndian = 1;
constexpr std::uint8_t kElfVersionCurrent = 1;
constexpr std::uint8_t kAbiSystemV = 0;
constexpr std::uint8_t kAbiFreeBsd = 9;
constexpr std::uint16_t kTypeExecutable = 2;
constexpr std::uint16_t kTypeDynamic = 3;
constexpr std::uint16_t kTypeSceExecutable = 0xfe10;
constexpr std::uint16_t kTypeSceDynamic = 0xfe18;
constexpr std::uint16_t kMachineX86_64 = 62;
constexpr std::uint32_t kProgramTypeLoad = 1;
constexpr std::uint32_t kProgramTypeDynamic = 2;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::int64_t kDynamicTagNull = 0;

std::uint8_t Read8(std::span<const std::byte> image,
                   std::size_t offset) noexcept {
  return std::to_integer<std::uint8_t>(image[offset]);
}

std::uint16_t Read16(std::span<const std::byte> image,
                     std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(Read8(image, offset)) |
         (static_cast<std::uint16_t>(Read8(image, offset + 1)) << 8U);
}

std::uint32_t Read32(std::span<const std::byte> image,
                     std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(Read8(image, offset + index))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> image,
                     std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(Read8(image, offset + index))
             << (index * 8U);
  }
  return value;
}

bool RangeWithin(std::uint64_t total_size, std::uint64_t offset,
                 std::uint64_t length) noexcept {
  return offset <= total_size && length <= total_size - offset;
}

bool IsSupportedAbi(std::uint8_t os_abi, std::uint8_t abi_version) noexcept {
  if (os_abi == kAbiSystemV) {
    return abi_version == 0;
  }
  if (os_abi == kAbiFreeBsd) {
    return abi_version == 0 || abi_version == 2;
  }
  return false;
}

bool IsSupportedType(std::uint16_t type) noexcept {
  return type == kTypeExecutable || type == kTypeDynamic ||
         type == kTypeSceExecutable || type == kTypeSceDynamic;
}

memory::GuestMemoryProtection ProtectionFromFlags(
    std::uint32_t flags) noexcept {
  auto protection = memory::GuestMemoryProtection::kNone;
  if ((flags & 4U) != 0) {
    protection = protection | memory::GuestMemoryProtection::kRead;
  }
  if ((flags & 2U) != 0) {
    protection = protection | memory::GuestMemoryProtection::kWrite;
  }
  if ((flags & 1U) != 0) {
    protection = protection | memory::GuestMemoryProtection::kExecute;
  }
  return protection;
}

ElfParseResult ParseFailure(ElfError error) noexcept {
  ElfParseResult result;
  result.error = error;
  return result;
}

}  // namespace

ElfParseResult ParseElf64(std::span<const std::byte> image) {
  if (image.size() < kElfHeaderSize) {
    return ParseFailure(ElfError::kImageTooSmall);
  }

  if (Read8(image, 0) != 0x7f || Read8(image, 1) != 'E' ||
      Read8(image, 2) != 'L' || Read8(image, 3) != 'F') {
    return ParseFailure(ElfError::kInvalidMagic);
  }
  if (Read8(image, 4) != kElfClass64) {
    return ParseFailure(ElfError::kUnsupportedClass);
  }
  if (Read8(image, 5) != kElfDataLittleEndian) {
    return ParseFailure(ElfError::kUnsupportedEndianness);
  }
  if (Read8(image, 6) != kElfVersionCurrent) {
    return ParseFailure(ElfError::kUnsupportedIdentVersion);
  }

  ElfMetadata metadata;
  metadata.os_abi = Read8(image, 7);
  metadata.abi_version = Read8(image, 8);
  metadata.type = Read16(image, 16);
  metadata.machine = Read16(image, 18);
  metadata.version = Read32(image, 20);
  metadata.entry_point = Read64(image, 24);
  const auto program_header_offset = Read64(image, 32);
  const auto header_size = Read16(image, 52);
  const auto program_header_size = Read16(image, 54);
  const auto program_header_count = Read16(image, 56);

  if (!IsSupportedAbi(metadata.os_abi, metadata.abi_version)) {
    return ParseFailure(ElfError::kUnsupportedAbi);
  }
  if (!IsSupportedType(metadata.type)) {
    return ParseFailure(ElfError::kUnsupportedFileType);
  }
  if (metadata.machine != kMachineX86_64) {
    return ParseFailure(ElfError::kUnsupportedMachine);
  }
  if (metadata.version != kElfVersionCurrent) {
    return ParseFailure(ElfError::kUnsupportedVersion);
  }
  if (header_size != kElfHeaderSize) {
    return ParseFailure(ElfError::kInvalidHeaderSize);
  }
  if (program_header_size != kProgramHeaderSize) {
    return ParseFailure(ElfError::kInvalidProgramHeaderSize);
  }

  const auto table_size =
      static_cast<std::uint64_t>(program_header_count) * program_header_size;
  if (!RangeWithin(static_cast<std::uint64_t>(image.size()),
                   program_header_offset, table_size)) {
    return ParseFailure(ElfError::kProgramHeaderTableOutOfRange);
  }

  metadata.program_headers.reserve(program_header_count);
  for (std::uint16_t index = 0; index < program_header_count; ++index) {
    const auto entry_offset64 =
        program_header_offset +
        static_cast<std::uint64_t>(index) * program_header_size;
    const auto entry_offset = static_cast<std::size_t>(entry_offset64);

    ElfProgramHeader header;
    header.type = Read32(image, entry_offset);
    header.flags = Read32(image, entry_offset + 4);
    header.offset = Read64(image, entry_offset + 8);
    header.virtual_address = Read64(image, entry_offset + 16);
    header.physical_address = Read64(image, entry_offset + 24);
    header.file_size = Read64(image, entry_offset + 32);
    header.memory_size = Read64(image, entry_offset + 40);
    header.alignment = Read64(image, entry_offset + 48);

    if (header.type == kProgramTypeLoad) {
      if (header.file_size > header.memory_size) {
        return ParseFailure(ElfError::kSegmentFileSizeExceedsMemorySize);
      }
      if (header.file_size != 0 &&
          !RangeWithin(static_cast<std::uint64_t>(image.size()),
                       header.offset, header.file_size)) {
        return ParseFailure(ElfError::kSegmentFileRangeOutOfRange);
      }
      if (header.memory_size >
          std::numeric_limits<std::uint64_t>::max() -
              header.virtual_address) {
        return ParseFailure(ElfError::kSegmentAddressRangeOverflow);
      }
      if (header.alignment > 1 &&
          (!std::has_single_bit(header.alignment) ||
           header.virtual_address % header.alignment !=
               header.offset % header.alignment)) {
        return ParseFailure(ElfError::kInvalidSegmentAlignment);
      }
    }

    metadata.program_headers.push_back(header);
  }

  std::vector<std::pair<std::uint64_t, std::uint64_t>> load_ranges;
  load_ranges.reserve(metadata.program_headers.size());
  for (const auto& header : metadata.program_headers) {
    if (header.type == kProgramTypeLoad && header.memory_size != 0) {
      load_ranges.emplace_back(header.virtual_address,
                               header.virtual_address + header.memory_size);
    }
  }
  std::sort(load_ranges.begin(), load_ranges.end());
  for (std::size_t index = 1; index < load_ranges.size(); ++index) {
    if (load_ranges[index].first < load_ranges[index - 1].second) {
      return ParseFailure(ElfError::kOverlappingLoadSegments);
    }
  }

  const ElfProgramHeader* dynamic_header = nullptr;
  for (const auto& header : metadata.program_headers) {
    if (header.type != kProgramTypeDynamic) {
      continue;
    }
    if (dynamic_header != nullptr) {
      return ParseFailure(ElfError::kMultipleDynamicSegments);
    }
    dynamic_header = &header;
  }

  if (dynamic_header != nullptr) {
    if (dynamic_header->file_size % kDynamicEntrySize != 0) {
      return ParseFailure(ElfError::kInvalidDynamicSegmentSize);
    }
    if (!RangeWithin(static_cast<std::uint64_t>(image.size()),
                     dynamic_header->offset, dynamic_header->file_size)) {
      return ParseFailure(ElfError::kDynamicSegmentFileRangeOutOfRange);
    }

    metadata.dynamic_entries.reserve(static_cast<std::size_t>(
        dynamic_header->file_size / kDynamicEntrySize));
    bool terminated = false;
    for (std::uint64_t relative_offset = 0;
         relative_offset < dynamic_header->file_size;
         relative_offset += kDynamicEntrySize) {
      const auto entry_offset =
          static_cast<std::size_t>(dynamic_header->offset + relative_offset);
      const auto tag = std::bit_cast<std::int64_t>(Read64(image, entry_offset));
      if (tag == kDynamicTagNull) {
        terminated = true;
        break;
      }
      metadata.dynamic_entries.push_back(
          {tag, Read64(image, entry_offset + sizeof(std::uint64_t))});
    }
    if (!terminated) {
      return ParseFailure(ElfError::kUnterminatedDynamicTable);
    }
  }

  ElfParseResult result;
  result.metadata = std::move(metadata);
  return result;
}

ElfLoadRangeResult CalculateElfLoadRange(
    const ElfMetadata& metadata) noexcept {
  ElfLoadRangeResult result;
  auto minimum_address = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_address = 0;

  for (const auto& header : metadata.program_headers) {
    if (header.type != kProgramTypeLoad || header.memory_size == 0) {
      continue;
    }
    if (header.memory_size >
        std::numeric_limits<std::uint64_t>::max() -
            header.virtual_address) {
      result.error = ElfError::kSegmentAddressRangeOverflow;
      return result;
    }

    const auto end_address = header.virtual_address + header.memory_size;
    minimum_address = std::min(minimum_address, header.virtual_address);
    maximum_address = std::max(maximum_address, end_address);
    ++result.load_segment_count;
  }

  if (result.load_segment_count == 0) {
    return result;
  }

  result.base_address = minimum_address;
  result.size = maximum_address - minimum_address;
  return result;
}

ElfLoadResult LoadElf64(std::span<const std::byte> image,
                        memory::GuestMemory& memory) {
  auto parsed = ParseElf64(image);
  ElfLoadResult result;
  result.error = parsed.error;
  result.metadata = std::move(parsed.metadata);
  if (result.error != ElfError::kNone) {
    return result;
  }

  for (const auto& header : result.metadata.program_headers) {
    if (header.type != kProgramTypeLoad || header.memory_size == 0) {
      continue;
    }
    if (!memory.Contains(header.virtual_address, header.memory_size)) {
      result.error = ElfError::kGuestRangeOutOfRange;
      return result;
    }
    if (!memory.CanMap(header.virtual_address, header.memory_size)) {
      result.error = ElfError::kGuestMappingConflict;
      return result;
    }
    const auto zero_size = header.memory_size - header.file_size;
    if (result.loaded_file_bytes >
            std::numeric_limits<std::uint64_t>::max() - header.file_size ||
        result.zero_filled_bytes >
            std::numeric_limits<std::uint64_t>::max() - zero_size) {
      result.error = ElfError::kLoadSizeOverflow;
      return result;
    }
    result.loaded_file_bytes += header.file_size;
    result.zero_filled_bytes += zero_size;
    ++result.loaded_segment_count;
  }

  for (const auto& header : result.metadata.program_headers) {
    if (header.type != kProgramTypeLoad || header.memory_size == 0) {
      continue;
    }

    if (!memory.Map(header.virtual_address, header.memory_size,
                    ProtectionFromFlags(header.flags))) {
      result.error = ElfError::kGuestMappingConflict;
      return result;
    }
  }

  for (const auto& header : result.metadata.program_headers) {
    if (header.type != kProgramTypeLoad || header.memory_size == 0) {
      continue;
    }

    if (header.file_size != 0) {
      const auto file_data = image.subspan(
          static_cast<std::size_t>(header.offset),
          static_cast<std::size_t>(header.file_size));
      if (!memory.Initialize(header.virtual_address, file_data)) {
        result.error = ElfError::kGuestRangeOutOfRange;
        return result;
      }
    }

    const auto zero_size = header.memory_size - header.file_size;
    if (zero_size != 0 &&
        !memory.InitializeFill(header.virtual_address + header.file_size,
                               zero_size, std::byte{0})) {
      result.error = ElfError::kGuestRangeOutOfRange;
      return result;
    }
  }

  return result;
}

std::string_view ElfErrorName(ElfError error) noexcept {
  switch (error) {
    case ElfError::kNone: return "none";
    case ElfError::kImageTooSmall: return "image-too-small";
    case ElfError::kInvalidMagic: return "invalid-magic";
    case ElfError::kUnsupportedClass: return "unsupported-class";
    case ElfError::kUnsupportedEndianness: return "unsupported-endianness";
    case ElfError::kUnsupportedIdentVersion:
      return "unsupported-ident-version";
    case ElfError::kUnsupportedAbi: return "unsupported-abi";
    case ElfError::kUnsupportedFileType: return "unsupported-file-type";
    case ElfError::kUnsupportedMachine: return "unsupported-machine";
    case ElfError::kUnsupportedVersion: return "unsupported-version";
    case ElfError::kInvalidHeaderSize: return "invalid-header-size";
    case ElfError::kInvalidProgramHeaderSize:
      return "invalid-program-header-size";
    case ElfError::kProgramHeaderTableOutOfRange:
      return "program-header-table-out-of-range";
    case ElfError::kMultipleDynamicSegments:
      return "multiple-dynamic-segments";
    case ElfError::kDynamicSegmentFileRangeOutOfRange:
      return "dynamic-segment-file-range-out-of-range";
    case ElfError::kInvalidDynamicSegmentSize:
      return "invalid-dynamic-segment-size";
    case ElfError::kUnterminatedDynamicTable:
      return "unterminated-dynamic-table";
    case ElfError::kSegmentFileSizeExceedsMemorySize:
      return "segment-file-size-exceeds-memory-size";
    case ElfError::kSegmentFileRangeOutOfRange:
      return "segment-file-range-out-of-range";
    case ElfError::kSegmentAddressRangeOverflow:
      return "segment-address-range-overflow";
    case ElfError::kInvalidSegmentAlignment:
      return "invalid-segment-alignment";
    case ElfError::kOverlappingLoadSegments:
      return "overlapping-load-segments";
    case ElfError::kGuestRangeOutOfRange: return "guest-range-out-of-range";
    case ElfError::kGuestMappingConflict: return "guest-mapping-conflict";
    case ElfError::kLoadSizeOverflow: return "load-size-overflow";
  }
  return "unknown";
}

}  // namespace kajps5::loader
