// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"

namespace {

constexpr std::size_t kSelfHeaderSize = 0x20;
constexpr std::size_t kSelfSegmentSize = 0x20;
constexpr std::size_t kElfOffset = kSelfHeaderSize + kSelfSegmentSize;
constexpr std::size_t kProgramHeaderOffset = kElfOffset + 0x40;
constexpr std::size_t kPayloadOffset = 0x180;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "self_loader_test: " << message << '\n';
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

std::vector<std::byte> MakeSelf(std::uint64_t segment_type = 0x800,
                                std::uint64_t segment_offset = kPayloadOffset,
                                std::uint64_t elf_segment_offset = 0x100) {
  std::vector<std::byte> image(kPayloadOffset + 4);

  image[0] = std::byte{0x54};
  image[1] = std::byte{0x14};
  image[2] = std::byte{0xf5};
  image[3] = std::byte{0xee};
  image[4] = std::byte{0x10};
  image[5] = std::byte{0x01};
  image[6] = std::byte{0x01};
  image[7] = std::byte{0x12};
  image[8] = std::byte{0x01};
  image[9] = std::byte{0x01};
  image[11] = std::byte{0x10};
  Write16(image, 12, kSelfHeaderSize);
  Write16(image, 14, kSelfHeaderSize);
  Write64(image, 16, image.size());
  Write16(image, 24, 1);
  Write16(image, 26, 0x32);

  Write64(image, kSelfHeaderSize, segment_type);
  Write64(image, kSelfHeaderSize + 8, segment_offset);
  Write64(image, kSelfHeaderSize + 16, 4);
  Write64(image, kSelfHeaderSize + 24, 4);

  image[kElfOffset] = std::byte{0x7f};
  image[kElfOffset + 1] = std::byte{'E'};
  image[kElfOffset + 2] = std::byte{'L'};
  image[kElfOffset + 3] = std::byte{'F'};
  image[kElfOffset + 4] = std::byte{2};
  image[kElfOffset + 5] = std::byte{1};
  image[kElfOffset + 6] = std::byte{1};
  image[kElfOffset + 7] = std::byte{9};
  image[kElfOffset + 8] = std::byte{2};
  Write16(image, kElfOffset + 16, 0xfe10);
  Write16(image, kElfOffset + 18, 62);
  Write32(image, kElfOffset + 20, 1);
  Write64(image, kElfOffset + 24, 0x2002);
  Write64(image, kElfOffset + 32, 0x40);
  Write16(image, kElfOffset + 52, 64);
  Write16(image, kElfOffset + 54, 56);
  Write16(image, kElfOffset + 56, 1);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 5);
  Write64(image, kProgramHeaderOffset + 8, elf_segment_offset);
  Write64(image, kProgramHeaderOffset + 16, 0x2000);
  Write64(image, kProgramHeaderOffset + 24, 0x2000);
  Write64(image, kProgramHeaderOffset + 32, 4);
  Write64(image, kProgramHeaderOffset + 40, 8);
  Write64(image, kProgramHeaderOffset + 48,
          elf_segment_offset == 0x100 ? 0x100 : 1);

  image[kPayloadOffset] = std::byte{0xde};
  image[kPayloadOffset + 1] = std::byte{0xad};
  image[kPayloadOffset + 2] = std::byte{0xbe};
  image[kPayloadOffset + 3] = std::byte{0xef};
  return image;
}

std::vector<std::byte> MakeContainedDynamicSelf() {
  auto image = MakeSelf();
  image.resize(kPayloadOffset + 0x20);
  Write16(image, kElfOffset + 56, 2);
  Write64(image, kProgramHeaderOffset + 32, 0x20);
  Write64(image, kProgramHeaderOffset + 40, 0x20);
  Write64(image, kSelfHeaderSize + 16, 0x20);
  Write64(image, kSelfHeaderSize + 24, 0x20);

  const auto dynamic_header = kProgramHeaderOffset + 56;
  Write32(image, dynamic_header, 2);
  Write32(image, dynamic_header + 4, 4);
  Write64(image, dynamic_header + 8, 0x108);
  Write64(image, dynamic_header + 16, 0x2008);
  Write64(image, dynamic_header + 24, 0x2008);
  Write64(image, dynamic_header + 32, 16);
  Write64(image, dynamic_header + 40, 16);
  Write64(image, dynamic_header + 48, 8);

  for (std::size_t index = 8; index < 0x20; ++index) {
    image[kPayloadOffset + index] = std::byte{0};
  }
  return image;
}

void CheckError(std::vector<std::byte> image, kajps5::loader::ElfError error,
                const char* message) {
  Check(kajps5::loader::ParseExecutable64(image).error == error, message);
}

}  // namespace

int main() {
  using kajps5::loader::ElfContainerKind;
  using kajps5::loader::ElfError;
  using kajps5::loader::LoadExecutable64;
  using kajps5::loader::ParseExecutable64;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  const auto image = MakeSelf();
  Check(kajps5::loader::ParseElf64(image).error == ElfError::kInvalidMagic,
        "bare ELF parser accepted a SELF container");

  const auto parsed = ParseExecutable64(image);
  Check(static_cast<bool>(parsed), "valid synthetic PS5 SELF was rejected");
  Check(parsed.metadata.container == ElfContainerKind::kSelf,
        "SELF container kind was not recorded");
  Check(parsed.metadata.elf_file_offset == kElfOffset,
        "embedded ELF offset is incorrect");
  Check(parsed.metadata.self_segment_count == 1,
        "SELF segment count is incorrect");
  Check(parsed.metadata.program_headers.size() == 1,
        "embedded program header count is incorrect");
  Check(parsed.metadata.program_headers[0].offset == 0x100,
        "logical ELF segment offset changed");
  Check(parsed.metadata.program_headers[0].file_offset == kPayloadOffset,
        "SELF payload offset was not resolved");

  const auto trace = kajps5::loader::FormatElfTrace(parsed.metadata);
  Check(trace.find("elf.container=self\n") != std::string::npos,
        "SELF trace omits the container kind");
  Check(trace.find("offset=0x0000000000000100 "
                   "file_offset=0x0000000000000180") != std::string::npos,
        "SELF trace omits the resolved payload offset");

  GuestMemory memory(0x2000, 0x100, GuestMemoryProtection::kNone);
  const auto loaded = LoadExecutable64(image, memory);
  Check(static_cast<bool>(loaded), "valid synthetic PS5 SELF did not load");
  std::array<std::byte, 8> loaded_bytes{};
  Check(memory.Read(0x2000, loaded_bytes), "loaded SELF memory is unreadable");
  const std::array expected = {
      std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0}};
  Check(loaded_bytes == expected, "SELF payload copy or zero fill is wrong");

  Check(static_cast<bool>(ParseExecutable64(MakeSelf(0x802))),
        "resolved dumped encrypted payload was rejected");
  Check(static_cast<bool>(ParseExecutable64(MakeSelf(0x808))),
        "resolved dumped compressed payload was rejected");

  const auto embedded_fallback = ParseExecutable64(MakeSelf(0));
  Check(static_cast<bool>(embedded_fallback) &&
            embedded_fallback.metadata.program_headers[0].file_offset ==
                kElfOffset + 0x100,
        "embedded-ELF-relative payload fallback was not resolved");
  const auto absolute_fallback = ParseExecutable64(MakeSelf(0, 0, 0x180));
  Check(static_cast<bool>(absolute_fallback) &&
            absolute_fallback.metadata.program_headers[0].file_offset ==
                kPayloadOffset,
        "absolute SELF payload fallback was not resolved");

  const auto contained_dynamic = ParseExecutable64(MakeContainedDynamicSelf());
  Check(static_cast<bool>(contained_dynamic) &&
            contained_dynamic.metadata.program_headers.size() == 2 &&
            contained_dynamic.metadata.program_headers[1].file_offset ==
                kPayloadOffset + 8 &&
            contained_dynamic.metadata.dynamic_entries.empty(),
        "SELF dynamic segment was not resolved through its containing load "
        "segment");

  auto signing_variant = image;
  signing_variant[4] = std::byte{0x7a};
  signing_variant[8] = std::byte{0x55};
  signing_variant[11] = std::byte{0x33};
  Write16(signing_variant, 26, 0xabcd);
  Check(static_cast<bool>(ParseExecutable64(signing_variant)),
        "structurally valid SELF signing variant was rejected");

  auto ps4_magic_variant = image;
  ps4_magic_variant[0] = std::byte{0x4f};
  ps4_magic_variant[1] = std::byte{0x15};
  ps4_magic_variant[2] = std::byte{0x3d};
  ps4_magic_variant[3] = std::byte{0x1d};
  Check(static_cast<bool>(ParseExecutable64(ps4_magic_variant)),
        "recognized PS4 SELF magic was rejected");

  std::vector<std::byte> truncated_header(16);
  truncated_header[0] = std::byte{0x54};
  truncated_header[1] = std::byte{0x14};
  truncated_header[2] = std::byte{0xf5};
  truncated_header[3] = std::byte{0xee};
  CheckError(std::move(truncated_header), ElfError::kSelfHeaderOutOfRange,
             "truncated SELF header returned the wrong error");

  auto unsupported_header = image;
  unsupported_header[7] = std::byte{0};
  CheckError(std::move(unsupported_header), ElfError::kUnsupportedSelfHeader,
             "unsupported SELF structure returned the wrong error");

  auto truncated_embedded_elf = image;
  Write16(truncated_embedded_elf, 24, 12);
  truncated_embedded_elf.resize(0x100);
  CheckError(std::move(truncated_embedded_elf),
             ElfError::kSelfEmbeddedElfOutOfRange,
             "truncated embedded ELF returned the wrong error");

  CheckError(MakeSelf(0x802, 0x1000, 0x1000),
             ElfError::kUnsupportedEncryptedSelfSegment,
             "unavailable encrypted SELF payload returned the wrong error");
  CheckError(MakeSelf(0x808, 0x1000, 0x1000),
             ElfError::kUnsupportedCompressedSelfSegment,
             "unavailable compressed SELF payload returned the wrong error");
  CheckError(MakeSelf(0x800, 0x1000, 0x1000),
             ElfError::kSelfSegmentFileRangeOutOfRange,
             "out-of-range SELF payload returned the wrong error");
  CheckError(MakeSelf(0, 0x1000, 0x1000), ElfError::kSelfSegmentMappingNotFound,
             "missing SELF mapping returned the wrong error");

  return failures == 0 ? 0 : 1;
}
