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

#include "core/memory/guest_memory.h"
#include "hle/import_coverage.h"
#include "loader/lifecycle_plan.h"
#include "loader/module_export_registry.h"
#include "loader/module_loader.h"
#include "loader/relocator.h"
#include "loader/static_tls_layout.h"

namespace kajps5::runtime {

enum class ModuleRuntimeStatus {
  kOk,
  kInvalidState,
  kInvalidArgument,
  kLoadRangeFailed,
  kLayoutOverflow,
  kMemoryRangeExceeded,
  kExecutableLoadFailed,
  kLaunchMetadataFailed,
  kTlsLayoutFailed,
  kExportRegistrationFailed,
  kRelocationFailed,
  kUnresolvedImports,
  kLifecyclePlanFailed,
  kLifecycleLimitExceeded,
};

struct ModuleRuntimeProgram {
  std::string guest_path;
  loader::ElfMetadata metadata;
  loader::ElfLoadRangeResult load_range;
  std::uint64_t load_bias = 0;
  std::uint64_t module_id = 0;
  loader::ExecutableLaunchMetadata launch;
  loader::StaticTlsRegistrationResult tls;
  loader::ModuleExportRegistrationResult exports;
  loader::RelocationResult relocation;
  loader::LifecyclePlanResult lifecycle;
  bool is_main = false;
};

struct ModuleRuntimeResult {
  ModuleRuntimeStatus status = ModuleRuntimeStatus::kOk;
  std::size_t program_index = 0;
  std::string failure_path;
  std::uint64_t next_load_address = 0;
  std::size_t unresolved_import_count = 0;
  loader::ElfError elf_error = loader::ElfError::kNone;
  loader::LaunchMetadataStatus launch_status =
      loader::LaunchMetadataStatus::kOk;
  loader::StaticTlsLayoutStatus tls_status = loader::StaticTlsLayoutStatus::kOk;
  loader::ModuleExportRegistryStatus export_status =
      loader::ModuleExportRegistryStatus::kOk;
  loader::RelocationStatus relocation_status = loader::RelocationStatus::kOk;
  hle::ImportCoverageResult import_coverage;
  loader::LifecyclePlanStatus lifecycle_status =
      loader::LifecyclePlanStatus::kOk;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ModuleRuntimeStatus::kOk;
  }
};

struct CombinedLifecycleResult {
  ModuleRuntimeStatus status = ModuleRuntimeStatus::kOk;
  loader::ExecutableLifecyclePlan plan;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ModuleRuntimeStatus::kOk;
  }
};

[[nodiscard]] CombinedLifecycleResult CombineModuleLifecycles(
    std::span<const loader::LifecyclePlanResult> programs,
    std::span<const std::size_t> adjacent_start_order);

class ModuleRuntime final {
 public:
  explicit ModuleRuntime(memory::GuestMemory& memory) noexcept;

  ModuleRuntime(const ModuleRuntime&) = delete;
  ModuleRuntime& operator=(const ModuleRuntime&) = delete;

  [[nodiscard]] ModuleRuntimeResult RegisterMain(
      std::string guest_path, loader::ElfMetadata metadata,
      loader::ElfLoadRangeResult load_range, std::uint64_t load_bias,
      loader::ExecutableLaunchMetadata launch);
  [[nodiscard]] ModuleRuntimeResult LoadAdjacent(
      std::vector<loader::AdjacentModuleImage> modules,
      loader::ModuleStartPlanResult start_plan,
      std::uint64_t first_load_address);
  [[nodiscard]] ModuleRuntimeResult RelocateAndPlan(
      const loader::ImportResolver& fallback);
  [[nodiscard]] CombinedLifecycleResult BuildCombinedLifecycle() const;

  [[nodiscard]] std::span<const ModuleRuntimeProgram> programs() const noexcept;
  [[nodiscard]] std::vector<const loader::ElfMetadata*> MetadataPointers()
      const;
  [[nodiscard]] const loader::ModuleExportRegistry& exports() const noexcept;
  [[nodiscard]] const loader::StaticTlsLayout& tls_layout() const noexcept;

 private:
  [[nodiscard]] ModuleRuntimeResult RegisterProgramMetadata();

  memory::GuestMemory& memory_;
  loader::ModuleExportRegistry exports_;
  loader::StaticTlsLayout tls_layout_;
  std::vector<ModuleRuntimeProgram> programs_;
  loader::ModuleStartPlanResult start_plan_;
  bool main_registered_ = false;
  bool adjacent_loaded_ = false;
  bool relocated_ = false;
};

[[nodiscard]] std::string_view ModuleRuntimeStatusName(
    ModuleRuntimeStatus status) noexcept;

}  // namespace kajps5::runtime
