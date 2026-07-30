// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <string>
#include <vector>

#include "loader/elf.h"
#include "loader/module_plan.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "module_plan_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ModuleDependencyInput Module(
    std::string file_name, std::vector<std::string> dependencies = {},
    bool has_initializer = true) {
  kajps5::loader::ModuleDependencyInput module;
  module.file_name = std::move(file_name);
  module.needed_libraries = std::move(dependencies);
  module.has_initializer = has_initializer;
  return module;
}

}  // namespace

int main() {
  using kajps5::loader::BuildModuleStartPlan;
  using kajps5::loader::ModulePlanStatus;

  auto root = Module("root.bin", {"libA.prx"}, false);
  auto library_c = Module("SCE_MODULE/LIBC.PRX", {"libB.prx"});
  library_c.shared_object_name = "libC.sprx";
  auto library_a = Module("libA.prx", {"LIBC.SPRX", "missing.prx",
                                        "missing.prx"});
  auto library_b = Module("libB.prx");
  const auto ordered = BuildModuleStartPlan(
      {root, library_c, library_a, library_b});
  Check(ordered && ordered.start_order == std::vector<std::size_t>({3, 1, 2}),
        "dependency order is incorrect");
  Check(ordered.missing_dependencies.size() == 1 &&
            ordered.missing_dependencies[0].requester_index == 2 &&
            ordered.missing_dependencies[0].name == "missing.prx" &&
            ordered.fallback_start_count == 0,
        "missing dependency diagnostics are incorrect");

  const auto cycle = BuildModuleStartPlan(
      {Module("a.prx", {"b.prx"}), Module("b.prx", {"a.prx"}),
       Module("c.prx")});
  Check(cycle && cycle.start_order == std::vector<std::size_t>({2, 0, 1}) &&
            cycle.fallback_start_count == 2,
        "cycle fallback did not preserve input order");

  auto first_alias = Module("first.prx");
  first_alias.shared_object_name = "shared.prx";
  auto second_alias = Module("SHARED.PRX");
  Check(BuildModuleStartPlan({first_alias, second_alias}).status ==
            ModulePlanStatus::kDuplicateModuleIdentity,
        "duplicate module identity was accepted");

  Check(BuildModuleStartPlan({Module("")}).status ==
            ModulePlanStatus::kInvalidModuleName,
        "empty module name was accepted");

  kajps5::loader::ElfMetadata parsed_metadata;
  parsed_metadata.dynamic_info.shared_object_name = "parsed.prx";
  parsed_metadata.dynamic_info.needed_libraries = {"dependency.prx"};
  parsed_metadata.dynamic_info.init_array_address = 0x1000;
  parsed_metadata.dynamic_info.init_array_size = 8;
  const auto parsed_input = kajps5::loader::MakeModuleDependencyInput(
      "path/parsed.bin", parsed_metadata);
  Check(parsed_input.file_name == "path/parsed.bin" &&
            parsed_input.shared_object_name == "parsed.prx" &&
            parsed_input.needed_libraries ==
                std::vector<std::string>({"dependency.prx"}) &&
            parsed_input.has_initializer,
        "parsed ELF metadata did not produce a module dependency input");

  parsed_metadata.dynamic_info.init_array_address.reset();
  const auto incomplete_input = kajps5::loader::MakeModuleDependencyInput(
      "path/incomplete.bin", parsed_metadata);
  Check(!incomplete_input.has_initializer,
        "incomplete initializer metadata marked a module for startup");

  std::vector<kajps5::loader::ModuleDependencyInput> too_many(
      kajps5::loader::kMaximumPlannedModules + 1);
  Check(BuildModuleStartPlan(too_many).status ==
            ModulePlanStatus::kLimitExceeded,
        "module limit was not enforced");

  Check(kajps5::loader::ModulePlanStatusName(
            ModulePlanStatus::kDuplicateModuleIdentity) ==
            "duplicate-module-identity",
        "module plan status name is unstable");
  return failures == 0 ? 0 : 1;
}
