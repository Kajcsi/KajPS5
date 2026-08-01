// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "hle/import_coverage.h"
#include "loader/elf.h"
#include "loader/launch_metadata.h"
#include "loader/lifecycle_plan.h"
#include "loader/module_loader.h"
#include "loader/relocator.h"
#include "loader/static_tls_layout.h"
#include "runtime/module_runtime.h"
#include "runtime/title_session.h"

namespace kajps5::runtime {

inline constexpr std::uint64_t kTitleRuntimeReserveSize = 16U * 1024U * 1024U;
inline constexpr std::uint64_t kMaximumTitleMemorySize = 512U * 1024U * 1024U;

enum class TitleLoadStatus {
  kOk,
  kInvalidArgument,
  kParseFailed,
  kLoadRangeFailed,
  kNoLoadSegments,
  kMemorySizeOverflow,
  kMemoryLimitExceeded,
  kHostMemoryAllocationFailed,
  kLoadBiasUnderflow,
  kExecutableLoadFailed,
  kLaunchMetadataFailed,
  kSessionCreationFailed,
  kHleSetupFailed,
  kImportCoverageFailed,
  kAdjacentModuleInputFailed,
  kModuleRuntimeFailed,
  kUnresolvedImports,
  kStaticTlsLayoutFailed,
  kStaticTlsExecutionUnsupported,
  kRelocationFailed,
  kLifecyclePlanFailed,
  kSessionConfigurationFailed,
};

struct TitleLoadResult {
  TitleLoadStatus status = TitleLoadStatus::kOk;
  std::unique_ptr<TitleSession> session;
  std::uint64_t load_bias = 0;
  std::uint64_t stack_search_start = 0;
  loader::ElfError elf_error = loader::ElfError::kNone;
  loader::LaunchMetadataStatus launch_status =
      loader::LaunchMetadataStatus::kOk;
  TitleHleSetupResult hle;
  hle::ImportCoverageResult coverage;
  loader::AdjacentModuleLoadStatus adjacent_status =
      loader::AdjacentModuleLoadStatus::kOk;
  std::size_t adjacent_module_count = 0;
  ModuleRuntimeResult modules;
  loader::StaticTlsRegistrationResult tls;
  loader::RelocationResult relocation;
  loader::LifecyclePlanResult lifecycle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == TitleLoadStatus::kOk && session != nullptr;
  }
};

[[nodiscard]] TitleLoadResult PrepareTitleImage(
    std::span<const std::byte> image, std::string_view process_image_name,
    std::uint64_t maximum_memory_size = kMaximumTitleMemorySize);
[[nodiscard]] TitleLoadResult PrepareTitleImageWithModules(
    std::span<const std::byte> image, std::string_view process_image_name,
    loader::AdjacentModuleLoadResult adjacent_modules,
    std::uint64_t maximum_memory_size = kMaximumTitleMemorySize);
[[nodiscard]] std::string_view TitleLoadStatusName(
    TitleLoadStatus status) noexcept;

}  // namespace kajps5::runtime
