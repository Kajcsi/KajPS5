// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

#include "core/memory/guest_memory.h"
#include "loader/elf.h"
#include "loader/relocator.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "relocation_kinds_test: " << message << '\n';
    ++failures;
  }
}

std::uint64_t ReadValue(const kajps5::memory::GuestMemory& memory,
                        std::uint64_t address, std::size_t size) {
  std::array<std::byte, sizeof(std::uint64_t)> bytes{};
  if (!memory.Read(address, std::span(bytes).first(size))) {
    ++failures;
    return 0;
  }

  std::uint64_t value = 0;
  for (std::size_t index = 0; index < size; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

kajps5::loader::ElfMetadata MakeDefinedSymbolMetadata() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.symbols.resize(2);
  auto& symbol = metadata.dynamic_info.symbols[1];
  symbol.section_index = 1;
  symbol.value = 0x800;
  symbol.size = 0x1234;
  return metadata;
}

}  // namespace

int main() {
  using kajps5::loader::ApplyRelativeRelocations;
  using kajps5::loader::RelocationStatus;
  using kajps5::memory::GuestMemory;

  auto metadata = MakeDefinedSymbolMetadata();
  metadata.dynamic_info.relocations = {
      {0x00, (std::uint64_t{1} << 32U) | 2U, -4},
      {0x08, (std::uint64_t{1} << 32U) | 4U, 4},
      {0x10, (std::uint64_t{1} << 32U) | 10U, 3},
      {0x18, 11U, -7},
      {0x20, (std::uint64_t{1} << 32U) | 24U, -8},
      {0x28, (std::uint64_t{1} << 32U) | 32U, 2},
      {0x30, (std::uint64_t{1} << 32U) | 33U, 4},
  };

  GuestMemory memory(0x1000, 0x80);
  Check(memory.InitializeFill(0x1000, 0x80, std::byte{0xaa}),
        "test memory setup failed");
  const auto applied = ApplyRelativeRelocations(metadata, memory, 0x1000);
  Check(applied && applied.applied_count == 7 &&
            applied.unresolved_import_count == 0,
        "supported symbol relocations were not applied");
  Check(ReadValue(memory, 0x1000, 4) == 0x7fc,
        "PC32 did not write S + A - P");
  Check(ReadValue(memory, 0x1008, 4) == 0x7fc,
        "PLT32 did not write S + A - P");
  Check(ReadValue(memory, 0x1010, 4) == 0x1803,
        "unsigned 32-bit relocation wrote the wrong value");
  Check(ReadValue(memory, 0x1018, 4) == 0xfffffff9,
        "signed 32-bit relocation wrote the wrong value");
  Check(ReadValue(memory, 0x1020, 8) == 0x7d8,
        "PC64 did not write S + A - P");
  Check(ReadValue(memory, 0x1028, 4) == 0x1236,
        "SIZE32 did not write Z + A");
  Check(ReadValue(memory, 0x1030, 8) == 0x1238,
        "SIZE64 did not write Z + A");
  Check(ReadValue(memory, 0x1004, 4) == 0xaaaaaaaa &&
            ReadValue(memory, 0x100c, 4) == 0xaaaaaaaa &&
            ReadValue(memory, 0x1014, 4) == 0xaaaaaaaa &&
            ReadValue(memory, 0x101c, 4) == 0xaaaaaaaa &&
            ReadValue(memory, 0x102c, 4) == 0xaaaaaaaa,
        "a 32-bit relocation overwrote adjacent bytes");

  kajps5::loader::ElfMetadata relative64;
  relative64.dynamic_info.relocations.push_back({0x40, 38U, -4});
  const auto relative64_applied =
      ApplyRelativeRelocations(relative64, memory, 0x1000);
  Check(relative64_applied && relative64_applied.applied_count == 1 &&
            ReadValue(memory, 0x1040, 8) == 0xffc,
        "RELATIVE64 did not write B + A");
  relative64.dynamic_info.relocations[0].info =
      (std::uint64_t{1} << 32U) | 38U;
  Check(ApplyRelativeRelocations(relative64, memory, 0x1000).status ==
            RelocationStatus::kInvalidRelativeSymbol,
        "RELATIVE64 accepted a symbol");

  kajps5::loader::ElfMetadata overflow;
  overflow.dynamic_info.relocations = {
      {0x00, 8U, 1},
      {0x08, 10U, 0x100000000LL},
  };
  GuestMemory transactional_memory(0x2000, 16);
  Check(transactional_memory.InitializeFill(0x2000, 16, std::byte{0xaa}),
        "transactional test memory setup failed");
  const auto overflow_result =
      ApplyRelativeRelocations(overflow, transactional_memory, 0x2000);
  std::array<std::byte, 16> unchanged{};
  Check(overflow_result.status == RelocationStatus::kRelocationValueOverflow,
        "unsigned 32-bit overflow was accepted");
  Check(transactional_memory.Read(0x2000, unchanged) &&
            unchanged == std::array{
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa},
                             std::byte{0xaa}, std::byte{0xaa}},
        "overflow changed guest memory");

  auto signed_overflow = MakeDefinedSymbolMetadata();
  signed_overflow.dynamic_info.symbols[1].value = 0x7ffff000;
  signed_overflow.dynamic_info.relocations.push_back(
      {0x1000, (std::uint64_t{1} << 32U) | 11U, 0});
  Check(ApplyRelativeRelocations(signed_overflow, transactional_memory, 0x1000)
            .status == RelocationStatus::kRelocationValueOverflow,
        "signed 32-bit overflow was accepted");

  auto size_overflow = MakeDefinedSymbolMetadata();
  size_overflow.dynamic_info.symbols[1].size = 0x100000000ULL;
  size_overflow.dynamic_info.relocations.push_back(
      {0, (std::uint64_t{1} << 32U) | 32U, 0});
  Check(ApplyRelativeRelocations(size_overflow, transactional_memory, 0x2000)
            .status == RelocationStatus::kRelocationValueOverflow,
        "SIZE32 overflow was accepted");

  kajps5::loader::ElfMetadata narrow_target;
  narrow_target.dynamic_info.relocations.push_back({0, 10U, 1});
  GuestMemory four_byte_memory(0x3000, 4);
  Check(ApplyRelativeRelocations(narrow_target, four_byte_memory, 0x3000) &&
            ReadValue(four_byte_memory, 0x3000, 4) == 1,
        "a valid four-byte relocation target was rejected");
  narrow_target.dynamic_info.relocations[0].info = 1U;
  Check(ApplyRelativeRelocations(narrow_target, four_byte_memory, 0x3000)
            .status == RelocationStatus::kTargetNotMapped,
        "an incomplete eight-byte relocation target was accepted");

  auto invalid_symbol = MakeDefinedSymbolMetadata();
  invalid_symbol.dynamic_info.relocations.push_back(
      {0, (std::uint64_t{2} << 32U) | 2U, 0});
  Check(ApplyRelativeRelocations(invalid_symbol, transactional_memory, 0x2000)
            .status == RelocationStatus::kInvalidSymbolIndex,
        "an invalid PC-relative symbol index was accepted");

  Check(kajps5::loader::RelocationStatusName(
            RelocationStatus::kRelocationValueOverflow) ==
            "relocation-value-overflow",
        "relocation overflow status name is unstable");
  return failures == 0 ? 0 : 1;
}
