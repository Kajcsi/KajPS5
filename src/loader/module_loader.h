// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kernel/file.h"
#include "loader/elf.h"
#include "loader/module_plan.h"

namespace kajps5::loader {

inline constexpr std::size_t kMaximumAdjacentModules = 256;
inline constexpr std::uint64_t kMaximumAdjacentModuleSize = 512ULL << 20;
inline constexpr std::uint64_t kMaximumAdjacentModuleBytes = 2ULL << 30;

struct AdjacentModuleLimits {
  std::size_t maximum_modules = kMaximumAdjacentModules;
  std::uint64_t maximum_module_size = kMaximumAdjacentModuleSize;
  std::uint64_t maximum_total_bytes = kMaximumAdjacentModuleBytes;
};

enum class AdjacentModuleLoadStatus {
  kOk,
  kInvalidArgument,
  kModuleLimitExceeded,
  kOpenFailed,
  kStatFailed,
  kEmptyImage,
  kImageTooLarge,
  kTotalSizeExceeded,
  kReadFailed,
  kCloseFailed,
  kParseFailed,
  kPlanFailed,
};

struct AdjacentModuleImage {
  std::string guest_path;
  std::vector<std::byte> image;
  ElfMetadata metadata;
};

struct AdjacentModuleLoadResult {
  AdjacentModuleLoadStatus status = AdjacentModuleLoadStatus::kOk;
  std::vector<AdjacentModuleImage> modules;
  ModuleStartPlanResult start_plan;
  std::string failure_path;
  kernel::KernelStatus kernel_status = kernel::KernelStatus::kOk;
  ElfError elf_error = ElfError::kNone;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == AdjacentModuleLoadStatus::kOk;
  }
};

[[nodiscard]] AdjacentModuleLoadResult
DiscoverAdjacentModules(kernel::FileService &files,
                        AdjacentModuleLimits limits = AdjacentModuleLimits{});
[[nodiscard]] AdjacentModuleLoadResult
DiscoverAdjacentModules(kernel::FileService &files,
                        std::span<const std::string_view> guest_directories,
                        AdjacentModuleLimits limits = AdjacentModuleLimits{});
[[nodiscard]] std::string_view
AdjacentModuleLoadStatusName(AdjacentModuleLoadStatus status) noexcept;

} // namespace kajps5::loader
