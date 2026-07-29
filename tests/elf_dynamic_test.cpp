// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "loader/elf.h"
#include "loader/elf_trace.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kDynamicOffset = 0x100;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::size_t kDynamicEntryCount = 5;
constexpr std::size_t kStringTableOffset = 0x180;
constexpr std::uint64_t kStringTableAddress = 0x3000;
constexpr std::size_t kStringTableSize = 0x40;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "elf_dynamic_test: " << message << '\n';
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

void WriteDynamic(std::vector<std::byte>& image, std::size_t index,
                  std::int64_t tag, std::uint64_t value) {
  const auto offset = kDynamicOffset + index * kDynamicEntrySize;
  Write64(image, offset, static_cast<std::uint64_t>(tag));
  Write64(image, offset + sizeof(std::uint64_t), value);
}

void WriteString(std::vector<std::byte>& image, std::size_t offset,
                 std::string_view value) {
  for (std::size_t index = 0; index < value.size(); ++index) {
    image[offset + index] = static_cast<std::byte>(value[index]);
  }
  image[offset + value.size()] = std::byte{0};
}

std::vector<std::byte> MakeDynamicElf() {
  std::vector<std::byte> image(kStringTableOffset + kStringTableSize);
  image[0] = std::byte{0x7f};
  image[1] = std::byte{'E'};
  image[2] = std::byte{'L'};
  image[3] = std::byte{'F'};
  image[4] = std::byte{2};
  image[5] = std::byte{1};
  image[6] = std::byte{1};
  Write16(image, 16, 3);
  Write16(image, 18, 62);
  Write32(image, 20, 1);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, 56);
  Write16(image, 56, 2);

  Write32(image, kProgramHeaderOffset, 2);
  Write32(image, kProgramHeaderOffset + 4, 4);
  Write64(image, kProgramHeaderOffset + 8, kDynamicOffset);
  Write64(image, kProgramHeaderOffset + 16, 0x2000);
  Write64(image, kProgramHeaderOffset + 32,
          kDynamicEntryCount * kDynamicEntrySize);
  Write64(image, kProgramHeaderOffset + 40,
          kDynamicEntryCount * kDynamicEntrySize);
  Write64(image, kProgramHeaderOffset + 48, 8);

  const auto load_header = kProgramHeaderOffset + 56;
  Write32(image, load_header, 1);
  Write32(image, load_header + 4, 4);
  Write64(image, load_header + 8, kStringTableOffset);
  Write64(image, load_header + 16, kStringTableAddress);
  Write64(image, load_header + 32, kStringTableSize);
  Write64(image, load_header + 40, kStringTableSize);
  Write64(image, load_header + 48, 1);

  WriteDynamic(image, 0, 5, kStringTableAddress);
  WriteDynamic(image, 1, 10, kStringTableSize);
  WriteDynamic(image, 2, 1, 1);
  WriteDynamic(image, 3, 14, 14);
  WriteDynamic(image, 4, 0, 0);
  WriteString(image, kStringTableOffset + 1, "libfirst.prx");
  WriteString(image, kStringTableOffset + 14, "sample.elf");
  return image;
}

void CheckError(std::vector<std::byte> image, kajps5::loader::ElfError expected,
                const char* message) {
  const auto parsed = kajps5::loader::ParseElf64(image);
  Check(parsed.error == expected, message);
}

}  // namespace

int main() {
  using kajps5::loader::ElfError;
  using kajps5::loader::ParseElf64;

  const auto image = MakeDynamicElf();
  const auto parsed = ParseElf64(image);
  Check(static_cast<bool>(parsed), "valid dynamic table was rejected");
  Check(parsed.metadata.dynamic_entries.size() == 4,
        "dynamic entry count is incorrect");
  Check(parsed.metadata.dynamic_entries[0].tag == 5 &&
            parsed.metadata.dynamic_entries[0].value == kStringTableAddress,
        "string-table entry is incorrect");
  Check(parsed.metadata.dynamic_entries[1].tag == 10 &&
            parsed.metadata.dynamic_entries[1].value == kStringTableSize,
        "string-table size entry is incorrect");
  Check(parsed.metadata.dynamic_entries[2].tag == 1 &&
            parsed.metadata.dynamic_entries[2].value == 1,
        "needed-library entry is incorrect");
  Check(parsed.metadata.dynamic_info.needed_libraries.size() == 1 &&
            parsed.metadata.dynamic_info.needed_libraries[0] ==
                "libfirst.prx",
        "needed-library name is incorrect");
  Check(parsed.metadata.dynamic_info.shared_object_name == "sample.elf",
        "shared-object name is incorrect");
  Check(kajps5::loader::FormatElfTrace(parsed.metadata)
                .find("elf.dynamic_entries=4\n"
                      "elf.dynamic_string_table_size=64\n"
                      "elf.needed_libraries=1\n"
                      "elf.has_soname=1\n") != std::string::npos,
        "dynamic summary is missing from the stable trace");

  auto invalid_size = image;
  Write64(invalid_size, kProgramHeaderOffset + 32, 24);
  CheckError(std::move(invalid_size), ElfError::kInvalidDynamicSegmentSize,
             "misaligned dynamic table returned the wrong error");

  auto truncated = image;
  Write64(truncated, kProgramHeaderOffset + 8, image.size() - 8);
  Write64(truncated, kProgramHeaderOffset + 32, 16);
  CheckError(std::move(truncated), ElfError::kDynamicSegmentFileRangeOutOfRange,
             "truncated dynamic table returned the wrong error");

  auto unterminated = image;
  WriteDynamic(unterminated, 4, 1, 8);
  CheckError(std::move(unterminated), ElfError::kUnterminatedDynamicTable,
             "unterminated dynamic table returned the wrong error");

  auto multiple = image;
  Write16(multiple, 56, 3);
  const auto second_dynamic_header = kProgramHeaderOffset + 2 * 56;
  Write32(multiple, second_dynamic_header, 2);
  Write32(multiple, second_dynamic_header + 4, 4);
  Write64(multiple, second_dynamic_header + 8, kDynamicOffset);
  Write64(multiple, second_dynamic_header + 16, 0x4000);
  Write64(multiple, second_dynamic_header + 32,
          kDynamicEntryCount * kDynamicEntrySize);
  Write64(multiple, second_dynamic_header + 40,
          kDynamicEntryCount * kDynamicEntrySize);
  Write64(multiple, second_dynamic_header + 48, 8);
  CheckError(std::move(multiple), ElfError::kMultipleDynamicSegments,
             "multiple dynamic tables returned the wrong error");

  auto incomplete_strings = image;
  WriteDynamic(incomplete_strings, 1, 0x1234, kStringTableSize);
  CheckError(std::move(incomplete_strings),
             ElfError::kIncompleteDynamicStringTable,
             "incomplete string table returned the wrong error");

  auto unmapped_strings = image;
  WriteDynamic(unmapped_strings, 0, 5, 0x4000);
  CheckError(std::move(unmapped_strings),
             ElfError::kDynamicStringTableNotFileBacked,
             "unmapped string table returned the wrong error");

  auto invalid_string_offset = image;
  WriteDynamic(invalid_string_offset, 2, 1, kStringTableSize);
  CheckError(std::move(invalid_string_offset),
             ElfError::kDynamicStringOffsetOutOfRange,
             "invalid string offset returned the wrong error");

  auto unterminated_string = image;
  WriteDynamic(unterminated_string, 1, 10, 13);
  CheckError(std::move(unterminated_string),
             ElfError::kUnterminatedDynamicString,
             "unterminated string returned the wrong error");

  Check(kajps5::loader::ElfErrorName(ElfError::kUnterminatedDynamicTable) ==
            "unterminated-dynamic-table",
        "dynamic-table error name is unstable");

  return failures == 0 ? 0 : 1;
}
