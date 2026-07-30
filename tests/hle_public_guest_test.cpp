// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "cpu/native_hle_trampoline.h"
#include "cpu/native_leaf_executor.h"
#include "hle/export_registry.h"
#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/relocator.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kDynamicOffset = 0x100;
constexpr std::size_t kStringOffset = 0x1a0;
constexpr std::size_t kSymbolOffset = 0x1c0;
constexpr std::size_t kHashOffset = 0x1f0;
constexpr std::size_t kRelaOffset = 0x208;
constexpr std::size_t kProgramOffset = 0x300;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::size_t kDynamicEntryCount = 10;
constexpr std::size_t kStringSize = 32;
constexpr std::size_t kSymbolEntrySize = 24;
constexpr std::size_t kRelaEntrySize = 24;
constexpr std::size_t kProgramSize = 64;
constexpr std::uint64_t kProgramAddress = 0x1000;
constexpr std::uint64_t kGotAddress = kProgramAddress + 56;
constexpr std::uint64_t kMetadataAddress = 0x2000;
constexpr std::uint64_t kStringAddress =
    kMetadataAddress + kStringOffset - kDynamicOffset;
constexpr std::uint64_t kSymbolAddress =
    kMetadataAddress + kSymbolOffset - kDynamicOffset;
constexpr std::uint64_t kHashAddress =
    kMetadataAddress + kHashOffset - kDynamicOffset;
constexpr std::uint64_t kRelaAddress =
    kMetadataAddress + kRelaOffset - kDynamicOffset;
constexpr std::uint64_t kMetadataSize =
    kRelaOffset + kRelaEntrySize - kDynamicOffset;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_public_guest_test: " << message << '\n';
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

std::vector<std::byte> MakePublicHleElf() {
  std::vector<std::byte> image(kProgramOffset + kProgramSize);
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
  Write64(image, 24, kProgramAddress);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, 56);
  Write16(image, 56, 3);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 5);
  Write64(image, kProgramHeaderOffset + 8, kProgramOffset);
  Write64(image, kProgramHeaderOffset + 16, kProgramAddress);
  Write64(image, kProgramHeaderOffset + 32, kProgramSize);
  Write64(image, kProgramHeaderOffset + 40, kProgramSize);
  Write64(image, kProgramHeaderOffset + 48, 1);

  const auto metadata_header = kProgramHeaderOffset + 56;
  Write32(image, metadata_header, 1);
  Write32(image, metadata_header + 4, 4);
  Write64(image, metadata_header + 8, kDynamicOffset);
  Write64(image, metadata_header + 16, kMetadataAddress);
  Write64(image, metadata_header + 32, kMetadataSize);
  Write64(image, metadata_header + 40, kMetadataSize);
  Write64(image, metadata_header + 48, 1);

  const auto dynamic_header = kProgramHeaderOffset + 2 * 56;
  Write32(image, dynamic_header, 2);
  Write32(image, dynamic_header + 4, 4);
  Write64(image, dynamic_header + 8, kDynamicOffset);
  Write64(image, dynamic_header + 16, kMetadataAddress);
  Write64(image, dynamic_header + 32,
          kDynamicEntryCount * kDynamicEntrySize);
  Write64(image, dynamic_header + 40,
          kDynamicEntryCount * kDynamicEntrySize);
  Write64(image, dynamic_header + 48, 8);

  WriteDynamic(image, 0, 5, kStringAddress);
  WriteDynamic(image, 1, 10, kStringSize);
  WriteDynamic(image, 2, 1, 1);
  WriteDynamic(image, 3, 4, kHashAddress);
  WriteDynamic(image, 4, 6, kSymbolAddress);
  WriteDynamic(image, 5, 11, kSymbolEntrySize);
  WriteDynamic(image, 6, 23, kRelaAddress);
  WriteDynamic(image, 7, 2, kRelaEntrySize);
  WriteDynamic(image, 8, 20, 7);
  WriteDynamic(image, 9, 0, 0);

  WriteString(image, kStringOffset + 1, "libkajps5_test");
  WriteString(image, kStringOffset + 16, "answer");
  const auto import_symbol = kSymbolOffset + kSymbolEntrySize;
  Write32(image, import_symbol, 16);
  image[import_symbol + 4] = std::byte{0x12};

  Write32(image, kHashOffset, 1);
  Write32(image, kHashOffset + 4, 2);
  Write32(image, kHashOffset + 8, 1);
  Write32(image, kHashOffset + 12, 0);
  Write32(image, kHashOffset + 16, 0);

  Write64(image, kRelaOffset, kGotAddress);
  Write64(image, kRelaOffset + 8, (std::uint64_t{1} << 32U) | 7U);
  Write64(image, kRelaOffset + 16, 0);

  constexpr auto stack_adjustment = std::byte{0x08};
  const std::array code = {
      std::byte{0xbf}, std::byte{10}, std::byte{0}, std::byte{0},
      std::byte{0},    std::byte{0xbe}, std::byte{20}, std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0xba}, std::byte{30},
      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0xb9},
      std::byte{40},   std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{0x41}, std::byte{0xb8}, std::byte{50},   std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0x41}, std::byte{0xb9},
      std::byte{60},   std::byte{0},    std::byte{0},    std::byte{0},
      std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, stack_adjustment,
      std::byte{0x6a}, std::byte{80},   std::byte{0x6a}, std::byte{70},
      std::byte{0xff}, std::byte{0x15}, std::byte{0x0a}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x48}, std::byte{0x83},
      std::byte{0xc4}, std::byte{0x18}, std::byte{0xc3}};
  for (std::size_t index = 0; index < code.size(); ++index) {
    image[kProgramOffset + index] = code[index];
  }
  return image;
}

}  // namespace

int main() {
  using kajps5::cpu::NativeHleTrampoline;
  using kajps5::cpu::NativeHleTrampolineStatus;
  using kajps5::cpu::NativeExecutionStatus;
  using kajps5::cpu::NativeLeafExecutor;
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::ImportRegistry;
  using kajps5::hle::ImportRegistryStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  const auto image = MakePublicHleElf();
  GuestMemory memory(
      kProgramAddress,
      static_cast<std::size_t>(kMetadataAddress + kMetadataSize -
                               kProgramAddress),
      GuestMemoryProtection::kNone);
  const auto loaded = kajps5::loader::LoadElf64(image, memory);
  Check(static_cast<bool>(loaded), "public HLE ELF did not load");
  Check(loaded.metadata.dynamic_info.needed_libraries.size() == 1 &&
            loaded.metadata.dynamic_info.symbols.size() == 2 &&
            loaded.metadata.dynamic_info.plt_relocations.size() == 1,
        "public HLE ELF metadata is incomplete");

  ExportRegistry exports;
  Check(exports.Register(
            "libkajps5_test", "answer",
            [](kajps5::hle::HleCallContext& context) {
              const std::array expected = {10ULL, 20ULL, 30ULL, 40ULL,
                                           50ULL, 60ULL, 70ULL, 80ULL};
              std::uint64_t sum = 0;
              for (std::size_t index = 0; index < expected.size(); ++index) {
                const auto argument = context.Argument(index).value_or(0);
                Check(argument == expected[index],
                      "native trampoline changed a guest argument");
                sum += argument;
              }
              context.SetReturn(sum);
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "checked HLE handler registration failed");
  NativeHleTrampoline trampoline(memory, exports, "answer",
                                 {"libkajps5_test"}, 2);
  if (trampoline.status() == NativeHleTrampolineStatus::kUnsupportedHost) {
    std::cout << "native HLE guest execution is unsupported on this host\n";
    return failures == 0 ? 0 : 1;
  }
  Check(trampoline.status() == NativeHleTrampolineStatus::kOk &&
            trampoline.address() != 0,
        "native HLE trampoline creation failed");
  NativeHleTrampoline missing_trampoline(
      memory, exports, "missing", {"libkajps5_test"});
  Check(missing_trampoline.status() ==
                NativeHleTrampolineStatus::kInvalidArgument &&
            missing_trampoline.address() == 0,
        "missing HLE export produced an executable trampoline");
  NativeHleTrampoline oversized_stack_trampoline(
      memory, exports, "answer", {"libkajps5_test"},
      kajps5::hle::kMaximumCapturedHleStackArguments + 1);
  Check(oversized_stack_trampoline.status() ==
                NativeHleTrampolineStatus::kInvalidArgument &&
            oversized_stack_trampoline.address() == 0,
        "oversized stack capture produced an executable trampoline");

  ImportRegistry imports;
  Check(imports.Register("libkajps5_test", "answer", trampoline.address()) ==
            ImportRegistryStatus::kOk,
        "native HLE trampoline registration failed");
  const auto linked =
      kajps5::loader::ApplyRelocations(loaded.metadata, memory, imports);
  Check(linked && linked.applied_count == 1 &&
            linked.resolved_import_count == 1 &&
            linked.unresolved_import_count == 0,
        "public HLE import did not link");

  NativeLeafExecutor executor;
  const auto executed =
      executor.Execute(memory, loaded.metadata.entry_point, kProgramSize);
  if (executed.status == NativeExecutionStatus::kUnsupportedHost) {
    std::cout << "public HLE guest execution is unsupported on this host\n";
    return failures == 0 ? 0 : 1;
  }
  const auto dispatch = trampoline.last_dispatch();
  Check(executed && executed.return_value == 360,
        "public guest did not return the checked HLE result");
  Check(dispatch.lookup_status == ExportRegistryStatus::kOk &&
            dispatch.handler_status == HleContextStatus::kOk &&
            dispatch.return_written && !dispatch.host_exception &&
            dispatch.library == "libkajps5_test",
        "native trampoline did not preserve the HLE dispatch result");
  Check(kajps5::cpu::NativeHleTrampolineStatusName(
            NativeHleTrampolineStatus::kHostProtectionFailed) ==
            "host-protection-failed",
        "native trampoline status name is unstable");
  return failures == 0 ? 0 : 1;
}
