// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "loader/launch_metadata.h"

namespace {

constexpr std::uint32_t kProgramTypeLoad = 1;
constexpr std::uint32_t kProgramTypeTls = 7;
constexpr std::uint32_t kProgramTypeSceProcessParameters = 0x61000001;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "launch_metadata_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ElfProgramHeader MakeHeader(
    std::uint32_t type, std::uint32_t flags, std::uint64_t address,
    std::uint64_t file_size, std::uint64_t memory_size,
    std::uint64_t alignment = 1) {
  kajps5::loader::ElfProgramHeader header;
  header.type = type;
  header.flags = flags;
  header.virtual_address = address;
  header.file_size = file_size;
  header.memory_size = memory_size;
  header.alignment = alignment;
  return header;
}

kajps5::loader::ElfMetadata MakeLaunchMetadata() {
  kajps5::loader::ElfMetadata metadata;
  metadata.entry_point = 0x1010;
  metadata.program_headers.push_back(
      MakeHeader(kProgramTypeLoad, 5, 0x1000, 0x500, 0x500, 0x100));
  metadata.program_headers.push_back(MakeHeader(
      kProgramTypeSceProcessParameters, 4, 0x1100, 0x40, 0x40, 8));
  metadata.program_headers.push_back(
      MakeHeader(kProgramTypeTls, 4, 0x1200, 0x20, 0x40, 0x10));
  metadata.dynamic_info.init_function = 0x1020;
  metadata.dynamic_info.fini_function = 0x1030;
  metadata.dynamic_info.preinit_array_address = 0x1300;
  metadata.dynamic_info.preinit_array_size = 8;
  metadata.dynamic_info.init_array_address = 0x1310;
  metadata.dynamic_info.init_array_size = 16;
  metadata.dynamic_info.fini_array_address = 0x1320;
  metadata.dynamic_info.fini_array_size = 8;
  return metadata;
}

}  // namespace

int main() {
  using kajps5::loader::AnalyzeLaunchMetadata;
  using kajps5::loader::FormatLaunchMetadataTrace;
  using kajps5::loader::LaunchMetadataStatus;

  const auto valid = AnalyzeLaunchMetadata(MakeLaunchMetadata(), 0x10000);
  Check(valid && valid.metadata.entry_point == 0x11010 &&
            valid.metadata.process_parameters == 0x11100 &&
            valid.metadata.tls.has_value() &&
            valid.metadata.tls->image_address == 0x11200 &&
            valid.metadata.tls->initial_size == 0x20 &&
            valid.metadata.tls->memory_size == 0x40 &&
            valid.metadata.tls->alignment == 0x10 &&
            valid.metadata.init_function == 0x11020 &&
            valid.metadata.fini_function == 0x11030 &&
            valid.metadata.preinit_array.has_value() &&
            valid.metadata.preinit_array->address == 0x11300 &&
            valid.metadata.preinit_array->entry_count == 1 &&
            valid.metadata.init_array.has_value() &&
            valid.metadata.init_array->entry_count == 2 &&
            valid.metadata.fini_array.has_value() &&
            valid.metadata.fini_array->entry_count == 1,
        "valid launch metadata was resolved incorrectly");

  const std::string expected_trace =
      "launch.status=ok\n"
      "launch.has_entry=1\n"
      "launch.entry=0x0000000000011010\n"
      "launch.has_process_parameters=1\n"
      "launch.process_parameters=0x0000000000011100\n"
      "launch.has_tls=1\n"
      "launch.tls_address=0x0000000000011200\n"
      "launch.tls_initial_size=32\n"
      "launch.tls_memory_size=64\n"
      "launch.tls_alignment=16\n"
      "launch.has_init_function=1\n"
      "launch.init_function=0x0000000000011020\n"
      "launch.has_fini_function=1\n"
      "launch.fini_function=0x0000000000011030\n"
      "launch.has_preinit_array=1\n"
      "launch.preinit_array_address=0x0000000000011300\n"
      "launch.preinit_array_count=1\n"
      "launch.has_init_array=1\n"
      "launch.init_array_address=0x0000000000011310\n"
      "launch.init_array_count=2\n"
      "launch.has_fini_array=1\n"
      "launch.fini_array_address=0x0000000000011320\n"
      "launch.fini_array_count=1\n";
  Check(FormatLaunchMetadataTrace(valid) == expected_trace,
        "stable launch metadata trace changed");

  auto unmapped_entry = MakeLaunchMetadata();
  unmapped_entry.entry_point = 0x2000;
  Check(AnalyzeLaunchMetadata(unmapped_entry).status ==
            LaunchMetadataStatus::kEntryPointNotMapped,
        "unmapped entry point was accepted");

  auto non_executable_entry = MakeLaunchMetadata();
  non_executable_entry.program_headers[0].flags = 4;
  Check(AnalyzeLaunchMetadata(non_executable_entry).status ==
            LaunchMetadataStatus::kEntryPointNotExecutable,
        "non-executable entry point was accepted");

  auto overflowing_entry = MakeLaunchMetadata();
  overflowing_entry.entry_point = std::numeric_limits<std::uint64_t>::max();
  overflowing_entry.program_headers[0] = MakeHeader(
      kProgramTypeLoad, 5, std::numeric_limits<std::uint64_t>::max(), 1, 1);
  Check(AnalyzeLaunchMetadata(overflowing_entry, 1).status ==
            LaunchMetadataStatus::kAddressOverflow,
        "overflowing biased entry point was accepted");

  auto multiple_process_parameters = MakeLaunchMetadata();
  multiple_process_parameters.program_headers.push_back(
      multiple_process_parameters.program_headers[1]);
  Check(AnalyzeLaunchMetadata(multiple_process_parameters).status ==
            LaunchMetadataStatus::kMultipleProcessParameterSegments,
        "multiple process-parameter segments were accepted");

  auto unmapped_process_parameters = MakeLaunchMetadata();
  unmapped_process_parameters.program_headers[1].virtual_address = 0x2000;
  Check(AnalyzeLaunchMetadata(unmapped_process_parameters).status ==
            LaunchMetadataStatus::kProcessParametersNotMapped,
        "unmapped process parameters were accepted");

  auto multiple_tls = MakeLaunchMetadata();
  multiple_tls.program_headers.push_back(multiple_tls.program_headers[2]);
  Check(AnalyzeLaunchMetadata(multiple_tls).status ==
            LaunchMetadataStatus::kMultipleTlsSegments,
        "multiple TLS segments were accepted");

  auto unmapped_tls = MakeLaunchMetadata();
  unmapped_tls.program_headers[2].virtual_address = 0x2000;
  Check(AnalyzeLaunchMetadata(unmapped_tls).status ==
            LaunchMetadataStatus::kTlsNotMapped,
        "unmapped TLS template was accepted");

  auto zero_fill_tls = MakeLaunchMetadata();
  zero_fill_tls.program_headers[2] =
      MakeHeader(kProgramTypeTls, 4, 0x14f0, 0x10, 0x100, 0x10);
  const auto zero_fill = AnalyzeLaunchMetadata(zero_fill_tls);
  Check(zero_fill && zero_fill.metadata.tls.has_value() &&
            zero_fill.metadata.tls->initial_size == 0x10 &&
            zero_fill.metadata.tls->memory_size == 0x100,
        "TLS zero-fill area was incorrectly required in a load segment");

  auto invalid_init = MakeLaunchMetadata();
  invalid_init.dynamic_info.init_function = 0x2000;
  Check(AnalyzeLaunchMetadata(invalid_init).status ==
            LaunchMetadataStatus::kInitFunctionNotExecutable,
        "invalid init function was accepted");

  auto invalid_fini = MakeLaunchMetadata();
  invalid_fini.dynamic_info.fini_function = 0x2000;
  Check(AnalyzeLaunchMetadata(invalid_fini).status ==
            LaunchMetadataStatus::kFiniFunctionNotExecutable,
        "invalid fini function was accepted");

  auto incomplete_preinit = MakeLaunchMetadata();
  incomplete_preinit.dynamic_info.preinit_array_size.reset();
  Check(AnalyzeLaunchMetadata(incomplete_preinit).status ==
            LaunchMetadataStatus::kIncompletePreinitArray,
        "incomplete preinit array was accepted");

  auto incomplete_init = MakeLaunchMetadata();
  incomplete_init.dynamic_info.init_array_address.reset();
  Check(AnalyzeLaunchMetadata(incomplete_init).status ==
            LaunchMetadataStatus::kIncompleteInitArray,
        "incomplete init array was accepted");

  auto incomplete_fini = MakeLaunchMetadata();
  incomplete_fini.dynamic_info.fini_array_size.reset();
  Check(AnalyzeLaunchMetadata(incomplete_fini).status ==
            LaunchMetadataStatus::kIncompleteFiniArray,
        "incomplete fini array was accepted");

  auto invalid_array_size = MakeLaunchMetadata();
  invalid_array_size.dynamic_info.init_array_size = 7;
  Check(AnalyzeLaunchMetadata(invalid_array_size).status ==
            LaunchMetadataStatus::kInvalidFunctionArraySize,
        "invalid function-array size was accepted");

  auto unmapped_array = MakeLaunchMetadata();
  unmapped_array.dynamic_info.init_array_address = 0x2000;
  Check(AnalyzeLaunchMetadata(unmapped_array).status ==
            LaunchMetadataStatus::kFunctionArrayNotMapped,
        "unmapped function array was accepted");

  auto zero_arrays = MakeLaunchMetadata();
  zero_arrays.dynamic_info.preinit_array_address.reset();
  zero_arrays.dynamic_info.preinit_array_size = 0;
  zero_arrays.dynamic_info.init_array_address = 0;
  zero_arrays.dynamic_info.init_array_size.reset();
  const auto empty_arrays = AnalyzeLaunchMetadata(zero_arrays);
  Check(empty_arrays && !empty_arrays.metadata.preinit_array.has_value() &&
            !empty_arrays.metadata.init_array.has_value(),
        "zero-valued function-array tags were rejected");

  auto optional_fields = MakeLaunchMetadata();
  optional_fields.entry_point = 0;
  optional_fields.program_headers.erase(
      optional_fields.program_headers.begin() + 1,
      optional_fields.program_headers.end());
  optional_fields.dynamic_info = {};
  const auto optional = AnalyzeLaunchMetadata(optional_fields);
  Check(optional && !optional.metadata.entry_point.has_value() &&
            !optional.metadata.process_parameters.has_value() &&
            !optional.metadata.tls.has_value(),
        "optional launch metadata was not kept optional");

  Check(kajps5::loader::LaunchMetadataStatusName(
            LaunchMetadataStatus::kTlsNotMapped) == "tls-not-mapped",
        "launch metadata status name is unstable");
  return failures == 0 ? 0 : 1;
}
