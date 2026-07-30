// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "loader/elf.h"

namespace kajps5::loader {

enum class LaunchMetadataStatus {
  kOk,
  kAddressOverflow,
  kEntryPointNotMapped,
  kEntryPointNotExecutable,
  kMultipleProcessParameterSegments,
  kProcessParametersNotMapped,
  kMultipleTlsSegments,
  kTlsNotMapped,
};

struct ExecutableTlsMetadata {
  std::uint64_t image_address = 0;
  std::uint64_t initial_size = 0;
  std::uint64_t memory_size = 0;
  std::uint64_t alignment = 0;
};

struct ExecutableLaunchMetadata {
  std::optional<std::uint64_t> entry_point;
  std::optional<std::uint64_t> process_parameters;
  std::optional<ExecutableTlsMetadata> tls;
};

struct LaunchMetadataResult {
  LaunchMetadataStatus status = LaunchMetadataStatus::kOk;
  ExecutableLaunchMetadata metadata;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == LaunchMetadataStatus::kOk;
  }
};

[[nodiscard]] LaunchMetadataResult AnalyzeLaunchMetadata(
    const ElfMetadata& metadata, std::uint64_t load_bias = 0) noexcept;
[[nodiscard]] std::string_view LaunchMetadataStatusName(
    LaunchMetadataStatus status) noexcept;
[[nodiscard]] std::string FormatLaunchMetadataTrace(
    const LaunchMetadataResult& result);

}  // namespace kajps5::loader
