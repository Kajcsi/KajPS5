// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "core/memory/guest_memory.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kLoadHeaderOffset = kProgramHeaderOffset;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "elf_loader_test: " << message << '\n';
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

std::vector<std::byte> MakePublicTestElf() {
  std::vector<std::byte> image(0x104);
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
  Write64(image, 24, 0x1002);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, 56);
  Write16(image, 56, 2);

  Write32(image, kLoadHeaderOffset, 1);
  Write32(image, kLoadHeaderOffset + 4, 5);
  Write64(image, kLoadHeaderOffset + 8, 0x100);
  Write64(image, kLoadHeaderOffset + 16, 0x1000);
  Write64(image, kLoadHeaderOffset + 24, 0x1000);
  Write64(image, kLoadHeaderOffset + 32, 4);
  Write64(image, kLoadHeaderOffset + 40, 8);
  Write64(image, kLoadHeaderOffset + 48, 0x100);

  const auto second_header = kLoadHeaderOffset + 56;
  Write32(image, second_header, 7);
  Write32(image, second_header + 4, 4);
  Write64(image, second_header + 48, 8);

  image[0x100] = std::byte{0xde};
  image[0x101] = std::byte{0xad};
  image[0x102] = std::byte{0xbe};
  image[0x103] = std::byte{0xef};
  return image;
}

void CheckError(std::vector<std::byte> image, kajps5::loader::ElfError expected,
                const char* message) {
  const auto result = kajps5::loader::ParseElf64(image);
  Check(result.error == expected, message);
}

}  // namespace

int main() {
  using kajps5::loader::ElfError;
  using kajps5::loader::LoadElf64;
  using kajps5::loader::ParseElf64;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  const auto image = MakePublicTestElf();
  const auto parsed = ParseElf64(image);
  Check(static_cast<bool>(parsed), "valid public ELF fixture was rejected");
  const auto executable_parsed =
      kajps5::loader::ParseExecutable64(image);
  Check(static_cast<bool>(executable_parsed) &&
            executable_parsed.metadata.container ==
                kajps5::loader::ElfContainerKind::kElf &&
            executable_parsed.metadata.elf_file_offset == 0,
        "executable parser changed bare ELF behavior");
  Check(parsed.metadata.os_abi == 9, "OS ABI metadata is incorrect");
  Check(parsed.metadata.abi_version == 2, "ABI version metadata is incorrect");
  Check(parsed.metadata.type == 0xfe10, "file type metadata is incorrect");
  Check(parsed.metadata.machine == 62, "machine metadata is incorrect");
  Check(parsed.metadata.entry_point == 0x1002,
        "entry point metadata is incorrect");
  Check(parsed.metadata.program_headers.size() == 2,
        "program header count is incorrect");
  Check(parsed.metadata.program_headers[0].file_size == 4,
        "load segment file size is incorrect");
  Check(parsed.metadata.program_headers[0].memory_size == 8,
        "load segment memory size is incorrect");
  const auto load_range =
      kajps5::loader::CalculateElfLoadRange(parsed.metadata);
  Check(static_cast<bool>(load_range), "load range calculation failed");
  Check(load_range.base_address == 0x1000, "load range base is incorrect");
  Check(load_range.size == 8, "load range size is incorrect");
  Check(load_range.load_segment_count == 1,
        "load range segment count is incorrect");

  const std::string expected_trace =
      "elf.container=elf\n"
      "elf.container_offset=0x0000000000000000\n"
      "elf.self_segments=0\n"
      "elf.class=ELF64\n"
      "elf.endian=little\n"
      "elf.os_abi=freebsd\n"
      "elf.abi_version=2\n"
      "elf.type=0xfe10\n"
      "elf.machine=62\n"
      "elf.entry=0x0000000000001002\n"
      "elf.program_headers=2\n"
      "elf.load_segments=1\n"
      "elf.dynamic_entries=0\n"
      "elf.dynamic_string_source=none\n"
      "elf.dynamic_string_table_size=0\n"
      "elf.needed_libraries=0\n"
      "elf.has_soname=0\n"
      "elf.relocations=0\n"
      "elf.plt_relocations=0\n"
      "elf.symbols=0\n"
      "elf.undefined_symbols=0\n"
      "elf.import_modules=0\n"
      "elf.export_modules=0\n"
      "elf.import_libraries=0\n"
      "elf.export_libraries=0\n"
      "elf.load[0].flags=r-x offset=0x0000000000000100 "
      "file_offset=0x0000000000000100 "
      "virtual_address=0x0000000000001000 file_size=0x0000000000000004 "
      "memory_size=0x0000000000000008 alignment=0x0000000000000100\n";
  Check(kajps5::loader::FormatElfTrace(parsed.metadata) == expected_trace,
        "stable ELF trace changed");

  auto generic_image = image;
  generic_image[7] = std::byte{0};
  generic_image[8] = std::byte{0};
  Write16(generic_image, 16, 2);
  Check(static_cast<bool>(ParseElf64(generic_image)),
        "generic System V ELF64 fixture was rejected");

  GuestMemory memory(0x1000, 0x100, GuestMemoryProtection::kNone);
  const auto loaded = LoadElf64(image, memory);
  Check(static_cast<bool>(loaded), "valid ELF load failed");
  Check(loaded.loaded_segment_count == 1, "loaded segment count is incorrect");
  Check(loaded.loaded_file_bytes == 4, "loaded file byte count is incorrect");
  Check(loaded.zero_filled_bytes == 4, "zero-filled byte count is incorrect");

  std::array<std::byte, 8> loaded_bytes{};
  Check(memory.Read(0x1000, loaded_bytes), "loaded memory read failed");
  const std::array expected = {
      std::byte{0xde}, std::byte{0xad}, std::byte{0xbe},
      std::byte{0xef}, std::byte{0},    std::byte{0},
      std::byte{0},    std::byte{0}};
  Check(loaded_bytes == expected, "segment copy or zero fill is incorrect");
  Check(memory.regions().size() == 1, "load mapping count is incorrect");
  Check(memory.regions()[0].protection ==
            (GuestMemoryProtection::kRead |
             GuestMemoryProtection::kExecute),
        "ELF segment permissions were not preserved");
  Check(memory.CanExecute(0x1000, 8), "executable segment is not executable");
  const std::array denied_write = {std::byte{0xff}};
  Check(!memory.Write(0x1000, denied_write),
        "read-execute segment accepted a guest write");
  std::array<std::byte, 1> gap_byte{};
  Check(!memory.Read(0x1008, gap_byte), "unmapped load gap was readable");

  constexpr std::uint64_t kLoadBias = 0x10000000;
  GuestMemory biased_memory(kLoadBias + 0x1000, 0x100,
                            GuestMemoryProtection::kNone);
  const auto biased_loaded = LoadElf64(image, biased_memory, kLoadBias);
  std::array<std::byte, 8> biased_bytes{};
  Check(biased_loaded &&
            biased_memory.Read(kLoadBias + 0x1000, biased_bytes) &&
            biased_bytes == expected &&
            biased_memory.CanExecute(kLoadBias + 0x1000, 8),
        "load bias did not move the complete executable segment");
  Check(!biased_memory.IsMapped(0x1000, 1),
        "biased load also mapped the raw ELF address");

  auto multi_segment_image = image;
  const auto second_header = kLoadHeaderOffset + 56;
  Write32(multi_segment_image, second_header, 1);
  Write32(multi_segment_image, second_header + 4, 2);
  Write64(multi_segment_image, second_header + 8, 0);
  Write64(multi_segment_image, second_header + 16, 0x1100);
  Write64(multi_segment_image, second_header + 32, 0);
  Write64(multi_segment_image, second_header + 40, 4);
  Write64(multi_segment_image, second_header + 48, 1);
  GuestMemory multi_memory(0x1000, 0x200, GuestMemoryProtection::kNone);
  const auto multi_loaded = LoadElf64(multi_segment_image, multi_memory);
  Check(static_cast<bool>(multi_loaded), "multi-segment ELF load failed");
  Check(multi_memory.regions().size() == 2,
        "multi-segment mapping count is incorrect");
  Check(multi_memory.regions()[1].protection ==
            GuestMemoryProtection::kWrite,
        "write-only ELF permissions were not preserved");
  Check(multi_memory.Write(0x1100, denied_write),
        "write-only ELF segment rejected a write");
  Check(!multi_memory.Read(0x1100, gap_byte),
        "write-only ELF segment was readable");

  auto bad_magic = image;
  bad_magic[1] = std::byte{'X'};
  CheckError(std::move(bad_magic), ElfError::kInvalidMagic,
             "bad magic did not return the expected error");

  auto bad_endianness = image;
  bad_endianness[5] = std::byte{2};
  CheckError(std::move(bad_endianness), ElfError::kUnsupportedEndianness,
             "big-endian image did not return the expected error");

  auto truncated_table = image;
  truncated_table.resize(100);
  CheckError(std::move(truncated_table),
             ElfError::kProgramHeaderTableOutOfRange,
             "truncated program header table returned the wrong error");

  auto oversized_file_part = image;
  Write64(oversized_file_part, kLoadHeaderOffset + 32, 9);
  CheckError(std::move(oversized_file_part),
             ElfError::kSegmentFileSizeExceedsMemorySize,
             "oversized file part returned the wrong error");

  auto truncated_segment = image;
  Write64(truncated_segment, kLoadHeaderOffset + 8, 0x102);
  Write64(truncated_segment, kLoadHeaderOffset + 40, 4);
  Write64(truncated_segment, kLoadHeaderOffset + 48, 1);
  CheckError(std::move(truncated_segment),
             ElfError::kSegmentFileRangeOutOfRange,
             "truncated segment returned the wrong error");

  auto overflowing_segment = image;
  Write64(overflowing_segment, kLoadHeaderOffset + 16,
          std::numeric_limits<std::uint64_t>::max() - 3);
  Write64(overflowing_segment, kLoadHeaderOffset + 48, 1);
  CheckError(std::move(overflowing_segment),
             ElfError::kSegmentAddressRangeOverflow,
             "overflowing guest range returned the wrong error");

  auto constructed_metadata = parsed.metadata;
  constructed_metadata.program_headers[0].virtual_address =
      std::numeric_limits<std::uint64_t>::max() - 3;
  constructed_metadata.program_headers[0].memory_size = 8;
  const auto rejected_range =
      kajps5::loader::CalculateElfLoadRange(constructed_metadata);
  Check(rejected_range.error == ElfError::kSegmentAddressRangeOverflow,
        "load range accepted an overflowing constructed segment");

  auto bad_alignment = image;
  Write64(bad_alignment, kLoadHeaderOffset + 48, 3);
  CheckError(std::move(bad_alignment), ElfError::kInvalidSegmentAlignment,
             "invalid alignment returned the wrong error");

  auto overlapping_segments = image;
  Write32(overlapping_segments, second_header, 1);
  Write32(overlapping_segments, second_header + 4, 4);
  Write64(overlapping_segments, second_header + 16, 0x1004);
  Write64(overlapping_segments, second_header + 32, 0);
  Write64(overlapping_segments, second_header + 40, 4);
  Write64(overlapping_segments, second_header + 48, 1);
  CheckError(std::move(overlapping_segments),
             ElfError::kOverlappingLoadSegments,
             "overlapping load segments returned the wrong error");

  GuestMemory short_memory(0x1000, 4, GuestMemoryProtection::kNone);
  const auto rejected_load = LoadElf64(image, short_memory);
  Check(rejected_load.error == ElfError::kGuestRangeOutOfRange,
        "out-of-range load returned the wrong error");
  Check(short_memory.regions().empty(),
        "rejected load created a partial mapping");

  GuestMemory overflow_memory(0x1000, 0x100,
                              GuestMemoryProtection::kNone);
  const auto overflow_load = LoadElf64(
      image, overflow_memory, std::numeric_limits<std::uint64_t>::max());
  Check(overflow_load.error == ElfError::kSegmentAddressRangeOverflow &&
            overflow_memory.regions().empty(),
        "overflowing load bias changed guest mappings");

  GuestMemory conflicting_memory(0x1000, 0x200,
                                 GuestMemoryProtection::kNone);
  Check(conflicting_memory.Map(0x1100, 4, GuestMemoryProtection::kRead),
        "mapping conflict setup failed");
  const auto conflicting_load =
      LoadElf64(multi_segment_image, conflicting_memory);
  Check(conflicting_load.error == ElfError::kGuestMappingConflict,
        "mapping conflict returned the wrong error");
  Check(conflicting_memory.regions().size() == 1,
        "mapping conflict created a partial mapping");
  Check(conflicting_memory.regions()[0].address == 0x1100,
        "mapping conflict changed the existing region");

  Check(kajps5::loader::ElfErrorName(ElfError::kInvalidMagic) ==
            "invalid-magic",
        "stable error name is incorrect");

  return failures == 0 ? 0 : 1;
}
