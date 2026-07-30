// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"
#include "loader/relocator.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kDynamicOffset = 0x180;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::size_t kDynamicEntryCount = 16;
constexpr std::size_t kSceDataOffset = 0x300;
constexpr std::size_t kSceDataSize = 0x120;
constexpr std::size_t kLoadOffset = 0x500;
constexpr std::uint64_t kLoadAddress = 0x5000;
constexpr std::size_t kLoadSize = 0x100;
constexpr std::size_t kStringTableOffset = 0x20;
constexpr std::size_t kStringTableSize = 0x80;
constexpr std::size_t kSymbolTableOffset = 0xa0;
constexpr std::size_t kRelaOffset = 0xd0;
constexpr std::size_t kJumpRelaOffset = 0xe8;

constexpr std::int64_t kTagSceNeededModule = 0x6100000f;
constexpr std::int64_t kTagSceImportLibrary = 0x61000015;
constexpr std::int64_t kTagSceJumpRela = 0x61000029;
constexpr std::int64_t kTagSceJumpRelaSize = 0x6100002d;
constexpr std::int64_t kTagSceRela = 0x6100002f;
constexpr std::int64_t kTagSceRelaSize = 0x61000031;
constexpr std::int64_t kTagSceStringTable = 0x61000035;
constexpr std::int64_t kTagSceStringTableSize = 0x61000037;
constexpr std::int64_t kTagSceSymbolTable = 0x61000039;
constexpr std::int64_t kTagSceSymbolTableSize = 0x6100003f;
constexpr std::int64_t kTagSceModuleInfoV2 = 0x61000043;
constexpr std::int64_t kTagSceExportLibraryV2 = 0x61000047;

constexpr std::size_t kSceStringTableEntry = 2;
constexpr std::size_t kSceStringTableSizeEntry = 3;
constexpr std::size_t kSceSymbolTableSizeEntry = 6;
constexpr std::size_t kSceRelaSizeEntry = 8;
constexpr std::size_t kSceNeededModuleEntry = 11;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "elf_sce_dynamic_test: " << message << '\n';
    ++failures;
  }
}

void Write16(std::vector<std::byte>& image, std::size_t offset,
             std::uint16_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::vector<std::byte>& image, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::vector<std::byte>& image, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void WriteProgramHeader(std::vector<std::byte>& image, std::size_t index,
                        std::uint32_t type, std::uint32_t flags,
                        std::uint64_t offset, std::uint64_t address,
                        std::uint64_t file_size, std::uint64_t memory_size,
                        std::uint64_t alignment) {
  const auto header = kProgramHeaderOffset + index * kProgramHeaderSize;
  Write32(image, header, type);
  Write32(image, header + 4, flags);
  Write64(image, header + 8, offset);
  Write64(image, header + 16, address);
  Write64(image, header + 24, address);
  Write64(image, header + 32, file_size);
  Write64(image, header + 40, memory_size);
  Write64(image, header + 48, alignment);
}

void WriteDynamic(std::vector<std::byte>& image, std::size_t index,
                  std::int64_t tag, std::uint64_t value) {
  const auto offset = kDynamicOffset + index * kDynamicEntrySize;
  Write64(image, offset, static_cast<std::uint64_t>(tag));
  Write64(image, offset + sizeof(std::uint64_t), value);
}

void WriteString(std::vector<std::byte>& image, std::size_t relative_offset,
                 std::string_view value) {
  const auto offset = kSceDataOffset + kStringTableOffset + relative_offset;
  for (std::size_t index = 0; index < value.size(); ++index) {
    image[offset + index] = static_cast<std::byte>(value[index]);
  }
  image[offset + value.size()] = std::byte{0};
}

std::uint64_t PackModule(std::uint16_t id, std::uint8_t major,
                         std::uint8_t minor, std::uint32_t name_offset) {
  return (static_cast<std::uint64_t>(id) << 48U) |
         (static_cast<std::uint64_t>(major) << 40U) |
         (static_cast<std::uint64_t>(minor) << 32U) | name_offset;
}

std::uint64_t PackLibrary(std::uint16_t id, std::uint16_t version,
                          std::uint32_t name_offset) {
  return (static_cast<std::uint64_t>(id) << 48U) |
         (static_cast<std::uint64_t>(version) << 32U) | name_offset;
}

std::vector<std::byte> MakeSceDynamicElf() {
  std::vector<std::byte> image(kLoadOffset + kLoadSize);
  image[0] = std::byte{0x7f};
  image[1] = std::byte{'E'};
  image[2] = std::byte{'L'};
  image[3] = std::byte{'F'};
  image[4] = std::byte{2};
  image[5] = std::byte{1};
  image[6] = std::byte{1};
  image[7] = std::byte{9};
  image[8] = std::byte{2};
  Write16(image, 16, 0xfe10);
  Write16(image, 18, 62);
  Write32(image, 20, 1);
  Write64(image, 24, kLoadAddress);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, kProgramHeaderSize);
  Write16(image, 56, 3);

  WriteProgramHeader(image, 0, 2, 4, kDynamicOffset, 0,
                     kDynamicEntryCount * kDynamicEntrySize,
                     kDynamicEntryCount * kDynamicEntrySize, 8);
  WriteProgramHeader(image, 1, 0x61000000, 4, kSceDataOffset, 0, kSceDataSize,
                     kSceDataSize, 16);
  WriteProgramHeader(image, 2, 1, 6, kLoadOffset, kLoadAddress, kLoadSize,
                     kLoadSize, 0x100);

  // Invalid standard values prove that the SCE table takes precedence.
  WriteDynamic(image, 0, 5, 0xdead);
  WriteDynamic(image, 1, 10, 1);
  WriteDynamic(image, kSceStringTableEntry, kTagSceStringTable,
               kStringTableOffset);
  WriteDynamic(image, kSceStringTableSizeEntry, kTagSceStringTableSize,
               kStringTableSize);
  WriteDynamic(image, 4, 1, 1);
  WriteDynamic(image, 5, kTagSceSymbolTable, kSymbolTableOffset);
  WriteDynamic(image, kSceSymbolTableSizeEntry, kTagSceSymbolTableSize, 48);
  WriteDynamic(image, 7, kTagSceRela, kRelaOffset);
  WriteDynamic(image, kSceRelaSizeEntry, kTagSceRelaSize, 24);
  WriteDynamic(image, 9, kTagSceJumpRela, kJumpRelaOffset);
  WriteDynamic(image, 10, kTagSceJumpRelaSize, 24);
  WriteDynamic(image, kSceNeededModuleEntry, kTagSceNeededModule,
               PackModule(0x1234, 2, 3, 15));
  WriteDynamic(image, 12, kTagSceModuleInfoV2, PackModule(0x5678, 4, 5, 28));
  WriteDynamic(image, 13, kTagSceImportLibrary,
               PackLibrary(0x1111, 0x0203, 41));
  WriteDynamic(image, 14, kTagSceExportLibraryV2,
               PackLibrary(0x2222, 0x0405, 55));
  WriteDynamic(image, 15, 0, 0);

  WriteString(image, 1, "libkernel.prx");
  WriteString(image, 15, "importModule");
  WriteString(image, 28, "exportModule");
  WriteString(image, 41, "importLibrary");
  WriteString(image, 55, "exportLibrary");
  WriteString(image, 69, "open#BER#BI0");

  const auto symbol = kSceDataOffset + kSymbolTableOffset + 24;
  Write32(image, symbol, 69);
  image[symbol + 4] = std::byte{0x12};

  const auto rela = kSceDataOffset + kRelaOffset;
  Write64(image, rela, kLoadAddress + 0x10);
  Write64(image, rela + 8, 8);
  Write64(image, rela + 16, 0x20);

  const auto jump_rela = kSceDataOffset + kJumpRelaOffset;
  Write64(image, jump_rela, kLoadAddress + 0x18);
  Write64(image, jump_rela + 8, (std::uint64_t{1} << 32U) | 7U);
  return image;
}

void CheckError(std::vector<std::byte> image, kajps5::loader::ElfError expected,
                const char* message) {
  const auto parsed = kajps5::loader::ParseElf64(image);
  Check(parsed.error == expected, message);
}

}  // namespace

int main() {
  using kajps5::loader::ElfDynamicDataSource;
  using kajps5::loader::ElfError;
  using kajps5::loader::LoadElf64;
  using kajps5::loader::ParseElf64;
  using kajps5::memory::GuestMemory;

  const auto image = MakeSceDynamicElf();
  const auto parsed = ParseElf64(image);
  Check(static_cast<bool>(parsed), "valid SCE dynamic metadata was rejected");
  const auto& info = parsed.metadata.dynamic_info;
  Check(info.string_table_source == ElfDynamicDataSource::kSceDynlibData,
        "the SCE string table did not override the standard table");
  Check(info.string_table_file_offset == kSceDataOffset + kStringTableOffset,
        "the SCE string-table file offset is incorrect");
  Check(info.needed_libraries.size() == 1 &&
            info.needed_libraries[0] == "libkernel.prx",
        "the needed library did not use the SCE string table");
  Check(info.import_modules.size() == 1 &&
            info.import_modules[0].id == 0x1234 &&
            info.import_modules[0].version_major == 2 &&
            info.import_modules[0].version_minor == 3 &&
            info.import_modules[0].name == "importModule",
        "the imported module identity is incorrect");
  Check(info.export_modules.size() == 1 &&
            info.export_modules[0].id == 0x5678 &&
            info.export_modules[0].version_major == 4 &&
            info.export_modules[0].version_minor == 5 &&
            info.export_modules[0].name == "exportModule",
        "the exported module identity is incorrect");
  Check(info.import_libraries.size() == 1 &&
            info.import_libraries[0].id == 0x1111 &&
            info.import_libraries[0].version == 0x0203 &&
            info.import_libraries[0].name == "importLibrary",
        "the imported library identity is incorrect");
  Check(info.export_libraries.size() == 1 &&
            info.export_libraries[0].id == 0x2222 &&
            info.export_libraries[0].version == 0x0405 &&
            info.export_libraries[0].name == "exportLibrary",
        "the exported library identity is incorrect");
  Check(info.symbols.size() == 2 && info.symbols[1].name == "open#BER#BI0",
        "the size-based SCE symbol table is incorrect");
  Check(info.relocations.size() == 1 && info.relocations[0].type() == 8,
        "the SCE relocation table is incorrect");
  Check(info.plt_relocations.size() == 1 && info.plt_relocations[0].type() == 7,
        "the SCE PLT table is incorrect");

  const auto trace = kajps5::loader::FormatElfTrace(parsed.metadata);
  Check(trace.find("elf.dynamic_string_source=sce-dynlibdata\n") !=
                std::string::npos &&
            trace.find("elf.import_modules=1\n") != std::string::npos &&
            trace.find("elf.export_modules=1\n") != std::string::npos &&
            trace.find("elf.import_libraries=1\n") != std::string::npos &&
            trace.find("elf.export_libraries=1\n") != std::string::npos,
        "the SCE metadata summary is missing from the stable trace");

  GuestMemory guest_memory(
      kLoadAddress, kLoadSize,
      kajps5::memory::GuestMemoryProtection::kNone);
  const auto loaded = LoadElf64(image, guest_memory);
  if (!loaded) {
    std::cerr << "elf_sce_dynamic_test: load error: "
              << kajps5::loader::ElfErrorName(loaded.error) << '\n';
  }
  Check(static_cast<bool>(loaded), "valid SCE ELF did not load");
  kajps5::hle::ImportRegistry registry;
  Check(registry.Register("importLibrary", "open", 0x8877665544332211) ==
            kajps5::hle::ImportRegistryStatus::kOk,
        "scoped SCE import registration failed");
  const auto linked =
      kajps5::loader::ApplyRelocations(loaded.metadata, guest_memory, registry);
  Check(linked && linked.applied_count == 2 &&
            linked.resolved_import_count == 1 &&
            linked.unresolved_import_count == 0,
        "parsed SCE import metadata did not reach relocation linking");
  std::array<std::byte, 8> relative_value{};
  std::array<std::byte, 8> import_value{};
  const std::array expected_relative = {
      std::byte{0x20}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{0},    std::byte{0}, std::byte{0}, std::byte{0}};
  const std::array expected_import = {
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
      std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88}};
  Check(guest_memory.Read(kLoadAddress + 0x10, relative_value) &&
            relative_value == expected_relative,
        "parsed SCE relative relocation wrote the wrong value");
  Check(guest_memory.Read(kLoadAddress + 0x18, import_value) &&
            import_value == expected_import,
        "parsed SCE import relocation wrote the wrong value");

  auto missing_data = image;
  Write32(missing_data, kProgramHeaderOffset + kProgramHeaderSize, 0);
  CheckError(std::move(missing_data), ElfError::kMissingSceDynlibDataSegment,
             "a missing SCE data segment returned the wrong error");

  auto multiple_data = image;
  Write16(multiple_data, 56, 4);
  WriteProgramHeader(multiple_data, 3, 0x61000000, 4, kSceDataOffset, 0,
                     kSceDataSize, kSceDataSize, 16);
  CheckError(std::move(multiple_data), ElfError::kMultipleSceDynlibDataSegments,
             "multiple SCE data segments returned the wrong error");

  auto truncated_data = image;
  Write64(truncated_data, kProgramHeaderOffset + kProgramHeaderSize + 8,
          truncated_data.size() - 8);
  CheckError(std::move(truncated_data),
             ElfError::kSceDynlibDataSegmentFileRangeOutOfRange,
             "a truncated SCE data segment returned the wrong error");

  auto incomplete_strings = image;
  WriteDynamic(incomplete_strings, kSceStringTableSizeEntry, 0x70000000,
               kStringTableSize);
  CheckError(std::move(incomplete_strings),
             ElfError::kIncompleteDynamicStringTable,
             "an incomplete SCE string table returned the wrong error");

  auto invalid_strings = image;
  WriteDynamic(invalid_strings, kSceStringTableEntry, kTagSceStringTable,
               kSceDataSize - 8);
  CheckError(std::move(invalid_strings),
             ElfError::kDynamicStringTableNotFileBacked,
             "an out-of-range SCE string table returned the wrong error");

  auto invalid_module_name = image;
  WriteDynamic(invalid_module_name, kSceNeededModuleEntry, kTagSceNeededModule,
               PackModule(0x1234, 2, 3, kStringTableSize));
  CheckError(std::move(invalid_module_name),
             ElfError::kDynamicStringOffsetOutOfRange,
             "an invalid module-name offset returned the wrong error");

  auto invalid_symbol_size = image;
  WriteDynamic(invalid_symbol_size, kSceSymbolTableSizeEntry,
               kTagSceSymbolTableSize, 25);
  CheckError(std::move(invalid_symbol_size), ElfError::kInvalidSymbolTableSize,
             "an invalid SCE symbol-table size returned the wrong error");

  auto invalid_rela_size = image;
  WriteDynamic(invalid_rela_size, kSceRelaSizeEntry, kTagSceRelaSize, 23);
  CheckError(std::move(invalid_rela_size), ElfError::kInvalidRelaEntrySize,
             "an invalid SCE relocation size returned the wrong error");

  return failures == 0 ? 0 : 1;
}
