// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/module_plan.h"

#include "loader/elf.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kajps5::loader {
namespace {

std::string BaseName(std::string_view value) {
  const auto separator = value.find_last_of("/\\");
  return std::string(separator == std::string_view::npos
                         ? value
                         : value.substr(separator + 1));
}

std::string NormalizeName(std::string_view value) {
  auto name = BaseName(value);
  std::transform(name.begin(), name.end(), name.begin(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A')
                                                        : byte);
  });
  return name;
}

bool AddIdentity(std::unordered_map<std::string, std::size_t>& identities,
                 std::string_view name, std::size_t index) {
  auto normalized = NormalizeName(name);
  if (normalized.empty()) {
    return false;
  }
  const auto [found, inserted] = identities.emplace(std::move(normalized), index);
  return inserted || found->second == index;
}

}  // namespace

ModuleStartPlanResult BuildModuleStartPlan(
    const std::vector<ModuleDependencyInput>& modules) {
  ModuleStartPlanResult result;
  if (modules.size() > kMaximumPlannedModules) {
    result.status = ModulePlanStatus::kLimitExceeded;
    return result;
  }

  std::size_t dependency_count = 0;
  std::unordered_map<std::string, std::size_t> identities;
  identities.reserve(modules.size() * 2);
  for (std::size_t index = 0; index < modules.size(); ++index) {
    const auto& module = modules[index];
    if (NormalizeName(module.file_name).empty()) {
      result.status = ModulePlanStatus::kInvalidModuleName;
      return result;
    }
    if (!AddIdentity(identities, module.file_name, index) ||
        (module.shared_object_name.has_value() &&
         !module.shared_object_name->empty() &&
         !AddIdentity(identities, *module.shared_object_name, index))) {
      result.status = ModulePlanStatus::kDuplicateModuleIdentity;
      return result;
    }
    if (module.needed_libraries.size() >
        kMaximumPlannedDependencies - dependency_count) {
      result.status = ModulePlanStatus::kLimitExceeded;
      return result;
    }
    dependency_count += module.needed_libraries.size();
  }

  std::vector<std::set<std::size_t>> dependencies(modules.size());
  for (std::size_t index = 0; index < modules.size(); ++index) {
    std::unordered_set<std::string> missing_names;
    for (const auto& dependency_name : modules[index].needed_libraries) {
      const auto normalized = NormalizeName(dependency_name);
      if (normalized.empty()) {
        continue;
      }
      const auto found = identities.find(normalized);
      if (found == identities.end()) {
        if (missing_names.insert(normalized).second) {
          result.missing_dependencies.push_back({index, dependency_name});
        }
        continue;
      }
      if (found->second != index && modules[found->second].has_initializer) {
        dependencies[index].insert(found->second);
      }
    }
  }

  std::vector<bool> started(modules.size(), false);
  std::size_t remaining = static_cast<std::size_t>(std::count_if(
      modules.begin(), modules.end(),
      [](const ModuleDependencyInput& module) {
        return module.has_initializer;
      }));
  result.start_order.reserve(remaining);

  while (remaining != 0) {
    bool progressed = false;
    for (std::size_t index = 0; index < modules.size(); ++index) {
      if (!modules[index].has_initializer || started[index]) {
        continue;
      }
      const auto ready = std::all_of(
          dependencies[index].begin(), dependencies[index].end(),
          [&started](std::size_t dependency) { return started[dependency]; });
      if (!ready) {
        continue;
      }
      started[index] = true;
      result.start_order.push_back(index);
      --remaining;
      progressed = true;
    }
    if (!progressed) {
      break;
    }
  }

  for (std::size_t index = 0; index < modules.size(); ++index) {
    if (modules[index].has_initializer && !started[index]) {
      result.start_order.push_back(index);
      ++result.fallback_start_count;
    }
  }
  return result;
}

ModuleDependencyInput MakeModuleDependencyInput(std::string file_name,
                                                const ElfMetadata& metadata) {
  ModuleDependencyInput input;
  input.file_name = std::move(file_name);
  input.shared_object_name = metadata.dynamic_info.shared_object_name;
  input.needed_libraries = metadata.dynamic_info.needed_libraries;
  const auto has_array = [](const std::optional<std::uint64_t>& address,
                            const std::optional<std::uint64_t>& size) {
    return address.value_or(0) != 0 && size.value_or(0) != 0;
  };
  input.has_initializer =
      metadata.dynamic_info.init_function.value_or(0) != 0 ||
      has_array(metadata.dynamic_info.preinit_array_address,
                metadata.dynamic_info.preinit_array_size) ||
      has_array(metadata.dynamic_info.init_array_address,
                metadata.dynamic_info.init_array_size);
  return input;
}

std::string_view ModulePlanStatusName(ModulePlanStatus status) noexcept {
  switch (status) {
    case ModulePlanStatus::kOk: return "ok";
    case ModulePlanStatus::kInvalidModuleName: return "invalid-module-name";
    case ModulePlanStatus::kDuplicateModuleIdentity:
      return "duplicate-module-identity";
    case ModulePlanStatus::kLimitExceeded: return "limit-exceeded";
  }
  return "unknown";
}

}  // namespace kajps5::loader
