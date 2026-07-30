// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/launch_metadata.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace kajps5::loader {
namespace {

constexpr std::uint32_t kProgramTypeLoad = 1;
constexpr std::uint32_t kProgramTypeTls = 7;
constexpr std::uint32_t kProgramTypeSceProcessParameters = 0x61000001;
constexpr std::uint32_t kProgramFlagExecute = 1;

bool RangeWithinLoad(const ElfMetadata& metadata, std::uint64_t address,
                     std::uint64_t size, bool require_execute) noexcept {
  for (const auto& header : metadata.program_headers) {
    if (header.type != kProgramTypeLoad || address < header.virtual_address) {
      continue;
    }
    const auto relative = address - header.virtual_address;
    if (relative > header.memory_size ||
        size > header.memory_size - relative) {
      continue;
    }
    if (!require_execute || (header.flags & kProgramFlagExecute) != 0) {
      return true;
    }
  }
  return false;
}

bool AddAddress(std::uint64_t address, std::uint64_t load_bias,
                std::uint64_t& result) noexcept {
  if (address > std::numeric_limits<std::uint64_t>::max() - load_bias) {
    return false;
  }
  result = address + load_bias;
  return true;
}

void WriteHex64(std::ostringstream& trace, std::uint64_t value) {
  trace << "0x" << std::hex << std::setfill('0') << std::setw(16) << value
        << std::dec;
}

}  // namespace

LaunchMetadataResult AnalyzeLaunchMetadata(const ElfMetadata& metadata,
                                           std::uint64_t load_bias) noexcept {
  LaunchMetadataResult result;

  if (metadata.entry_point != 0) {
    if (!RangeWithinLoad(metadata, metadata.entry_point, 1, false)) {
      result.status = LaunchMetadataStatus::kEntryPointNotMapped;
      return result;
    }
    if (!RangeWithinLoad(metadata, metadata.entry_point, 1, true)) {
      result.status = LaunchMetadataStatus::kEntryPointNotExecutable;
      return result;
    }
    std::uint64_t entry_point = 0;
    if (!AddAddress(metadata.entry_point, load_bias, entry_point)) {
      result.status = LaunchMetadataStatus::kAddressOverflow;
      return result;
    }
    result.metadata.entry_point = entry_point;
  }

  const ElfProgramHeader* process_parameters = nullptr;
  const ElfProgramHeader* tls = nullptr;
  for (const auto& header : metadata.program_headers) {
    if (header.type == kProgramTypeSceProcessParameters) {
      if (process_parameters != nullptr) {
        result.status =
            LaunchMetadataStatus::kMultipleProcessParameterSegments;
        return result;
      }
      process_parameters = &header;
    }
    if (header.type == kProgramTypeTls) {
      if (tls != nullptr) {
        result.status = LaunchMetadataStatus::kMultipleTlsSegments;
        return result;
      }
      tls = &header;
    }
  }

  if (process_parameters != nullptr) {
    const auto checked_size = std::max<std::uint64_t>(
        process_parameters->memory_size, 1);
    if (!RangeWithinLoad(metadata, process_parameters->virtual_address,
                         checked_size, false)) {
      result.status = LaunchMetadataStatus::kProcessParametersNotMapped;
      return result;
    }
    std::uint64_t address = 0;
    if (!AddAddress(process_parameters->virtual_address, load_bias, address)) {
      result.status = LaunchMetadataStatus::kAddressOverflow;
      return result;
    }
    result.metadata.process_parameters = address;
  }

  if (tls != nullptr && tls->memory_size != 0) {
    const auto initial_size = std::min(tls->file_size, tls->memory_size);
    if (initial_size != 0 &&
        !RangeWithinLoad(metadata, tls->virtual_address, initial_size, false)) {
      result.status = LaunchMetadataStatus::kTlsNotMapped;
      return result;
    }
    std::uint64_t image_address = 0;
    if (!AddAddress(tls->virtual_address, load_bias, image_address)) {
      result.status = LaunchMetadataStatus::kAddressOverflow;
      return result;
    }
    result.metadata.tls = ExecutableTlsMetadata{
        image_address, initial_size, tls->memory_size, tls->alignment};
  }

  return result;
}

std::string_view LaunchMetadataStatusName(
    LaunchMetadataStatus status) noexcept {
  switch (status) {
    case LaunchMetadataStatus::kOk: return "ok";
    case LaunchMetadataStatus::kAddressOverflow: return "address-overflow";
    case LaunchMetadataStatus::kEntryPointNotMapped:
      return "entry-point-not-mapped";
    case LaunchMetadataStatus::kEntryPointNotExecutable:
      return "entry-point-not-executable";
    case LaunchMetadataStatus::kMultipleProcessParameterSegments:
      return "multiple-process-parameter-segments";
    case LaunchMetadataStatus::kProcessParametersNotMapped:
      return "process-parameters-not-mapped";
    case LaunchMetadataStatus::kMultipleTlsSegments:
      return "multiple-tls-segments";
    case LaunchMetadataStatus::kTlsNotMapped: return "tls-not-mapped";
  }
  return "unknown";
}

std::string FormatLaunchMetadataTrace(const LaunchMetadataResult& result) {
  std::ostringstream trace;
  trace.imbue(std::locale::classic());
  trace << "launch.status=" << LaunchMetadataStatusName(result.status) << '\n'
        << "launch.has_entry="
        << (result.metadata.entry_point.has_value() ? 1 : 0) << '\n'
        << "launch.entry=";
  WriteHex64(trace, result.metadata.entry_point.value_or(0));
  trace << '\n'
        << "launch.has_process_parameters="
        << (result.metadata.process_parameters.has_value() ? 1 : 0) << '\n'
        << "launch.process_parameters=";
  WriteHex64(trace, result.metadata.process_parameters.value_or(0));
  trace << '\n'
        << "launch.has_tls=" << (result.metadata.tls.has_value() ? 1 : 0)
        << '\n'
        << "launch.tls_address=";
  WriteHex64(trace, result.metadata.tls.has_value()
                        ? result.metadata.tls->image_address
                        : 0);
  trace << '\n'
        << "launch.tls_initial_size="
        << (result.metadata.tls.has_value()
                ? result.metadata.tls->initial_size
                : 0)
        << '\n'
        << "launch.tls_memory_size="
        << (result.metadata.tls.has_value() ? result.metadata.tls->memory_size
                                            : 0)
        << '\n'
        << "launch.tls_alignment="
        << (result.metadata.tls.has_value() ? result.metadata.tls->alignment
                                            : 0)
        << '\n';
  return trace.str();
}

}  // namespace kajps5::loader
