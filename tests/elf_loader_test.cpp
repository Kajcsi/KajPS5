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

  const auto image = MakePublicTestElf();
  const auto parsed = ParseElf64(image);
  Check(static_cast<bool>(parsed), "valid public ELF fixture was rejected");
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

  auto generic_image = image;
  generic_image[7] = std::byte{0};
  generic_image[8] = std::byte{0};
  Write16(generic_image, 16, 2);
  Check(static_cast<bool>(ParseElf64(generic_image)),
        "generic System V ELF64 fixture was rejected");

  GuestMemory memory(0x1000, 0x100);
  Check(memory.Fill(0x1000, 0x100, std::byte{0xcc}),
        "test memory setup failed");
  const auto loaded = LoadElf64(image, memory);
  Check(static_cast<bool>(loaded), "valid ELF load failed");
  Check(loaded.loaded_segment_count == 1, "loaded segment count is incorrect");
  Check(loaded.loaded_file_bytes == 4, "loaded file byte count is incorrect");
  Check(loaded.zero_filled_bytes == 4, "zero-filled byte count is incorrect");

  std::array<std::byte, 9> loaded_bytes{};
  Check(memory.Read(0x1000, loaded_bytes), "loaded memory read failed");
  const std::array expected = {
      std::byte{0xde}, std::byte{0xad}, std::byte{0xbe},
      std::byte{0xef}, std::byte{0},    std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0xcc}};
  Check(loaded_bytes == expected, "segment copy or zero fill is incorrect");

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

  auto bad_alignment = image;
  Write64(bad_alignment, kLoadHeaderOffset + 48, 3);
  CheckError(std::move(bad_alignment), ElfError::kInvalidSegmentAlignment,
             "invalid alignment returned the wrong error");

  GuestMemory short_memory(0x1000, 4);
  Check(short_memory.Fill(0x1000, 4, std::byte{0xcc}),
        "short memory setup failed");
  const auto rejected_load = LoadElf64(image, short_memory);
  Check(rejected_load.error == ElfError::kGuestRangeOutOfRange,
        "out-of-range load returned the wrong error");
  std::array<std::byte, 4> unchanged{};
  Check(short_memory.Read(0x1000, unchanged),
        "read after rejected load failed");
  Check(unchanged == std::array{std::byte{0xcc}, std::byte{0xcc},
                                std::byte{0xcc}, std::byte{0xcc}},
        "rejected load changed guest memory");

  Check(kajps5::loader::ElfErrorName(ElfError::kInvalidMagic) ==
            "invalid-magic",
        "stable error name is incorrect");

  return failures == 0 ? 0 : 1;
}
