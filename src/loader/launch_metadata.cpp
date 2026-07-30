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

  const auto resolve_function =
      [&metadata, load_bias, &result](
          const std::optional<std::uint64_t>& source,
          std::optional<std::uint64_t>& destination,
          LaunchMetadataStatus invalid_status) {
        if (!source.has_value() || *source == 0) {
          return true;
        }
        if (!RangeWithinLoad(metadata, *source, 1, true)) {
          result.status = invalid_status;
          return false;
        }
        std::uint64_t address = 0;
        if (!AddAddress(*source, load_bias, address)) {
          result.status = LaunchMetadataStatus::kAddressOverflow;
          return false;
        }
        destination = address;
        return true;
      };
  if (!resolve_function(metadata.dynamic_info.init_function,
                        result.metadata.init_function,
                        LaunchMetadataStatus::kInitFunctionNotExecutable) ||
      !resolve_function(metadata.dynamic_info.fini_function,
                        result.metadata.fini_function,
                        LaunchMetadataStatus::kFiniFunctionNotExecutable)) {
    return result;
  }

  const auto resolve_array =
      [&metadata, load_bias, &result](
          const std::optional<std::uint64_t>& source_address,
          const std::optional<std::uint64_t>& source_size,
          std::optional<ExecutableFunctionArrayMetadata>& destination,
          LaunchMetadataStatus incomplete_status) {
        const auto address = source_address.value_or(0);
        const auto size = source_size.value_or(0);
        if (address == 0 || size == 0) {
          if (address == 0 && size == 0) {
            return true;
          }
          result.status = incomplete_status;
          return false;
        }
        if (size % sizeof(std::uint64_t) != 0) {
          result.status = LaunchMetadataStatus::kInvalidFunctionArraySize;
          return false;
        }
        if (!RangeWithinLoad(metadata, address, size, false)) {
          result.status = LaunchMetadataStatus::kFunctionArrayNotMapped;
          return false;
        }
        std::uint64_t image_address = 0;
        if (!AddAddress(address, load_bias, image_address)) {
          result.status = LaunchMetadataStatus::kAddressOverflow;
          return false;
        }
        destination = ExecutableFunctionArrayMetadata{
            image_address, size / sizeof(std::uint64_t)};
        return true;
      };
  if (!resolve_array(metadata.dynamic_info.preinit_array_address,
                     metadata.dynamic_info.preinit_array_size,
                     result.metadata.preinit_array,
                     LaunchMetadataStatus::kIncompletePreinitArray) ||
      !resolve_array(metadata.dynamic_info.init_array_address,
                     metadata.dynamic_info.init_array_size,
                     result.metadata.init_array,
                     LaunchMetadataStatus::kIncompleteInitArray) ||
      !resolve_array(metadata.dynamic_info.fini_array_address,
                     metadata.dynamic_info.fini_array_size,
                     result.metadata.fini_array,
                     LaunchMetadataStatus::kIncompleteFiniArray)) {
    return result;
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
    case LaunchMetadataStatus::kInitFunctionNotExecutable:
      return "init-function-not-executable";
    case LaunchMetadataStatus::kFiniFunctionNotExecutable:
      return "fini-function-not-executable";
    case LaunchMetadataStatus::kIncompletePreinitArray:
      return "incomplete-preinit-array";
    case LaunchMetadataStatus::kIncompleteInitArray:
      return "incomplete-init-array";
    case LaunchMetadataStatus::kIncompleteFiniArray:
      return "incomplete-fini-array";
    case LaunchMetadataStatus::kInvalidFunctionArraySize:
      return "invalid-function-array-size";
    case LaunchMetadataStatus::kFunctionArrayNotMapped:
      return "function-array-not-mapped";
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
        << '\n'
        << "launch.has_init_function="
        << (result.metadata.init_function.has_value() ? 1 : 0) << '\n'
        << "launch.init_function=";
  WriteHex64(trace, result.metadata.init_function.value_or(0));
  trace << '\n'
        << "launch.has_fini_function="
        << (result.metadata.fini_function.has_value() ? 1 : 0) << '\n'
        << "launch.fini_function=";
  WriteHex64(trace, result.metadata.fini_function.value_or(0));
  const auto write_array = [&trace](
                               std::string_view name,
                               const std::optional<
                                   ExecutableFunctionArrayMetadata>& array) {
    trace << '\n'
          << "launch.has_" << name << "=" << (array.has_value() ? 1 : 0)
          << '\n'
          << "launch." << name << "_address=";
    WriteHex64(trace, array.has_value() ? array->address : 0);
    trace << '\n'
          << "launch." << name << "_count="
          << (array.has_value() ? array->entry_count : 0);
  };
  write_array("preinit_array", result.metadata.preinit_array);
  write_array("init_array", result.metadata.init_array);
  write_array("fini_array", result.metadata.fini_array);
  trace << '\n';
  return trace.str();
}

}  // namespace kajps5::loader
