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
constexpr std::size_t kDataOffset = 0x180;
constexpr std::uint64_t kDataAddress = 0x3000;
constexpr std::size_t kStringOffset = kDataOffset;
constexpr std::uint64_t kStringAddress = kDataAddress;
constexpr std::size_t kStringSize = 16;
constexpr std::size_t kSymbolOffset = kDataOffset + 0x20;
constexpr std::uint64_t kSymbolAddress = kDataAddress + 0x20;
constexpr std::size_t kSymbolEntrySize = 24;
constexpr std::size_t kHashOffset = kDataOffset + 0x50;
constexpr std::uint64_t kHashAddress = kDataAddress + 0x50;
constexpr std::size_t kDynamicEntrySize = 16;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "elf_symbol_test: " << message << '\n';
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

std::vector<std::byte> MakeSymbolElf() {
  constexpr std::size_t kDataSize = 0x70;
  std::vector<std::byte> image(kDataOffset + kDataSize);
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
  Write64(image, kProgramHeaderOffset + 32, 6 * kDynamicEntrySize);
  Write64(image, kProgramHeaderOffset + 40, 6 * kDynamicEntrySize);
  Write64(image, kProgramHeaderOffset + 48, 8);

  const auto data_header = kProgramHeaderOffset + 56;
  Write32(image, data_header, 1);
  Write32(image, data_header + 4, 4);
  Write64(image, data_header + 8, kDataOffset);
  Write64(image, data_header + 16, kDataAddress);
  Write64(image, data_header + 32, kDataSize);
  Write64(image, data_header + 40, kDataSize);
  Write64(image, data_header + 48, 8);

  WriteDynamic(image, 0, 5, kStringAddress);
  WriteDynamic(image, 1, 10, kStringSize);
  WriteDynamic(image, 2, 4, kHashAddress);
  WriteDynamic(image, 3, 6, kSymbolAddress);
  WriteDynamic(image, 4, 11, kSymbolEntrySize);
  WriteDynamic(image, 5, 0, 0);

  WriteString(image, kStringOffset + 1, "example");
  const auto second_symbol = kSymbolOffset + kSymbolEntrySize;
  Write32(image, second_symbol, 1);
  image[second_symbol + 4] = std::byte{0x12};

  Write32(image, kHashOffset, 1);
  Write32(image, kHashOffset + 4, 2);
  Write32(image, kHashOffset + 8, 1);
  Write32(image, kHashOffset + 12, 0);
  Write32(image, kHashOffset + 16, 0);
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

  const auto image = MakeSymbolElf();
  const auto parsed = ParseElf64(image);
  Check(static_cast<bool>(parsed), "valid dynamic symbol table was rejected");
  Check(parsed.metadata.dynamic_info.symbols.size() == 2,
        "dynamic symbol count is incorrect");
  const auto& symbol = parsed.metadata.dynamic_info.symbols[1];
  Check(symbol.name == "example" && symbol.binding() == 1 &&
            symbol.type() == 2 && symbol.section_index == 0,
        "dynamic symbol fields are incorrect");
  Check(kajps5::loader::FormatElfTrace(parsed.metadata)
                .find("elf.symbols=2\nelf.undefined_symbols=1\n") !=
            std::string::npos,
        "symbol counts are missing from the stable trace");

  auto incomplete = image;
  WriteDynamic(incomplete, 3, 0x1234, kSymbolAddress);
  CheckError(std::move(incomplete),
             ElfError::kIncompleteDynamicSymbolMetadata,
             "incomplete symbol metadata returned the wrong error");

  auto invalid_entry_size = image;
  WriteDynamic(invalid_entry_size, 4, 11, 16);
  CheckError(std::move(invalid_entry_size), ElfError::kInvalidSymbolEntrySize,
             "invalid symbol entry size returned the wrong error");

  auto unmapped_hash = image;
  WriteDynamic(unmapped_hash, 2, 4, 0x4000);
  CheckError(std::move(unmapped_hash), ElfError::kHashTableNotFileBacked,
             "unmapped hash table returned the wrong error");

  auto unmapped_symbols = image;
  WriteDynamic(unmapped_symbols, 3, 6, 0x4000);
  CheckError(std::move(unmapped_symbols), ElfError::kSymbolTableNotFileBacked,
             "unmapped symbol table returned the wrong error");

  auto invalid_name = image;
  Write32(invalid_name, kSymbolOffset + kSymbolEntrySize, kStringSize);
  CheckError(std::move(invalid_name), ElfError::kSymbolNameOffsetOutOfRange,
             "invalid symbol name returned the wrong error");

  auto unterminated_name = image;
  WriteDynamic(unterminated_name, 1, 10, 8);
  CheckError(std::move(unterminated_name), ElfError::kUnterminatedSymbolName,
             "unterminated symbol name returned the wrong error");

  Check(kajps5::loader::ElfErrorName(ElfError::kInvalidSymbolEntrySize) ==
            "invalid-symbol-entry-size",
        "symbol error name is unstable");
  return failures == 0 ? 0 : 1;
}
