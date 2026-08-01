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
#include "cpu/native_hle_import_table.h"
#include "cpu/native_hle_trampoline.h"
#include "cpu/native_leaf_executor.h"
#include "hle/export_registry.h"
#include "hle/libc_exports.h"
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
constexpr std::size_t kProgramSize = 128;
constexpr std::uint64_t kProgramAddress = 0x1000;
constexpr std::uint64_t kGotAddress = kProgramAddress + 120;
constexpr std::uint64_t kFirstVectorBits = 0x0102030405060708;
constexpr std::uint64_t kSecondVectorBits = 0x1112131415161718;
constexpr std::uint64_t kVectorReturnBits = 0x2122232425262728;
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

kajps5::hle::HleVectorValue MakeVectorValue(std::uint64_t low_bits) {
  kajps5::hle::HleVectorValue value{};
  for (std::size_t index = 0; index < sizeof(low_bits); ++index) {
    value[index] =
        static_cast<std::byte>((low_bits >> (index * 8U)) & 0xffU);
  }
  return value;
}

std::uint64_t ReadVectorLowBits(
    const kajps5::hle::HleVectorValue& value) {
  std::uint64_t result = 0;
  for (std::size_t index = 0; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(
                  std::to_integer<unsigned char>(value[index]))
              << (index * 8U);
  }
  return result;
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
      std::byte{0x48}, std::byte{0xb8}, std::byte{0x08}, std::byte{0x07},
      std::byte{0x06}, std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
      std::byte{0x02}, std::byte{0x01}, std::byte{0x66}, std::byte{0x48},
      std::byte{0x0f}, std::byte{0x6e}, std::byte{0xc0}, std::byte{0x48},
      std::byte{0xb8}, std::byte{0x18}, std::byte{0x17}, std::byte{0x16},
      std::byte{0x15}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12},
      std::byte{0x11}, std::byte{0x66}, std::byte{0x48}, std::byte{0x0f},
      std::byte{0x6e}, std::byte{0xc8},
      std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, stack_adjustment,
      std::byte{0x6a}, std::byte{80},   std::byte{0x6a}, std::byte{70},
      std::byte{0xff}, std::byte{0x15}, std::byte{0x2c}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x48}, std::byte{0x83},
      std::byte{0xc4}, std::byte{0x18}, std::byte{0x48}, std::byte{0x89},
      std::byte{0xc1}, std::byte{0x66}, std::byte{0x48}, std::byte{0x0f},
      std::byte{0x7e}, std::byte{0xc0}, std::byte{0x48}, std::byte{0x31},
      std::byte{0xc8}, std::byte{0xc3}};
  for (std::size_t index = 0; index < code.size(); ++index) {
    image[kProgramOffset + index] = code[index];
  }
  return image;
}

}  // namespace

int main() {
  using kajps5::cpu::NativeHleImportTable;
  using kajps5::cpu::NativeHleImportTableStatus;
  using kajps5::cpu::NativeHleTrampoline;
  using kajps5::cpu::NativeHleTrampolineStatus;
  using kajps5::cpu::NativeExecutionStatus;
  using kajps5::cpu::NativeLeafExecutor;
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleContextStatus;
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
              const auto first_vector = context.VectorArgument(0);
              const auto second_vector = context.VectorArgument(1);
              Check(first_vector && second_vector &&
                        ReadVectorLowBits(*first_vector) ==
                            kFirstVectorBits &&
                        ReadVectorLowBits(*second_vector) ==
                            kSecondVectorBits,
                    "native trampoline changed a vector argument");
              context.SetReturn(sum);
              Check(context.SetVectorReturn(
                        0, MakeVectorValue(kVectorReturnBits)),
                    "native vector return setup failed");
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "checked HLE handler registration failed");
  auto duplicate_metadata = loaded.metadata;
  duplicate_metadata.dynamic_info.relocations.push_back(
      loaded.metadata.dynamic_info.plt_relocations.front());

  NativeHleImportTable invalid_metadata_imports(memory, exports);
  const std::array<const kajps5::loader::ElfMetadata*, 1> null_metadata = {
      nullptr};
  Check(invalid_metadata_imports.BuildBatch(null_metadata).status ==
                NativeHleImportTableStatus::kInvalidMetadata &&
            invalid_metadata_imports.size() == 0,
        "null batch metadata changed the HLE table");

  NativeHleImportTable capacity_imports(memory, exports);
  const std::vector<const kajps5::loader::ElfMetadata*> too_many_images(
      kajps5::cpu::kMaximumNativeHleImages + 1, &loaded.metadata);
  Check(capacity_imports.BuildBatch(too_many_images).status ==
                NativeHleImportTableStatus::kCapacityExceeded &&
            capacity_imports.size() == 0,
        "oversized HLE image batch changed the table");

  NativeHleImportTable imports(memory, exports);
  const std::array<const kajps5::loader::ElfMetadata*, 2> import_metadata = {
      &duplicate_metadata, &loaded.metadata};
  const auto import_status = imports.BuildBatch(import_metadata, 2);
  if (import_status.status ==
          NativeHleImportTableStatus::kTrampolineBuildFailed &&
      import_status.trampoline_status ==
          NativeHleTrampolineStatus::kUnsupportedHost) {
    std::cout << "native HLE guest execution is unsupported on this host\n";
    return failures == 0 ? 0 : 1;
  }
  const auto* trampoline =
      imports.Find("answer", loaded.metadata.dynamic_info.needed_libraries);
  Check(import_status && import_status.import_count == 2 &&
            import_status.resolved_import_count == 2 &&
            import_status.unresolved_import_count == 0 &&
            import_status.trampoline_count == 1 && imports.size() == 1 &&
            trampoline != nullptr && trampoline->address() != 0,
        "native HLE import table creation failed");
  Check(imports.Build(loaded.metadata).status ==
            NativeHleImportTableStatus::kAlreadyBuilt,
        "native HLE import table accepted a second build");

  auto invalid_symbol_metadata = loaded.metadata;
  invalid_symbol_metadata.dynamic_info.plt_relocations.front().info =
      (99ULL << 32U) | 7U;
  NativeHleImportTable invalid_symbol_imports(memory, exports);
  Check(invalid_symbol_imports.Build(invalid_symbol_metadata).status ==
            NativeHleImportTableStatus::kInvalidSymbolIndex &&
            invalid_symbol_imports.size() == 0,
        "invalid import symbol index changed the HLE table");
  auto empty_symbol_metadata = loaded.metadata;
  empty_symbol_metadata.dynamic_info.symbols[1].name.clear();
  NativeHleImportTable empty_symbol_imports(memory, exports);
  Check(empty_symbol_imports.Build(empty_symbol_metadata).status ==
            NativeHleImportTableStatus::kEmptyImportSymbol &&
            empty_symbol_imports.size() == 0,
        "empty import symbol changed the HLE table");
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
  const auto dispatch = trampoline == nullptr
                            ? kajps5::cpu::NativeHleDispatchSnapshot{}
                            : trampoline->last_dispatch();
  Check(executed &&
            executed.return_value == (360ULL ^ kVectorReturnBits),
        "public guest did not return the checked HLE result");
  Check(dispatch.lookup_status == ExportRegistryStatus::kOk &&
            dispatch.handler_status == HleContextStatus::kOk &&
            dispatch.return_written && dispatch.vector_return_written[0] &&
            !dispatch.vector_return_written[1] && !dispatch.host_exception &&
            dispatch.library == "libkajps5_test",
        "native trampoline did not preserve the HLE dispatch result");

  auto host_memory = GuestMemory::CreateHostMapped(0x10000);
  Check(host_memory != nullptr,
        "host-mapped public HLE memory allocation failed");
  if (host_memory) {
    const auto load_bias = host_memory->base_address() - kProgramAddress;
    const auto host_loaded =
        kajps5::loader::LoadElf64(image, *host_memory, load_bias);
    NativeHleImportTable host_imports(*host_memory, exports);
    const auto host_import_status =
        host_loaded ? host_imports.Build(host_loaded.metadata, 2)
                    : kajps5::cpu::NativeHleImportTableResult{};
    const auto* host_trampoline = host_imports.Find(
        "answer", loaded.metadata.dynamic_info.needed_libraries);
    Check(host_loaded && host_import_status && host_trampoline != nullptr,
          "host-mapped HLE setup failed");
    const auto host_linked =
        host_loaded
            ? kajps5::loader::ApplyRelocations(
                  host_loaded.metadata, *host_memory, host_imports, load_bias)
            : kajps5::loader::RelocationResult{};
    const auto host_executed =
        host_loaded
            ? executor.Execute(
                  *host_memory,
                  host_loaded.metadata.entry_point + load_bias,
                  kProgramSize)
            : kajps5::cpu::NativeExecutionResult{
                  kajps5::cpu::NativeExecutionStatus::kGuestCodeNotExecutable,
                  0};
    const auto host_dispatch =
        host_trampoline == nullptr
            ? kajps5::cpu::NativeHleDispatchSnapshot{}
            : host_trampoline->last_dispatch();
    Check(host_linked && host_linked.resolved_import_count == 1 &&
              host_executed &&
              host_executed.return_value == (360ULL ^ kVectorReturnBits) &&
              host_dispatch.handler_status == HleContextStatus::kOk &&
              host_dispatch.return_written && !host_dispatch.host_exception,
          "host-mapped guest did not call the checked HLE runtime");
  }

  auto copy_memory = GuestMemory::CreateHostMapped(0x10000);
  Check(copy_memory != nullptr, "hot memory-copy allocation failed");
  if (copy_memory) {
    const auto code_address = copy_memory->base_address();
    const auto source_address = code_address + 0x4000;
    const auto destination_address = code_address + 0x8000;
    const auto data_protection = GuestMemoryProtection::kRead |
                                 GuestMemoryProtection::kWrite;
    ExportRegistry copy_exports;
    int fallback_calls = 0;
    Check(copy_memory->Map(code_address, 0x1000,
                           GuestMemoryProtection::kExecute) &&
              copy_memory->Map(source_address, 0x1000, data_protection) &&
              copy_memory->Map(destination_address, 0x1000,
                               data_protection) &&
              copy_exports.Register(
                  kajps5::hle::kLibcName, kajps5::hle::kLibcMemcpyNid,
                  [&fallback_calls](kajps5::hle::HleCallContext& context) {
                    ++fallback_calls;
                    context.SetReturn(context.Argument(0).value_or(0));
                    return HleContextStatus::kMemoryFault;
                  }) == ExportRegistryStatus::kOk,
          "hot memory-copy setup failed");
    NativeHleTrampoline copy_trampoline(
        *copy_memory, copy_exports, kajps5::hle::kLibcMemcpyNid,
        {kajps5::hle::kLibcName});
    std::vector<std::byte> copy_code = {
        std::byte{0x48}, std::byte{0xbf}, std::byte{0},    std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0x48}, std::byte{0xbe},
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{0x48}, std::byte{0xba}, std::byte{0},    std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0x48}, std::byte{0x83},
        std::byte{0xec}, std::byte{0x08}, std::byte{0x48}, std::byte{0xb8},
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
        std::byte{0xff}, std::byte{0xd0}, std::byte{0x48}, std::byte{0x83},
        std::byte{0xc4}, std::byte{0x08}, std::byte{0xc3}};
    Write64(copy_code, 2, destination_address);
    Write64(copy_code, 12, source_address);
    Write64(copy_code, 22, 16);
    Write64(copy_code, 36, copy_trampoline.address());
    const std::array copy_input = {
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
        std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
        std::byte{0x90}, std::byte{0xa0}, std::byte{0xb0}, std::byte{0xc0},
        std::byte{0xd0}, std::byte{0xe0}, std::byte{0xf0}, std::byte{0xff}};
    std::array<std::byte, copy_input.size()> copy_output{};
    const auto copy_result =
        copy_trampoline.status() == NativeHleTrampolineStatus::kOk &&
                copy_memory->Initialize(source_address, copy_input) &&
                copy_memory->Initialize(code_address, copy_code)
            ? executor.Execute(*copy_memory, code_address, copy_code.size())
            : kajps5::cpu::NativeExecutionResult{
                  NativeExecutionStatus::kHostProtectionFailed, 0};
    const auto copy_dispatch = copy_trampoline.last_dispatch();
    Check(copy_result && copy_result.return_value == destination_address &&
              copy_memory->Read(destination_address, copy_output) &&
              copy_output == copy_input && fallback_calls == 0 &&
              copy_dispatch.handler_status == HleContextStatus::kOk &&
              copy_dispatch.return_written &&
              copy_dispatch.library == kajps5::hle::kLibcName,
          "hot memory-copy import did not use the direct checked path");

    Write64(copy_code, 12, copy_memory->end_address() + 0x1000);
    const auto rejected_copy =
        copy_memory->Initialize(code_address, copy_code)
            ? executor.Execute(*copy_memory, code_address, copy_code.size())
            : kajps5::cpu::NativeExecutionResult{
                  NativeExecutionStatus::kHostProtectionFailed, 0};
    const auto rejected_dispatch = copy_trampoline.last_dispatch();
    Check(rejected_copy &&
              rejected_copy.return_value == destination_address &&
              fallback_calls == 1 &&
              rejected_dispatch.handler_status ==
                  HleContextStatus::kMemoryFault,
          "rejected hot memory copy did not use the checked fallback");
  }
  Check(kajps5::cpu::NativeHleTrampolineStatusName(
            NativeHleTrampolineStatus::kHostProtectionFailed) ==
            "host-protection-failed",
        "native trampoline status name is unstable");
  Check(kajps5::cpu::NativeHleImportTableStatusName(
            NativeHleImportTableStatus::kAlreadyBuilt) == "already-built",
        "native HLE import table status name is unstable");
  return failures == 0 ? 0 : 1;
}
