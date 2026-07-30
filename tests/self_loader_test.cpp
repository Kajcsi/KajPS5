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
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"
#include "loader/launch_metadata.h"
#include "loader/lifecycle_plan.h"
#include "loader/relocator.h"

namespace {

constexpr std::size_t kSelfHeaderSize = 0x20;
constexpr std::size_t kSelfSegmentSize = 0x20;
constexpr std::size_t kElfOffset = kSelfHeaderSize + kSelfSegmentSize;
constexpr std::size_t kProgramHeaderOffset = kElfOffset + 0x40;
constexpr std::size_t kPayloadOffset = 0x180;

constexpr std::size_t kLinkedPayloadOffset = 0x500;
constexpr std::size_t kLinkedPayloadSize = 0x400;
constexpr std::uint64_t kLinkedLoadFileOffset = 0x200;
constexpr std::uint64_t kLinkedLoadAddress = 0x4000;

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
constexpr std::int64_t kTagInit = 12;
constexpr std::int64_t kTagFini = 13;
constexpr std::int64_t kTagInitArray = 25;
constexpr std::int64_t kTagFiniArray = 26;
constexpr std::int64_t kTagInitArraySize = 27;
constexpr std::int64_t kTagFiniArraySize = 28;
constexpr std::int64_t kTagPreinitArray = 32;
constexpr std::int64_t kTagPreinitArraySize = 33;

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

void WriteDynamic(std::vector<std::byte>& image, std::size_t base,
                  std::size_t index, std::int64_t tag, std::uint64_t value) {
  const auto offset = base + index * 16;
  Write64(image, offset, static_cast<std::uint64_t>(tag));
  Write64(image, offset + 8, value);
}

void WriteString(std::vector<std::byte>& image, std::size_t base,
                 std::size_t offset, std::string_view value) {
  for (std::size_t index = 0; index < value.size(); ++index) {
    image[base + offset + index] = static_cast<std::byte>(value[index]);
  }
  image[base + offset + value.size()] = std::byte{0};
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

std::vector<std::byte> MakeLinkedSelf() {
  constexpr std::size_t dynamic_relative = 0x40;
  constexpr std::size_t dynamic_count = 19;
  constexpr std::size_t strings_relative = 0x180;
  constexpr std::size_t strings_size = 0x60;
  constexpr std::size_t symbols_relative = 0x240;
  constexpr std::size_t rela_relative = 0x280;
  constexpr std::size_t jump_rela_relative = 0x2f8;
  constexpr std::size_t targets_relative = 0x320;

  std::vector<std::byte> image(kLinkedPayloadOffset + kLinkedPayloadSize);

  image[0] = std::byte{0x54};
  image[1] = std::byte{0x14};
  image[2] = std::byte{0xf5};
  image[3] = std::byte{0xee};
  image[4] = std::byte{0x10};
  image[5] = std::byte{0x01};
  image[6] = std::byte{0x01};
  image[7] = std::byte{0x12};
  Write16(image, 12, kSelfHeaderSize);
  Write16(image, 14, kSelfHeaderSize);
  Write64(image, 16, image.size());
  Write16(image, 24, 1);

  Write64(image, kSelfHeaderSize, 0x800);
  Write64(image, kSelfHeaderSize + 8, kLinkedPayloadOffset);
  Write64(image, kSelfHeaderSize + 16, kLinkedPayloadSize);
  Write64(image, kSelfHeaderSize + 24, kLinkedPayloadSize);

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
  Write64(image, kElfOffset + 24, kLinkedLoadAddress);
  Write64(image, kElfOffset + 32, 0x40);
  Write16(image, kElfOffset + 52, 64);
  Write16(image, kElfOffset + 54, 56);
  Write16(image, kElfOffset + 56, 4);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 7);
  Write64(image, kProgramHeaderOffset + 8, kLinkedLoadFileOffset);
  Write64(image, kProgramHeaderOffset + 16, kLinkedLoadAddress);
  Write64(image, kProgramHeaderOffset + 24, kLinkedLoadAddress);
  Write64(image, kProgramHeaderOffset + 32, kLinkedPayloadSize);
  Write64(image, kProgramHeaderOffset + 40, kLinkedPayloadSize);
  Write64(image, kProgramHeaderOffset + 48, 0x100);

  const auto dynamic_header = kProgramHeaderOffset + 56;
  Write32(image, dynamic_header, 2);
  Write32(image, dynamic_header + 4, 4);
  Write64(image, dynamic_header + 8,
          kLinkedLoadFileOffset + dynamic_relative);
  Write64(image, dynamic_header + 16,
          kLinkedLoadAddress + dynamic_relative);
  Write64(image, dynamic_header + 24,
          kLinkedLoadAddress + dynamic_relative);
  Write64(image, dynamic_header + 32, dynamic_count * 16);
  Write64(image, dynamic_header + 40, dynamic_count * 16);
  Write64(image, dynamic_header + 48, 8);

  const auto tls_header = kProgramHeaderOffset + 112;
  Write32(image, tls_header, 7);
  Write32(image, tls_header + 4, 4);
  Write64(image, tls_header + 8, kLinkedLoadFileOffset + 0x380);
  Write64(image, tls_header + 16, kLinkedLoadAddress + 0x380);
  Write64(image, tls_header + 24, kLinkedLoadAddress + 0x380);
  Write64(image, tls_header + 32, 0x10);
  Write64(image, tls_header + 40, 0x20);
  Write64(image, tls_header + 48, 0x10);

  const auto process_parameters_header = kProgramHeaderOffset + 168;
  Write32(image, process_parameters_header, 0x61000001);
  Write32(image, process_parameters_header + 4, 4);
  Write64(image, process_parameters_header + 8,
          kLinkedLoadFileOffset + 0x3a0);
  Write64(image, process_parameters_header + 16,
          kLinkedLoadAddress + 0x3a0);
  Write64(image, process_parameters_header + 24,
          kLinkedLoadAddress + 0x3a0);
  Write64(image, process_parameters_header + 32, 0x10);
  Write64(image, process_parameters_header + 40, 0x10);
  Write64(image, process_parameters_header + 48, 8);

  const auto dynamic = kLinkedPayloadOffset + dynamic_relative;
  WriteDynamic(image, dynamic, 0, kTagSceStringTable,
               kLinkedLoadAddress + strings_relative);
  WriteDynamic(image, dynamic, 1, kTagSceStringTableSize, strings_size);
  WriteDynamic(image, dynamic, 2, kTagSceSymbolTable,
               kLinkedLoadAddress + symbols_relative);
  WriteDynamic(image, dynamic, 3, kTagSceSymbolTableSize, 48);
  WriteDynamic(image, dynamic, 4, kTagSceRela,
               kLinkedLoadAddress + rela_relative);
  WriteDynamic(image, dynamic, 5, kTagSceRelaSize, 120);
  WriteDynamic(image, dynamic, 6, kTagSceJumpRela,
               kLinkedLoadAddress + jump_rela_relative);
  WriteDynamic(image, dynamic, 7, kTagSceJumpRelaSize, 24);
  WriteDynamic(image, dynamic, 8, kTagSceNeededModule,
               PackModule(0x0040, 1, 2, 1));
  WriteDynamic(image, dynamic, 9, kTagSceImportLibrary,
               PackLibrary(0x1234, 0x0100, 14));
  WriteDynamic(image, dynamic, 10, kTagInit, kLinkedLoadAddress);
  WriteDynamic(image, dynamic, 11, kTagFini, kLinkedLoadAddress + 0x20);
  WriteDynamic(image, dynamic, 12, kTagInitArray,
               kLinkedLoadAddress + targets_relative + 0x20);
  WriteDynamic(image, dynamic, 13, kTagInitArraySize, 8);
  WriteDynamic(image, dynamic, 14, kTagFiniArray,
               kLinkedLoadAddress + targets_relative + 0x28);
  WriteDynamic(image, dynamic, 15, kTagFiniArraySize, 8);
  WriteDynamic(image, dynamic, 16, kTagPreinitArray,
               kLinkedLoadAddress + targets_relative + 0x18);
  WriteDynamic(image, dynamic, 17, kTagPreinitArraySize, 8);
  WriteDynamic(image, dynamic, 18, 0, 0);

  const auto strings = kLinkedPayloadOffset + strings_relative;
  WriteString(image, strings, 1, "kernelModule");
  WriteString(image, strings, 14, "libkernel");
  WriteString(image, strings, 24, "open#BI0#BA");

  const auto symbol = kLinkedPayloadOffset + symbols_relative + 24;
  Write32(image, symbol, 24);
  image[symbol + 4] = std::byte{0x12};

  const auto rela = kLinkedPayloadOffset + rela_relative;
  Write64(image, rela, kLinkedLoadAddress + targets_relative);
  Write64(image, rela + 8, 8);
  Write64(image, rela + 16, 0x55);
  Write64(image, rela + 24, kLinkedLoadAddress + targets_relative + 0x10);
  Write64(image, rela + 32, 16);
  Write64(image, rela + 48, kLinkedLoadAddress + targets_relative + 0x18);
  Write64(image, rela + 56, 8);
  Write64(image, rela + 64, kLinkedLoadAddress + 0x10);
  Write64(image, rela + 72, kLinkedLoadAddress + targets_relative + 0x20);
  Write64(image, rela + 80, 8);
  Write64(image, rela + 88, kLinkedLoadAddress + 0x20);
  Write64(image, rela + 96, kLinkedLoadAddress + targets_relative + 0x28);
  Write64(image, rela + 104, 8);
  Write64(image, rela + 112, kLinkedLoadAddress + 0x30);

  const auto jump_rela = kLinkedPayloadOffset + jump_rela_relative;
  Write64(image, jump_rela, kLinkedLoadAddress + targets_relative + 8);
  Write64(image, jump_rela + 8, (std::uint64_t{1} << 32U) | 7U);
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

  const auto linked_image = MakeLinkedSelf();
  GuestMemory linked_memory(kLinkedLoadAddress, kLinkedPayloadSize,
                            GuestMemoryProtection::kNone);
  const auto linked_load = LoadExecutable64(linked_image, linked_memory);
  Check(static_cast<bool>(linked_load),
        "synthetic PS5 SELF with link metadata did not load");
  kajps5::hle::ImportRegistry registry;
  Check(registry.Register("libkernel", "open", 0x8877665544332211) ==
            kajps5::hle::ImportRegistryStatus::kOk,
        "synthetic SELF import registration failed");
  const auto linked = kajps5::loader::ApplyRelocations(
      linked_load.metadata, linked_memory, registry, 0, 1);
  Check(linked && linked.applied_count == 6 &&
            linked.resolved_import_count == 1 &&
            linked.unresolved_import_count == 0,
        "synthetic PS5 SELF did not complete checked relocation linking");
  std::array<std::byte, 24> linked_values{};
  const std::array expected_linked_values = {
      std::byte{0x55}, std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
      std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88},
      std::byte{1},    std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0}};
  Check(linked_memory.Read(kLinkedLoadAddress + 0x320, linked_values) &&
            linked_values == expected_linked_values,
        "synthetic PS5 SELF relocation values are incorrect");
  const auto launch =
      kajps5::loader::AnalyzeLaunchMetadata(linked_load.metadata);
  Check(launch && launch.metadata.entry_point == kLinkedLoadAddress &&
            launch.metadata.process_parameters == kLinkedLoadAddress + 0x3a0 &&
            launch.metadata.tls.has_value() &&
            launch.metadata.tls->image_address == kLinkedLoadAddress + 0x380 &&
            launch.metadata.tls->initial_size == 0x10 &&
            launch.metadata.tls->memory_size == 0x20 &&
            launch.metadata.init_function == kLinkedLoadAddress &&
            launch.metadata.fini_function == kLinkedLoadAddress + 0x20 &&
            launch.metadata.preinit_array.has_value() &&
            launch.metadata.preinit_array->address ==
                kLinkedLoadAddress + 0x338 &&
            launch.metadata.preinit_array->entry_count == 1 &&
            launch.metadata.init_array.has_value() &&
            launch.metadata.init_array->address ==
                kLinkedLoadAddress + 0x340 &&
            launch.metadata.init_array->entry_count == 1 &&
            launch.metadata.fini_array.has_value() &&
            launch.metadata.fini_array->address ==
                kLinkedLoadAddress + 0x348 &&
            launch.metadata.fini_array->entry_count == 1,
        "synthetic PS5 SELF launch metadata is incorrect");

  const auto lifecycle =
      kajps5::loader::BuildLifecycleCallPlan(launch.metadata, linked_memory);
  Check(lifecycle &&
            lifecycle.plan.preinitializers ==
                std::vector<std::uint64_t>({kLinkedLoadAddress + 0x10}) &&
            lifecycle.plan.initializers ==
                std::vector<std::uint64_t>(
                    {kLinkedLoadAddress, kLinkedLoadAddress + 0x20}) &&
            lifecycle.plan.finalizers ==
                std::vector<std::uint64_t>(
                    {kLinkedLoadAddress + 0x30, kLinkedLoadAddress + 0x20}),
        "synthetic PS5 SELF lifecycle call plan is incorrect");

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
