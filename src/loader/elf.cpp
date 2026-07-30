// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/elf.h"

#include <algorithm>
#include <bit>
#include <initializer_list>
#include <limits>
#include <utility>

namespace kajps5::loader {
namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kSelfHeaderSize = 32;
constexpr std::size_t kSelfSegmentSize = 32;
constexpr std::uint64_t kSelfSegmentBlocked = 0x800;
constexpr std::uint64_t kSelfSegmentEncrypted = 0x2;
constexpr std::uint64_t kSelfSegmentCompressed = 0x8;
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
constexpr std::uint32_t kProgramTypeSceDynlibData = 0x61000000;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::int64_t kDynamicTagNull = 0;
constexpr std::int64_t kDynamicTagNeeded = 1;
constexpr std::int64_t kDynamicTagPltRelocationSize = 2;
constexpr std::int64_t kDynamicTagHash = 4;
constexpr std::int64_t kDynamicTagStringTable = 5;
constexpr std::int64_t kDynamicTagSymbolTable = 6;
constexpr std::int64_t kDynamicTagRela = 7;
constexpr std::int64_t kDynamicTagRelaSize = 8;
constexpr std::int64_t kDynamicTagRelaEntrySize = 9;
constexpr std::int64_t kDynamicTagStringTableSize = 10;
constexpr std::int64_t kDynamicTagSymbolEntrySize = 11;
constexpr std::int64_t kDynamicTagSharedObjectName = 14;
constexpr std::int64_t kDynamicTagPltRelocationFormat = 20;
constexpr std::int64_t kDynamicTagJumpRelocation = 23;
constexpr std::int64_t kDynamicTagSceExportLibrary = 0x61000013;
constexpr std::int64_t kDynamicTagSceImportLibrary = 0x61000015;
constexpr std::int64_t kDynamicTagSceJumpRelocation = 0x61000029;
constexpr std::int64_t kDynamicTagScePltRelocationFormat = 0x6100002b;
constexpr std::int64_t kDynamicTagScePltRelocationSize = 0x6100002d;
constexpr std::int64_t kDynamicTagSceRela = 0x6100002f;
constexpr std::int64_t kDynamicTagSceRelaSize = 0x61000031;
constexpr std::int64_t kDynamicTagSceRelaEntrySize = 0x61000033;
constexpr std::int64_t kDynamicTagSceStringTable = 0x61000035;
constexpr std::int64_t kDynamicTagSceStringTableSize = 0x61000037;
constexpr std::int64_t kDynamicTagSceSymbolTable = 0x61000039;
constexpr std::int64_t kDynamicTagSceSymbolEntrySize = 0x6100003b;
constexpr std::int64_t kDynamicTagSceSymbolTableSize = 0x6100003f;
constexpr std::int64_t kDynamicTagSceModuleInfoV2 = 0x61000043;
constexpr std::int64_t kDynamicTagSceNeededModuleV2 = 0x61000045;
constexpr std::int64_t kDynamicTagSceExportLibraryV2 = 0x61000047;
constexpr std::int64_t kDynamicTagSceImportLibraryV2 = 0x61000049;
constexpr std::int64_t kDynamicTagSceModuleInfo = 0x6100000d;
constexpr std::int64_t kDynamicTagSceNeededModule = 0x6100000f;
constexpr std::uint64_t kDynamicFormatRela = 7;
constexpr std::size_t kRelaEntrySize = 24;
constexpr std::size_t kSymbolEntrySize = 24;
constexpr std::size_t kMinimumDecodedStringBudget = 64 * 1024;
constexpr std::size_t kMaximumDecodedStringBudget = 64 * 1024 * 1024;
constexpr std::size_t kDecodedStringBudgetScale = 4;

struct SelfSegment {
  std::uint64_t type = 0;
  std::uint64_t offset = 0;
  std::uint64_t compressed_size = 0;
  std::uint64_t decompressed_size = 0;

  [[nodiscard]] bool blocked() const noexcept {
    return (type & kSelfSegmentBlocked) != 0;
  }
  [[nodiscard]] bool encrypted() const noexcept {
    return (type & kSelfSegmentEncrypted) != 0;
  }
  [[nodiscard]] bool compressed() const noexcept {
    return (type & kSelfSegmentCompressed) != 0;
  }
  [[nodiscard]] std::uint16_t program_header_index() const noexcept {
    return static_cast<std::uint16_t>((type >> 20U) & 0xfffU);
  }
};

struct ExecutableLayout {
  ElfContainerKind container = ElfContainerKind::kElf;
  std::uint64_t elf_offset = 0;
  std::uint64_t self_file_size = 0;
  std::vector<SelfSegment> self_segments;
};

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

bool AddWithoutOverflow(std::uint64_t left, std::uint64_t right,
                        std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

bool IsSelfMagic(std::span<const std::byte> image) noexcept {
  if (image.size() < 4) {
    return false;
  }
  const bool ps4 = Read8(image, 0) == 0x4f && Read8(image, 1) == 0x15 &&
                   Read8(image, 2) == 0x3d && Read8(image, 3) == 0x1d;
  const bool ps5 = Read8(image, 0) == 0x54 && Read8(image, 1) == 0x14 &&
                   Read8(image, 2) == 0xf5 && Read8(image, 3) == 0xee;
  return ps4 || ps5;
}

bool HasKnownSelfLayout(std::span<const std::byte> image) noexcept {
  return IsSelfMagic(image) && Read8(image, 5) == 0x01 &&
         Read8(image, 6) == 0x01 && Read8(image, 7) == 0x12;
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

const ElfDynamicEntry* FindDynamicEntry(
    const ElfMetadata& metadata, std::int64_t tag) noexcept {
  const auto entry = std::find_if(
      metadata.dynamic_entries.begin(), metadata.dynamic_entries.end(),
      [tag](const ElfDynamicEntry& candidate) { return candidate.tag == tag; });
  return entry == metadata.dynamic_entries.end() ? nullptr : &*entry;
}

bool HasDynamicEntry(const ElfMetadata& metadata,
                     std::initializer_list<std::int64_t> tags) noexcept {
  return std::any_of(
      metadata.dynamic_entries.begin(), metadata.dynamic_entries.end(),
      [tags](const ElfDynamicEntry& entry) {
        return std::find(tags.begin(), tags.end(), entry.tag) != tags.end();
      });
}

struct DynamicEntrySelection {
  const ElfDynamicEntry* entry = nullptr;
  ElfDynamicDataSource source = ElfDynamicDataSource::kNone;
};

ElfDynamicDataSource SceDynamicDataSource(
    const ElfMetadata& metadata) noexcept {
  const bool has_dynlibdata = std::any_of(
      metadata.program_headers.begin(), metadata.program_headers.end(),
      [](const ElfProgramHeader& header) {
        return header.type == kProgramTypeSceDynlibData;
      });
  return has_dynlibdata ? ElfDynamicDataSource::kSceDynlibData
                        : ElfDynamicDataSource::kLoadSegment;
}

DynamicEntrySelection SelectDynamicEntry(const ElfMetadata& metadata,
                                         std::int64_t standard_tag,
                                         std::int64_t sce_tag) noexcept {
  if (const auto* entry = FindDynamicEntry(metadata, sce_tag);
      entry != nullptr) {
    return {entry, SceDynamicDataSource(metadata)};
  }
  if (const auto* entry = FindDynamicEntry(metadata, standard_tag);
      entry != nullptr) {
    return {entry, ElfDynamicDataSource::kLoadSegment};
  }
  return {};
}

DynamicEntrySelection SelectSceDynamicEntry(const ElfMetadata& metadata,
                                            std::int64_t tag) noexcept {
  const auto* entry = FindDynamicEntry(metadata, tag);
  return {entry, entry == nullptr ? ElfDynamicDataSource::kNone
                                  : SceDynamicDataSource(metadata)};
}

std::optional<std::size_t> ResolveFileOffset(const ElfMetadata& metadata,
                                             std::uint64_t virtual_address,
                                             std::uint64_t size) noexcept {
  const auto resolve_address =
      [&metadata, size](std::uint64_t address) -> std::optional<std::size_t> {
    for (const auto& header : metadata.program_headers) {
      if (header.type != kProgramTypeLoad || address < header.virtual_address) {
        continue;
      }
      const auto relative_address = address - header.virtual_address;
      if (relative_address <= header.file_size &&
          size <= header.file_size - relative_address) {
        return static_cast<std::size_t>(header.file_offset + relative_address);
      }
    }
    return std::nullopt;
  };

  if (const auto direct = resolve_address(virtual_address);
      direct.has_value()) {
    return direct;
  }

  auto image_base = std::numeric_limits<std::uint64_t>::max();
  for (const auto& header : metadata.program_headers) {
    if (header.type == kProgramTypeLoad) {
      image_base = std::min(image_base, header.virtual_address);
    }
  }
  std::uint64_t rebased_address = 0;
  if (image_base != std::numeric_limits<std::uint64_t>::max() &&
      AddWithoutOverflow(image_base, virtual_address, rebased_address)) {
    return resolve_address(rebased_address);
  }
  return std::nullopt;
}

const ElfProgramHeader* FindProgramHeader(const ElfMetadata& metadata,
                                          std::uint32_t type) noexcept {
  const auto header = std::find_if(metadata.program_headers.begin(),
                                   metadata.program_headers.end(),
                                   [type](const ElfProgramHeader& candidate) {
                                     return candidate.type == type;
                                   });
  return header == metadata.program_headers.end() ? nullptr : &*header;
}

std::optional<std::size_t> ResolveDynamicFileOffset(
    const ElfMetadata& metadata, DynamicEntrySelection selection,
    std::uint64_t size) noexcept {
  if (selection.entry == nullptr) {
    return std::nullopt;
  }
  if (selection.source == ElfDynamicDataSource::kLoadSegment) {
    return ResolveFileOffset(metadata, selection.entry->value, size);
  }
  if (selection.source != ElfDynamicDataSource::kSceDynlibData) {
    return std::nullopt;
  }
  const auto* header = FindProgramHeader(metadata, kProgramTypeSceDynlibData);
  if (header == nullptr ||
      !RangeWithin(header->file_size, selection.entry->value, size)) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(header->file_offset + selection.entry->value);
}

enum class DynamicStringReadStatus {
  kOk,
  kUnterminated,
  kBudgetExceeded,
};

std::size_t CalculateDecodedStringBudget(std::size_t image_size) noexcept {
  const auto scaled =
      image_size >= kMaximumDecodedStringBudget / kDecodedStringBudgetScale
          ? kMaximumDecodedStringBudget
          : image_size * kDecodedStringBudgetScale;
  return std::clamp(scaled, kMinimumDecodedStringBudget,
                    kMaximumDecodedStringBudget);
}

DynamicStringReadStatus ReadDynamicString(
    std::span<const std::byte> string_table, std::uint64_t offset,
    std::size_t& remaining_budget, std::string& value) {
  if (offset >= string_table.size()) {
    return DynamicStringReadStatus::kUnterminated;
  }

  value.clear();
  for (auto index = static_cast<std::size_t>(offset);
       index < string_table.size(); ++index) {
    const auto byte = std::to_integer<unsigned char>(string_table[index]);
    if (byte == 0) {
      if (value.size() >= remaining_budget) {
        return DynamicStringReadStatus::kBudgetExceeded;
      }
      remaining_budget -= value.size() + 1;
      return DynamicStringReadStatus::kOk;
    }
    if (remaining_budget == 0 || value.size() >= remaining_budget - 1) {
      return DynamicStringReadStatus::kBudgetExceeded;
    }
    value.push_back(static_cast<char>(byte));
  }
  return DynamicStringReadStatus::kUnterminated;
}

ElfError ParseDynamicStrings(std::span<const std::byte> image,
                             ElfMetadata& metadata,
                             std::size_t& decoded_string_budget) {
  const auto string_table = SelectDynamicEntry(metadata, kDynamicTagStringTable,
                                               kDynamicTagSceStringTable);
  const auto string_table_size = SelectDynamicEntry(
      metadata, kDynamicTagStringTableSize, kDynamicTagSceStringTableSize);
  const auto* shared_object_name =
      FindDynamicEntry(metadata, kDynamicTagSharedObjectName);

  const auto has_needed =
      FindDynamicEntry(metadata, kDynamicTagNeeded) != nullptr;
  const auto has_sce_identities = HasDynamicEntry(
      metadata, {kDynamicTagSceNeededModule, kDynamicTagSceNeededModuleV2,
                 kDynamicTagSceModuleInfo, kDynamicTagSceModuleInfoV2,
                 kDynamicTagSceImportLibrary, kDynamicTagSceImportLibraryV2,
                 kDynamicTagSceExportLibrary, kDynamicTagSceExportLibraryV2});
  if ((string_table.entry == nullptr) != (string_table_size.entry == nullptr) ||
      (string_table.entry != nullptr &&
       string_table.source != string_table_size.source) ||
      ((has_needed || shared_object_name != nullptr) &&
       string_table.entry == nullptr) ||
      (has_sce_identities && string_table.entry == nullptr)) {
    return ElfError::kIncompleteDynamicStringTable;
  }
  if (string_table.entry == nullptr) {
    return ElfError::kNone;
  }

  const auto file_offset = ResolveDynamicFileOffset(
      metadata, string_table, string_table_size.entry->value);
  if (!file_offset.has_value()) {
    return ElfError::kDynamicStringTableNotFileBacked;
  }
  const auto bytes = image.subspan(
      *file_offset, static_cast<std::size_t>(string_table_size.entry->value));
  metadata.dynamic_info.string_table_source = string_table.source;
  metadata.dynamic_info.string_table_file_offset = *file_offset;
  metadata.dynamic_info.string_table_size = string_table_size.entry->value;

  for (const auto& entry : metadata.dynamic_entries) {
    if (entry.tag != kDynamicTagNeeded) {
      continue;
    }
    if (entry.value >= bytes.size()) {
      return ElfError::kDynamicStringOffsetOutOfRange;
    }
    std::string value;
    const auto status =
        ReadDynamicString(bytes, entry.value, decoded_string_budget, value);
    if (status == DynamicStringReadStatus::kBudgetExceeded) {
      return ElfError::kDecodedStringBudgetExceeded;
    }
    if (status != DynamicStringReadStatus::kOk) {
      return ElfError::kUnterminatedDynamicString;
    }
    metadata.dynamic_info.needed_libraries.push_back(std::move(value));
  }

  if (shared_object_name != nullptr) {
    if (shared_object_name->value >= bytes.size()) {
      return ElfError::kDynamicStringOffsetOutOfRange;
    }
    std::string value;
    const auto status = ReadDynamicString(bytes, shared_object_name->value,
                                          decoded_string_budget, value);
    if (status == DynamicStringReadStatus::kBudgetExceeded) {
      return ElfError::kDecodedStringBudgetExceeded;
    }
    if (status != DynamicStringReadStatus::kOk) {
      return ElfError::kUnterminatedDynamicString;
    }
    metadata.dynamic_info.shared_object_name = std::move(value);
  }
  return ElfError::kNone;
}

ElfError ReadMetadataString(std::span<const std::byte> image,
                            const ElfMetadata& metadata, std::uint64_t offset,
                            std::size_t& decoded_string_budget,
                            std::string& value) {
  if (!metadata.dynamic_info.string_table_file_offset.has_value() ||
      !metadata.dynamic_info.string_table_size.has_value()) {
    return ElfError::kIncompleteDynamicStringTable;
  }
  const auto table_offset = *metadata.dynamic_info.string_table_file_offset;
  const auto table_size = *metadata.dynamic_info.string_table_size;
  if (!RangeWithin(image.size(), table_offset, table_size)) {
    return ElfError::kDynamicStringTableNotFileBacked;
  }
  const auto bytes = image.subspan(static_cast<std::size_t>(table_offset),
                                   static_cast<std::size_t>(table_size));
  if (offset >= bytes.size()) {
    return ElfError::kDynamicStringOffsetOutOfRange;
  }
  const auto status =
      ReadDynamicString(bytes, offset, decoded_string_budget, value);
  if (status == DynamicStringReadStatus::kBudgetExceeded) {
    return ElfError::kDecodedStringBudgetExceeded;
  }
  return status == DynamicStringReadStatus::kOk
             ? ElfError::kNone
             : ElfError::kUnterminatedDynamicString;
}

ElfError ParseSceIdentities(std::span<const std::byte> image,
                            ElfMetadata& metadata,
                            std::size_t& decoded_string_budget) {
  for (const auto& entry : metadata.dynamic_entries) {
    const auto is_import_module = entry.tag == kDynamicTagSceNeededModule ||
                                  entry.tag == kDynamicTagSceNeededModuleV2;
    const auto is_export_module = entry.tag == kDynamicTagSceModuleInfo ||
                                  entry.tag == kDynamicTagSceModuleInfoV2;
    const auto is_import_library = entry.tag == kDynamicTagSceImportLibrary ||
                                   entry.tag == kDynamicTagSceImportLibraryV2;
    const auto is_export_library = entry.tag == kDynamicTagSceExportLibrary ||
                                   entry.tag == kDynamicTagSceExportLibraryV2;
    if (!is_import_module && !is_export_module && !is_import_library &&
        !is_export_library) {
      continue;
    }

    std::string name;
    if (const auto error = ReadMetadataString(
            image, metadata, static_cast<std::uint32_t>(entry.value),
            decoded_string_budget, name);
        error != ElfError::kNone) {
      return error;
    }

    if (is_import_module || is_export_module) {
      ElfModuleIdentity identity;
      identity.id = static_cast<std::uint16_t>(entry.value >> 48U);
      identity.version_major = static_cast<std::uint8_t>(entry.value >> 40U);
      identity.version_minor = static_cast<std::uint8_t>(entry.value >> 32U);
      identity.name = std::move(name);
      auto& identities = is_import_module
                             ? metadata.dynamic_info.import_modules
                             : metadata.dynamic_info.export_modules;
      identities.push_back(std::move(identity));
      continue;
    }

    ElfLibraryIdentity identity;
    identity.id = static_cast<std::uint16_t>(entry.value >> 48U);
    identity.version = static_cast<std::uint16_t>(entry.value >> 32U);
    identity.name = std::move(name);
    auto& identities = is_import_library
                           ? metadata.dynamic_info.import_libraries
                           : metadata.dynamic_info.export_libraries;
    identities.push_back(std::move(identity));
  }
  return ElfError::kNone;
}

bool IsGuestRangeMapped(const ElfMetadata& metadata, std::uint64_t address,
                        std::uint64_t size) noexcept {
  for (const auto& header : metadata.program_headers) {
    if (header.type != kProgramTypeLoad || address < header.virtual_address) {
      continue;
    }
    const auto relative_address = address - header.virtual_address;
    if (relative_address <= header.memory_size &&
        size <= header.memory_size - relative_address) {
      return true;
    }
  }
  return false;
}

ElfError ParseRelaTable(std::span<const std::byte> image, ElfMetadata& metadata,
                        DynamicEntrySelection table, std::uint64_t size,
                        std::vector<ElfRelaEntry>& entries) {
  if (size % kRelaEntrySize != 0) {
    return ElfError::kInvalidRelaEntrySize;
  }
  const auto file_offset = ResolveDynamicFileOffset(metadata, table, size);
  if (!file_offset.has_value()) {
    return ElfError::kRelaTableNotFileBacked;
  }

  entries.reserve(static_cast<std::size_t>(size / kRelaEntrySize));
  for (std::uint64_t relative_offset = 0; relative_offset < size;
       relative_offset += kRelaEntrySize) {
    const auto entry_offset =
        *file_offset + static_cast<std::size_t>(relative_offset);
    ElfRelaEntry entry;
    entry.offset = Read64(image, entry_offset);
    entry.info = Read64(image, entry_offset + sizeof(std::uint64_t));
    entry.addend = std::bit_cast<std::int64_t>(
        Read64(image, entry_offset + 2 * sizeof(std::uint64_t)));
    if (entry.type() != 0 && !IsGuestRangeMapped(metadata, entry.offset, 1)) {
      return ElfError::kRelocationTargetOutOfRange;
    }
    entries.push_back(entry);
  }
  return ElfError::kNone;
}

ElfError ParseDynamicRelocations(std::span<const std::byte> image,
                                 ElfMetadata& metadata) {
  const auto has_sce_rela =
      HasDynamicEntry(metadata, {kDynamicTagSceRela, kDynamicTagSceRelaSize,
                                 kDynamicTagSceRelaEntrySize});
  DynamicEntrySelection rela;
  const ElfDynamicEntry* rela_size = nullptr;
  const ElfDynamicEntry* rela_entry_size = nullptr;
  if (has_sce_rela) {
    rela = SelectSceDynamicEntry(metadata, kDynamicTagSceRela);
    rela_size = FindDynamicEntry(metadata, kDynamicTagSceRelaSize);
    rela_entry_size = FindDynamicEntry(metadata, kDynamicTagSceRelaEntrySize);
    if (rela.entry == nullptr || rela_size == nullptr) {
      return ElfError::kIncompleteRelaMetadata;
    }
  } else {
    rela = {FindDynamicEntry(metadata, kDynamicTagRela),
            ElfDynamicDataSource::kLoadSegment};
    rela_size = FindDynamicEntry(metadata, kDynamicTagRelaSize);
    rela_entry_size = FindDynamicEntry(metadata, kDynamicTagRelaEntrySize);
    const auto fields = static_cast<unsigned>(rela.entry != nullptr) +
                        static_cast<unsigned>(rela_size != nullptr) +
                        static_cast<unsigned>(rela_entry_size != nullptr);
    if (fields != 0 && fields != 3) {
      return ElfError::kIncompleteRelaMetadata;
    }
  }
  if (rela.entry != nullptr) {
    if (rela_entry_size != nullptr &&
        rela_entry_size->value != kRelaEntrySize) {
      return ElfError::kInvalidRelaEntrySize;
    }
    if (const auto error =
            ParseRelaTable(image, metadata, rela, rela_size->value,
                           metadata.dynamic_info.relocations);
        error != ElfError::kNone) {
      return error;
    }
  }

  const auto has_sce_plt = HasDynamicEntry(
      metadata, {kDynamicTagSceJumpRelocation, kDynamicTagScePltRelocationSize,
                 kDynamicTagScePltRelocationFormat});
  DynamicEntrySelection jump_relocation;
  const ElfDynamicEntry* plt_size = nullptr;
  const ElfDynamicEntry* plt_format = nullptr;
  if (has_sce_plt) {
    jump_relocation =
        SelectSceDynamicEntry(metadata, kDynamicTagSceJumpRelocation);
    plt_size = FindDynamicEntry(metadata, kDynamicTagScePltRelocationSize);
    plt_format = FindDynamicEntry(metadata, kDynamicTagScePltRelocationFormat);
    if (jump_relocation.entry == nullptr || plt_size == nullptr) {
      return ElfError::kIncompleteRelaMetadata;
    }
  } else {
    jump_relocation = {FindDynamicEntry(metadata, kDynamicTagJumpRelocation),
                       ElfDynamicDataSource::kLoadSegment};
    plt_size = FindDynamicEntry(metadata, kDynamicTagPltRelocationSize);
    plt_format = FindDynamicEntry(metadata, kDynamicTagPltRelocationFormat);
    const auto fields =
        static_cast<unsigned>(jump_relocation.entry != nullptr) +
        static_cast<unsigned>(plt_size != nullptr) +
        static_cast<unsigned>(plt_format != nullptr);
    if (fields != 0 && fields != 3) {
      return ElfError::kIncompleteRelaMetadata;
    }
  }
  if (jump_relocation.entry != nullptr) {
    if (plt_format != nullptr && plt_format->value != kDynamicFormatRela) {
      return ElfError::kUnsupportedPltRelocationFormat;
    }
    if (const auto error =
            ParseRelaTable(image, metadata, jump_relocation, plt_size->value,
                           metadata.dynamic_info.plt_relocations);
        error != ElfError::kNone) {
      return error;
    }
  }
  return ElfError::kNone;
}

ElfError ParseSymbolRecords(std::span<const std::byte> image,
                            ElfMetadata& metadata,
                            std::size_t symbol_file_offset,
                            std::uint64_t symbol_count,
                            std::size_t& decoded_string_budget) {
  if (!metadata.dynamic_info.string_table_file_offset.has_value() ||
      !metadata.dynamic_info.string_table_size.has_value()) {
    return ElfError::kIncompleteDynamicSymbolMetadata;
  }
  const auto string_file_offset =
      *metadata.dynamic_info.string_table_file_offset;
  const auto string_table_size = *metadata.dynamic_info.string_table_size;
  if (!RangeWithin(image.size(), string_file_offset, string_table_size)) {
    return ElfError::kDynamicStringTableNotFileBacked;
  }
  const auto strings =
      image.subspan(static_cast<std::size_t>(string_file_offset),
                    static_cast<std::size_t>(string_table_size));

  metadata.dynamic_info.symbols.reserve(static_cast<std::size_t>(symbol_count));
  for (std::uint64_t index = 0; index < symbol_count; ++index) {
    const auto offset =
        symbol_file_offset + static_cast<std::size_t>(index) * kSymbolEntrySize;
    ElfSymbol symbol;
    symbol.name_offset = Read32(image, offset);
    symbol.info = Read8(image, offset + 4);
    symbol.other = Read8(image, offset + 5);
    symbol.section_index = Read16(image, offset + 6);
    symbol.value = Read64(image, offset + 8);
    symbol.size = Read64(image, offset + 16);
    if (symbol.name_offset >= strings.size()) {
      return ElfError::kSymbolNameOffsetOutOfRange;
    }
    std::string name;
    const auto status = ReadDynamicString(strings, symbol.name_offset,
                                          decoded_string_budget, name);
    if (status == DynamicStringReadStatus::kBudgetExceeded) {
      return ElfError::kDecodedStringBudgetExceeded;
    }
    if (status != DynamicStringReadStatus::kOk) {
      return ElfError::kUnterminatedSymbolName;
    }
    symbol.name = std::move(name);
    metadata.dynamic_info.symbols.push_back(std::move(symbol));
  }
  return ElfError::kNone;
}

ElfError ParseDynamicSymbols(std::span<const std::byte> image,
                             ElfMetadata& metadata,
                             std::size_t& decoded_string_budget) {
  const auto has_sce_symbols = HasDynamicEntry(
      metadata, {kDynamicTagSceSymbolTable, kDynamicTagSceSymbolTableSize,
                 kDynamicTagSceSymbolEntrySize});
  if (has_sce_symbols) {
    const auto symbol_table = SelectDynamicEntry(
        metadata, kDynamicTagSymbolTable, kDynamicTagSceSymbolTable);
    const auto* symbol_table_size =
        FindDynamicEntry(metadata, kDynamicTagSceSymbolTableSize);
    const auto* symbol_entry_size =
        FindDynamicEntry(metadata, kDynamicTagSceSymbolEntrySize);
    if (symbol_entry_size == nullptr) {
      symbol_entry_size =
          FindDynamicEntry(metadata, kDynamicTagSymbolEntrySize);
    }
    if (symbol_table.entry == nullptr ||
        metadata.dynamic_info.string_table_source ==
            ElfDynamicDataSource::kNone) {
      return ElfError::kIncompleteDynamicSymbolMetadata;
    }
    if (symbol_entry_size != nullptr &&
        symbol_entry_size->value != kSymbolEntrySize) {
      return ElfError::kInvalidSymbolEntrySize;
    }
    std::uint64_t symbol_table_bytes = 0;
    if (symbol_table_size != nullptr) {
      symbol_table_bytes = symbol_table_size->value;
    } else {
      std::uint32_t maximum_symbol = 0;
      for (const auto& relocation : metadata.dynamic_info.relocations) {
        maximum_symbol = std::max(maximum_symbol, relocation.symbol());
      }
      for (const auto& relocation : metadata.dynamic_info.plt_relocations) {
        maximum_symbol = std::max(maximum_symbol, relocation.symbol());
      }
      if (maximum_symbol == 0) {
        return ElfError::kNone;
      }
      symbol_table_bytes =
          (static_cast<std::uint64_t>(maximum_symbol) + 1) * kSymbolEntrySize;
    }
    if (symbol_table_bytes % kSymbolEntrySize != 0) {
      return ElfError::kInvalidSymbolTableSize;
    }
    const auto symbol_file_offset =
        ResolveDynamicFileOffset(metadata, symbol_table, symbol_table_bytes);
    if (!symbol_file_offset.has_value()) {
      return ElfError::kSymbolTableNotFileBacked;
    }
    return ParseSymbolRecords(image, metadata, *symbol_file_offset,
                              symbol_table_bytes / kSymbolEntrySize,
                              decoded_string_budget);
  }

  const auto* hash = FindDynamicEntry(metadata, kDynamicTagHash);
  if (hash == nullptr) {
    return ElfError::kNone;
  }
  const auto* symbol_table = FindDynamicEntry(metadata, kDynamicTagSymbolTable);
  const auto* symbol_entry_size =
      FindDynamicEntry(metadata, kDynamicTagSymbolEntrySize);
  if (symbol_table == nullptr || symbol_entry_size == nullptr ||
      !metadata.dynamic_info.string_table_file_offset.has_value() ||
      !metadata.dynamic_info.string_table_size.has_value()) {
    return ElfError::kIncompleteDynamicSymbolMetadata;
  }
  if (symbol_entry_size->value != kSymbolEntrySize) {
    return ElfError::kInvalidSymbolEntrySize;
  }

  const auto hash_header_offset = ResolveFileOffset(metadata, hash->value, 8);
  if (!hash_header_offset.has_value()) {
    return ElfError::kHashTableNotFileBacked;
  }
  const auto bucket_count = Read32(image, *hash_header_offset);
  const auto symbol_count = Read32(image, *hash_header_offset + 4);
  const auto hash_size =
      std::uint64_t{8} +
      std::uint64_t{4} * (static_cast<std::uint64_t>(bucket_count) +
                          static_cast<std::uint64_t>(symbol_count));
  if (!ResolveFileOffset(metadata, hash->value, hash_size).has_value()) {
    return ElfError::kHashTableNotFileBacked;
  }

  const auto symbol_table_size =
      static_cast<std::uint64_t>(symbol_count) * kSymbolEntrySize;
  const auto symbol_file_offset =
      ResolveFileOffset(metadata, symbol_table->value, symbol_table_size);
  if (!symbol_file_offset.has_value()) {
    return ElfError::kSymbolTableNotFileBacked;
  }
  return ParseSymbolRecords(image, metadata, *symbol_file_offset, symbol_count,
                            decoded_string_budget);
}

ElfError ParseExecutableLayout(std::span<const std::byte> image,
                               ExecutableLayout& layout) {
  if (!IsSelfMagic(image)) {
    return ElfError::kNone;
  }
  if (image.size() < kSelfHeaderSize) {
    return ElfError::kSelfHeaderOutOfRange;
  }
  if (!HasKnownSelfLayout(image)) {
    return ElfError::kUnsupportedSelfHeader;
  }

  layout.container = ElfContainerKind::kSelf;
  layout.self_file_size = Read64(image, 16);
  const auto segment_count = Read16(image, 24);
  const auto segment_table_size =
      static_cast<std::uint64_t>(segment_count) * kSelfSegmentSize;
  if (!AddWithoutOverflow(kSelfHeaderSize, segment_table_size,
                          layout.elf_offset) ||
      !RangeWithin(static_cast<std::uint64_t>(image.size()), layout.elf_offset,
                   kElfHeaderSize)) {
    return ElfError::kSelfEmbeddedElfOutOfRange;
  }

  layout.self_segments.reserve(segment_count);
  for (std::uint16_t index = 0; index < segment_count; ++index) {
    const auto offset =
        kSelfHeaderSize + static_cast<std::size_t>(index) * kSelfSegmentSize;
    layout.self_segments.push_back(
        {Read64(image, offset), Read64(image, offset + 8),
         Read64(image, offset + 16), Read64(image, offset + 24)});
  }
  return ElfError::kNone;
}

bool HeaderRangeContains(const ElfProgramHeader& parent,
                         const ElfProgramHeader& child) noexcept {
  if (child.offset < parent.offset) {
    return false;
  }
  return RangeWithin(parent.file_size, child.offset - parent.offset,
                     child.file_size);
}

ElfError ResolveSelfFileOffset(
    std::uint64_t image_size, const ExecutableLayout& layout,
    const std::vector<ElfProgramHeader>& program_headers,
    std::size_t target_index, std::uint64_t& file_offset) noexcept {
  const auto& target = program_headers[target_index];
  const bool has_exact_mapping =
      std::any_of(layout.self_segments.begin(), layout.self_segments.end(),
                  [target_index](const SelfSegment& segment) {
                    return segment.blocked() &&
                           segment.program_header_index() == target_index;
                  });

  std::size_t resolved_count = 0;
  bool saw_mapping = false;
  bool saw_encrypted = false;
  bool saw_compressed = false;
  for (const auto& segment : layout.self_segments) {
    if (!segment.blocked() ||
        segment.program_header_index() >= program_headers.size()) {
      continue;
    }
    const auto source_index = segment.program_header_index();
    if ((has_exact_mapping && source_index != target_index) ||
        (!has_exact_mapping &&
         !HeaderRangeContains(program_headers[source_index], target))) {
      continue;
    }

    saw_mapping = true;
    saw_encrypted = saw_encrypted || segment.encrypted();
    saw_compressed = saw_compressed || segment.compressed();
    const auto relative_offset =
        target.offset - program_headers[source_index].offset;
    if (!RangeWithin(segment.decompressed_size, relative_offset,
                     target.file_size)) {
      continue;
    }
    std::uint64_t candidate = 0;
    if (!AddWithoutOverflow(segment.offset, relative_offset, candidate) ||
        !RangeWithin(image_size, candidate, target.file_size)) {
      continue;
    }
    file_offset = candidate;
    ++resolved_count;
  }

  if (resolved_count > 1) {
    return ElfError::kMultipleSelfSegmentMappings;
  }
  if (resolved_count == 1) {
    return ElfError::kNone;
  }

  std::uint64_t elf_relative_offset = 0;
  if (AddWithoutOverflow(layout.elf_offset, target.offset,
                         elf_relative_offset) &&
      RangeWithin(image_size, elf_relative_offset, target.file_size)) {
    file_offset = elf_relative_offset;
    return ElfError::kNone;
  }
  if (RangeWithin(image_size, target.offset, target.file_size)) {
    file_offset = target.offset;
    return ElfError::kNone;
  }
  if (layout.self_file_size <= image_size &&
      image_size - layout.self_file_size == target.file_size) {
    file_offset = layout.self_file_size;
    return ElfError::kNone;
  }

  if (saw_encrypted) {
    return ElfError::kUnsupportedEncryptedSelfSegment;
  }
  if (saw_compressed) {
    return ElfError::kUnsupportedCompressedSelfSegment;
  }
  return saw_mapping ? ElfError::kSelfSegmentFileRangeOutOfRange
                     : ElfError::kSelfSegmentMappingNotFound;
}

}  // namespace

namespace {

ElfParseResult ParseElf64WithLayout(std::span<const std::byte> image,
                                    const ExecutableLayout& layout) {
  if (!RangeWithin(static_cast<std::uint64_t>(image.size()), layout.elf_offset,
                   kElfHeaderSize)) {
    return ParseFailure(ElfError::kImageTooSmall);
  }

  const auto elf_offset = static_cast<std::size_t>(layout.elf_offset);

  if (Read8(image, elf_offset) != 0x7f || Read8(image, elf_offset + 1) != 'E' ||
      Read8(image, elf_offset + 2) != 'L' ||
      Read8(image, elf_offset + 3) != 'F') {
    return ParseFailure(ElfError::kInvalidMagic);
  }
  if (Read8(image, elf_offset + 4) != kElfClass64) {
    return ParseFailure(ElfError::kUnsupportedClass);
  }
  if (Read8(image, elf_offset + 5) != kElfDataLittleEndian) {
    return ParseFailure(ElfError::kUnsupportedEndianness);
  }
  if (Read8(image, elf_offset + 6) != kElfVersionCurrent) {
    return ParseFailure(ElfError::kUnsupportedIdentVersion);
  }

  ElfMetadata metadata;
  metadata.container = layout.container;
  metadata.elf_file_offset = layout.elf_offset;
  metadata.self_segment_count =
      static_cast<std::uint16_t>(layout.self_segments.size());
  metadata.os_abi = Read8(image, elf_offset + 7);
  metadata.abi_version = Read8(image, elf_offset + 8);
  metadata.type = Read16(image, elf_offset + 16);
  metadata.machine = Read16(image, elf_offset + 18);
  metadata.version = Read32(image, elf_offset + 20);
  metadata.entry_point = Read64(image, elf_offset + 24);
  const auto program_header_offset = Read64(image, elf_offset + 32);
  const auto header_size = Read16(image, elf_offset + 52);
  const auto program_header_size = Read16(image, elf_offset + 54);
  const auto program_header_count = Read16(image, elf_offset + 56);

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
  std::uint64_t program_header_file_offset = 0;
  if (!AddWithoutOverflow(layout.elf_offset, program_header_offset,
                          program_header_file_offset) ||
      !RangeWithin(static_cast<std::uint64_t>(image.size()),
                   program_header_file_offset, table_size)) {
    return ParseFailure(ElfError::kProgramHeaderTableOutOfRange);
  }

  metadata.program_headers.reserve(program_header_count);
  for (std::uint16_t index = 0; index < program_header_count; ++index) {
    const auto entry_offset64 =
        program_header_file_offset +
        static_cast<std::uint64_t>(index) * program_header_size;
    const auto entry_offset = static_cast<std::size_t>(entry_offset64);

    ElfProgramHeader header;
    header.type = Read32(image, entry_offset);
    header.flags = Read32(image, entry_offset + 4);
    header.offset = Read64(image, entry_offset + 8);
    header.file_offset = header.offset;
    header.virtual_address = Read64(image, entry_offset + 16);
    header.physical_address = Read64(image, entry_offset + 24);
    header.file_size = Read64(image, entry_offset + 32);
    header.memory_size = Read64(image, entry_offset + 40);
    header.alignment = Read64(image, entry_offset + 48);

    if (header.type == kProgramTypeLoad) {
      if (header.file_size > header.memory_size) {
        return ParseFailure(ElfError::kSegmentFileSizeExceedsMemorySize);
      }
      if (header.memory_size >
          std::numeric_limits<std::uint64_t>::max() - header.virtual_address) {
        return ParseFailure(ElfError::kSegmentAddressRangeOverflow);
      }
      if (header.alignment > 1 && (!std::has_single_bit(header.alignment) ||
                                   header.virtual_address % header.alignment !=
                                       header.offset % header.alignment)) {
        return ParseFailure(ElfError::kInvalidSegmentAlignment);
      }
    }

    metadata.program_headers.push_back(header);
  }

  for (std::size_t index = 0; index < metadata.program_headers.size();
       ++index) {
    auto& header = metadata.program_headers[index];
    const bool needs_file_data =
        header.file_size != 0 && (header.type == kProgramTypeLoad ||
                                  header.type == kProgramTypeDynamic ||
                                  header.type == kProgramTypeSceDynlibData);
    if (needs_file_data && layout.container == ElfContainerKind::kSelf) {
      if (const auto error = ResolveSelfFileOffset(
              static_cast<std::uint64_t>(image.size()), layout,
              metadata.program_headers, index, header.file_offset);
          error != ElfError::kNone) {
        return ParseFailure(error);
      }
    }
    if (header.type == kProgramTypeLoad &&
        !RangeWithin(static_cast<std::uint64_t>(image.size()),
                     header.file_offset, header.file_size)) {
      return ParseFailure(ElfError::kSegmentFileRangeOutOfRange);
    }
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

  const ElfProgramHeader* sce_dynlibdata_header = nullptr;
  for (const auto& header : metadata.program_headers) {
    if (header.type != kProgramTypeSceDynlibData) {
      continue;
    }
    if (sce_dynlibdata_header != nullptr) {
      return ParseFailure(ElfError::kMultipleSceDynlibDataSegments);
    }
    if (!RangeWithin(static_cast<std::uint64_t>(image.size()),
                     header.file_offset, header.file_size)) {
      return ParseFailure(ElfError::kSceDynlibDataSegmentFileRangeOutOfRange);
    }
    sce_dynlibdata_header = &header;
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
                     dynamic_header->file_offset, dynamic_header->file_size)) {
      return ParseFailure(ElfError::kDynamicSegmentFileRangeOutOfRange);
    }

    metadata.dynamic_entries.reserve(static_cast<std::size_t>(
        dynamic_header->file_size / kDynamicEntrySize));
    bool terminated = false;
    for (std::uint64_t relative_offset = 0;
         relative_offset < dynamic_header->file_size;
         relative_offset += kDynamicEntrySize) {
      const auto entry_offset = static_cast<std::size_t>(
          dynamic_header->file_offset + relative_offset);
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
    auto decoded_string_budget = CalculateDecodedStringBudget(image.size());
    if (const auto error =
            ParseDynamicStrings(image, metadata, decoded_string_budget);
        error != ElfError::kNone) {
      return ParseFailure(error);
    }
    if (const auto error =
            ParseSceIdentities(image, metadata, decoded_string_budget);
        error != ElfError::kNone) {
      return ParseFailure(error);
    }
    if (const auto error = ParseDynamicRelocations(image, metadata);
        error != ElfError::kNone) {
      return ParseFailure(error);
    }
    if (const auto error =
            ParseDynamicSymbols(image, metadata, decoded_string_budget);
        error != ElfError::kNone) {
      return ParseFailure(error);
    }
  }

  ElfParseResult result;
  result.metadata = std::move(metadata);
  return result;
}

}  // namespace

ElfParseResult ParseElf64(std::span<const std::byte> image) {
  return ParseElf64WithLayout(image, ExecutableLayout{});
}

ElfParseResult ParseExecutable64(std::span<const std::byte> image) {
  ExecutableLayout layout;
  if (const auto error = ParseExecutableLayout(image, layout);
      error != ElfError::kNone) {
    return ParseFailure(error);
  }
  return ParseElf64WithLayout(image, layout);
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

namespace {

ElfLoadResult LoadParsedElf64(std::span<const std::byte> image,
                              ElfParseResult parsed,
                              memory::GuestMemory& memory) {
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
          static_cast<std::size_t>(header.file_offset),
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

}  // namespace

ElfLoadResult LoadElf64(std::span<const std::byte> image,
                        memory::GuestMemory& memory) {
  return LoadParsedElf64(image, ParseElf64(image), memory);
}

ElfLoadResult LoadExecutable64(std::span<const std::byte> image,
                               memory::GuestMemory& memory) {
  return LoadParsedElf64(image, ParseExecutable64(image), memory);
}

std::string_view ElfErrorName(ElfError error) noexcept {
  switch (error) {
    case ElfError::kNone: return "none";
    case ElfError::kImageTooSmall: return "image-too-small";
    case ElfError::kInvalidMagic: return "invalid-magic";
    case ElfError::kSelfHeaderOutOfRange: return "self-header-out-of-range";
    case ElfError::kUnsupportedSelfHeader:
      return "unsupported-self-header";
    case ElfError::kSelfEmbeddedElfOutOfRange:
      return "self-embedded-elf-out-of-range";
    case ElfError::kSelfSegmentMappingNotFound:
      return "self-segment-mapping-not-found";
    case ElfError::kMultipleSelfSegmentMappings:
      return "multiple-self-segment-mappings";
    case ElfError::kSelfSegmentFileRangeOutOfRange:
      return "self-segment-file-range-out-of-range";
    case ElfError::kUnsupportedEncryptedSelfSegment:
      return "unsupported-encrypted-self-segment";
    case ElfError::kUnsupportedCompressedSelfSegment:
      return "unsupported-compressed-self-segment";
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
    case ElfError::kMultipleSceDynlibDataSegments:
      return "multiple-sce-dynlibdata-segments";
    case ElfError::kDynamicSegmentFileRangeOutOfRange:
      return "dynamic-segment-file-range-out-of-range";
    case ElfError::kSceDynlibDataSegmentFileRangeOutOfRange:
      return "sce-dynlibdata-segment-file-range-out-of-range";
    case ElfError::kInvalidDynamicSegmentSize:
      return "invalid-dynamic-segment-size";
    case ElfError::kUnterminatedDynamicTable:
      return "unterminated-dynamic-table";
    case ElfError::kIncompleteDynamicStringTable:
      return "incomplete-dynamic-string-table";
    case ElfError::kDynamicStringTableNotFileBacked:
      return "dynamic-string-table-not-file-backed";
    case ElfError::kDynamicStringOffsetOutOfRange:
      return "dynamic-string-offset-out-of-range";
    case ElfError::kUnterminatedDynamicString:
      return "unterminated-dynamic-string";
    case ElfError::kDecodedStringBudgetExceeded:
      return "decoded-string-budget-exceeded";
    case ElfError::kIncompleteRelaMetadata:
      return "incomplete-rela-metadata";
    case ElfError::kInvalidRelaEntrySize:
      return "invalid-rela-entry-size";
    case ElfError::kRelaTableNotFileBacked:
      return "rela-table-not-file-backed";
    case ElfError::kRelocationTargetOutOfRange:
      return "relocation-target-out-of-range";
    case ElfError::kUnsupportedPltRelocationFormat:
      return "unsupported-plt-relocation-format";
    case ElfError::kIncompleteDynamicSymbolMetadata:
      return "incomplete-dynamic-symbol-metadata";
    case ElfError::kInvalidSymbolEntrySize:
      return "invalid-symbol-entry-size";
    case ElfError::kInvalidSymbolTableSize: return "invalid-symbol-table-size";
    case ElfError::kHashTableNotFileBacked:
      return "hash-table-not-file-backed";
    case ElfError::kSymbolTableNotFileBacked:
      return "symbol-table-not-file-backed";
    case ElfError::kSymbolNameOffsetOutOfRange:
      return "symbol-name-offset-out-of-range";
    case ElfError::kUnterminatedSymbolName:
      return "unterminated-symbol-name";
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
