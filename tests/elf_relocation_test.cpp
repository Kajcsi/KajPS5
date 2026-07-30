// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "loader/elf.h"
#include "loader/elf_trace.h"
#include "loader/relocator.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kDynamicOffset = 0x100;
constexpr std::size_t kRelaOffset = 0x180;
constexpr std::uint64_t kRelaAddress = 0x3000;
constexpr std::uint64_t kTargetAddress = 0x4000;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::size_t kRelaEntrySize = 24;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "elf_relocation_test: " << message << '\n';
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

std::vector<std::byte> MakeRelaElf() {
  std::vector<std::byte> image(kRelaOffset + kRelaEntrySize);
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
  Write16(image, 56, 3);

  Write32(image, kProgramHeaderOffset, 2);
  Write32(image, kProgramHeaderOffset + 4, 4);
  Write64(image, kProgramHeaderOffset + 8, kDynamicOffset);
  Write64(image, kProgramHeaderOffset + 16, 0x2000);
  Write64(image, kProgramHeaderOffset + 32, 4 * kDynamicEntrySize);
  Write64(image, kProgramHeaderOffset + 40, 4 * kDynamicEntrySize);
  Write64(image, kProgramHeaderOffset + 48, 8);

  const auto rela_header = kProgramHeaderOffset + 56;
  Write32(image, rela_header, 1);
  Write32(image, rela_header + 4, 4);
  Write64(image, rela_header + 8, kRelaOffset);
  Write64(image, rela_header + 16, kRelaAddress);
  Write64(image, rela_header + 32, kRelaEntrySize);
  Write64(image, rela_header + 40, kRelaEntrySize);
  Write64(image, rela_header + 48, 8);

  const auto target_header = kProgramHeaderOffset + 2 * 56;
  Write32(image, target_header, 1);
  Write32(image, target_header + 4, 6);
  Write64(image, target_header + 16, kTargetAddress);
  Write64(image, target_header + 40, 8);
  Write64(image, target_header + 48, 1);

  WriteDynamic(image, 0, 7, kRelaAddress);
  WriteDynamic(image, 1, 8, kRelaEntrySize);
  WriteDynamic(image, 2, 9, kRelaEntrySize);
  WriteDynamic(image, 3, 0, 0);

  Write64(image, kRelaOffset, kTargetAddress);
  Write64(image, kRelaOffset + 8, 8);
  Write64(image, kRelaOffset + 16,
          static_cast<std::uint64_t>(std::int64_t{-4}));
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

  const auto image = MakeRelaElf();
  const auto parsed = ParseElf64(image);
  Check(static_cast<bool>(parsed), "valid RELA table was rejected");
  Check(parsed.metadata.dynamic_info.relocations.size() == 1,
        "RELA entry count is incorrect");
  const auto& relocation = parsed.metadata.dynamic_info.relocations[0];
  Check(relocation.offset == kTargetAddress && relocation.symbol() == 0 &&
            relocation.type() == 8 && relocation.addend == -4,
        "RELA entry fields are incorrect");
  Check(kajps5::loader::FormatElfTrace(parsed.metadata)
                .find("elf.relocations=1\n"
                      "elf.plt_relocations=0\n"
                      "elf.symbols=0\n"
                      "elf.undefined_symbols=0\n") !=
            std::string::npos,
        "RELA counts are missing from the stable trace");

  kajps5::memory::GuestMemory memory(
      kRelaAddress, static_cast<std::size_t>(kTargetAddress - kRelaAddress + 8),
      kajps5::memory::GuestMemoryProtection::kNone);
  const auto loaded = kajps5::loader::LoadElf64(image, memory);
  Check(static_cast<bool>(loaded), "RELA ELF did not load");
  const auto applied =
      kajps5::loader::ApplyRelativeRelocations(loaded.metadata, memory);
  Check(applied && applied.applied_count == 1 &&
            applied.unresolved_import_count == 0,
        "relative relocation was not applied");
  std::array<std::byte, 8> relocated{};
  Check(memory.Read(kTargetAddress, relocated),
        "relocated value could not be read");
  const std::array expected_relocated = {
      std::byte{0xfc}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
      std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  Check(relocated == expected_relocated,
        "relative relocation wrote the wrong value");

  auto invalid_symbol = loaded.metadata;
  invalid_symbol.dynamic_info.relocations[0].info =
      (std::uint64_t{1} << 32U) | 8U;
  Check(kajps5::loader::ApplyRelativeRelocations(invalid_symbol, memory)
            .status == kajps5::loader::RelocationStatus::kInvalidRelativeSymbol,
        "relative relocation accepted a symbol");

  auto unresolved_import = loaded.metadata;
  unresolved_import.dynamic_info.relocations.push_back(
      {kTargetAddress, (std::uint64_t{1} << 32U) | 6U, 0});
  const auto unresolved =
      kajps5::loader::ApplyRelativeRelocations(unresolved_import, memory);
  Check(unresolved && unresolved.applied_count == 1 &&
            unresolved.unresolved_import_count == 1,
        "unresolved import was not preserved");

  kajps5::memory::GuestMemory transactional_memory(
      kRelaAddress, static_cast<std::size_t>(kTargetAddress - kRelaAddress + 8),
      kajps5::memory::GuestMemoryProtection::kNone);
  const auto transactional_load =
      kajps5::loader::LoadElf64(image, transactional_memory);
  Check(static_cast<bool>(transactional_load),
        "transactional relocation ELF did not load");
  auto unsupported = transactional_load.metadata;
  unsupported.dynamic_info.relocations.push_back({kTargetAddress, 2, 0});
  const auto unsupported_result = kajps5::loader::ApplyRelativeRelocations(
      unsupported, transactional_memory);
  Check(unsupported_result.status ==
                kajps5::loader::RelocationStatus::kUnsupportedRelocation &&
            unsupported_result.unsupported_relocation_type == 2,
        "unsupported relocation was accepted");
  std::array<std::byte, 8> unchanged{};
  Check(transactional_memory.Read(kTargetAddress, unchanged) &&
            unchanged == std::array<std::byte, 8>{},
        "failed relocation pass changed guest memory");

  kajps5::loader::ElfMetadata tls_metadata;
  tls_metadata.dynamic_info.relocations.push_back({kTargetAddress, 16, 0});
  Check(kajps5::loader::ApplyRelativeRelocations(
            tls_metadata, transactional_memory)
            .status == kajps5::loader::RelocationStatus::kMissingTlsModuleId,
        "TLS module relocation accepted a missing module ID");
  const auto tls_relocated = kajps5::loader::ApplyRelativeRelocations(
      tls_metadata, transactional_memory, 0, 7);
  std::array<std::byte, 8> tls_value{};
  const std::array expected_tls_value = {
      std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  Check(tls_relocated && tls_relocated.applied_count == 1 &&
            transactional_memory.Read(kTargetAddress, tls_value) &&
            tls_value == expected_tls_value,
        "TLS module relocation wrote the wrong module ID");
  Check(kajps5::loader::ApplyRelativeRelocations(
            loaded.metadata, memory,
            std::numeric_limits<std::uint64_t>::max())
            .status ==
            kajps5::loader::RelocationStatus::kTargetAddressOverflow,
        "relocation target overflow was accepted");

  auto incomplete = image;
  WriteDynamic(incomplete, 2, 0x1234, kRelaEntrySize);
  CheckError(std::move(incomplete), ElfError::kIncompleteRelaMetadata,
             "incomplete RELA metadata returned the wrong error");

  auto invalid_size = image;
  WriteDynamic(invalid_size, 2, 9, 16);
  CheckError(std::move(invalid_size), ElfError::kInvalidRelaEntrySize,
             "invalid RELA entry size returned the wrong error");

  auto unmapped_table = image;
  WriteDynamic(unmapped_table, 0, 7, 0x3500);
  CheckError(std::move(unmapped_table), ElfError::kRelaTableNotFileBacked,
             "unmapped RELA table returned the wrong error");

  auto invalid_target = image;
  Write64(invalid_target, kRelaOffset, 0x5000);
  CheckError(std::move(invalid_target), ElfError::kRelocationTargetOutOfRange,
             "invalid relocation target returned the wrong error");

  auto no_operation = image;
  Write64(no_operation, kRelaOffset, 0);
  Write64(no_operation, kRelaOffset + 8, 0);
  Check(static_cast<bool>(ParseElf64(no_operation)),
        "no-operation relocation with no target was rejected");

  auto plt = image;
  WriteDynamic(plt, 0, 23, kRelaAddress);
  WriteDynamic(plt, 1, 2, kRelaEntrySize);
  WriteDynamic(plt, 2, 20, 7);
  const auto parsed_plt = ParseElf64(plt);
  Check(parsed_plt && parsed_plt.metadata.dynamic_info.relocations.empty() &&
            parsed_plt.metadata.dynamic_info.plt_relocations.size() == 1,
        "valid PLT RELA table was rejected");

  WriteDynamic(plt, 2, 20, 17);
  CheckError(std::move(plt), ElfError::kUnsupportedPltRelocationFormat,
             "unsupported PLT format returned the wrong error");

  Check(kajps5::loader::ElfErrorName(ElfError::kInvalidRelaEntrySize) ==
            "invalid-rela-entry-size",
        "RELA error name is unstable");
  Check(kajps5::loader::RelocationStatusName(
            kajps5::loader::RelocationStatus::kTargetNotMapped) ==
            "target-not-mapped",
        "relocation status name is unstable");
  return failures == 0 ? 0 : 1;
}
