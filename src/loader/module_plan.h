// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kajps5::loader {

inline constexpr std::size_t kMaximumPlannedModules = 4096;
inline constexpr std::size_t kMaximumPlannedDependencies = 65536;

enum class ModulePlanStatus {
  kOk,
  kInvalidModuleName,
  kDuplicateModuleIdentity,
  kLimitExceeded,
};

struct ModuleDependencyInput {
  std::string file_name;
  std::optional<std::string> shared_object_name;
  std::vector<std::string> needed_libraries;
  bool has_initializer = true;
};

struct MissingModuleDependency {
  std::size_t requester_index = 0;
  std::string name;
};

struct ModuleStartPlanResult {
  ModulePlanStatus status = ModulePlanStatus::kOk;
  std::vector<std::size_t> start_order;
  std::vector<MissingModuleDependency> missing_dependencies;
  std::size_t fallback_start_count = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ModulePlanStatus::kOk;
  }
};

[[nodiscard]] ModuleStartPlanResult BuildModuleStartPlan(
    const std::vector<ModuleDependencyInput>& modules);
[[nodiscard]] std::string_view ModulePlanStatusName(
    ModulePlanStatus status) noexcept;

}  // namespace kajps5::loader
