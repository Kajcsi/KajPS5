// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/module_runtime.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "hle/data_symbols.h"
#include "loader/launch_metadata.h"
#include "loader/layered_import_resolver.h"

namespace kajps5::runtime {
namespace {

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

bool AlignUp(std::uint64_t value, std::uint64_t alignment,
             std::uint64_t& result) noexcept {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    return false;
  }
  const auto mask = alignment - 1;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
    return false;
  }
  result = (value + mask) & ~mask;
  return true;
}

std::uint64_t RequiredAlignment(const loader::ElfMetadata& metadata) noexcept {
  auto alignment = hle::kHleDataPageSize;
  for (const auto& header : metadata.program_headers) {
    if (header.type == 1 && header.memory_size != 0) {
      alignment = std::max(alignment, header.alignment);
    }
  }
  return alignment;
}

ModuleRuntimeResult Failure(ModuleRuntimeStatus status, std::size_t index,
                            std::string path) {
  ModuleRuntimeResult result;
  result.status = status;
  result.program_index = index;
  result.failure_path = std::move(path);
  return result;
}

bool AppendCalls(std::vector<std::uint64_t>& destination,
                 std::span<const std::uint64_t> source) {
  if (source.size() > loader::kMaximumLifecycleCalls - destination.size()) {
    return false;
  }
  destination.insert(destination.end(), source.begin(), source.end());
  return true;
}

}  // namespace

ModuleRuntime::ModuleRuntime(memory::GuestMemory& memory) noexcept
    : memory_(memory) {}

ModuleRuntimeResult ModuleRuntime::RegisterMain(
    std::string guest_path, loader::ElfMetadata metadata,
    loader::ElfLoadRangeResult load_range, std::uint64_t load_bias,
    loader::ExecutableLaunchMetadata launch) {
  if (main_registered_ || adjacent_loaded_ || relocated_) {
    return Failure(ModuleRuntimeStatus::kInvalidState, 0,
                   std::move(guest_path));
  }
  if (guest_path.empty() || guest_path.find('\0') != std::string::npos ||
      !load_range || load_range.load_segment_count == 0 ||
      !launch.entry_point) {
    return Failure(ModuleRuntimeStatus::kInvalidArgument, 0,
                   std::move(guest_path));
  }
  const auto checked_range = loader::CalculateElfLoadRange(metadata);
  std::uint64_t load_address = 0;
  if (!checked_range || checked_range.base_address != load_range.base_address ||
      checked_range.size != load_range.size ||
      checked_range.load_segment_count != load_range.load_segment_count ||
      !Add(load_range.base_address, load_bias, load_address) ||
      !memory_.Contains(load_address, load_range.size)) {
    return Failure(ModuleRuntimeStatus::kLoadRangeFailed, 0,
                   std::move(guest_path));
  }

  ModuleRuntimeProgram main;
  main.guest_path = std::move(guest_path);
  main.metadata = std::move(metadata);
  main.load_range = load_range;
  main.load_bias = load_bias;
  main.module_id = 1;
  main.launch = std::move(launch);
  main.is_main = true;
  programs_.push_back(std::move(main));
  main_registered_ = true;
  return {};
}

ModuleRuntimeResult ModuleRuntime::LoadAdjacent(
    std::vector<loader::AdjacentModuleImage> modules,
    loader::ModuleStartPlanResult start_plan,
    std::uint64_t first_load_address) {
  if (!main_registered_ || adjacent_loaded_ || relocated_) {
    return Failure(ModuleRuntimeStatus::kInvalidState, 0, {});
  }
  if (!start_plan || start_plan.start_order.size() > modules.size() ||
      std::any_of(
          start_plan.start_order.begin(), start_plan.start_order.end(),
          [&modules](std::size_t index) { return index >= modules.size(); })) {
    return Failure(ModuleRuntimeStatus::kInvalidArgument, 0, {});
  }
  std::vector<bool> planned(modules.size(), false);
  for (const auto index : start_plan.start_order) {
    if (planned[index]) {
      return Failure(ModuleRuntimeStatus::kInvalidArgument, index + 1,
                     modules[index].guest_path);
    }
    planned[index] = true;
  }

  struct Placement {
    loader::ElfLoadRangeResult range;
    std::uint64_t load_address = 0;
    std::uint64_t load_bias = 0;
  };
  std::vector<Placement> placements;
  placements.reserve(modules.size());
  auto cursor = first_load_address;
  for (std::size_t index = 0; index < modules.size(); ++index) {
    const auto range = loader::CalculateElfLoadRange(modules[index].metadata);
    if (!range || range.load_segment_count == 0) {
      auto result = Failure(ModuleRuntimeStatus::kLoadRangeFailed, index + 1,
                            modules[index].guest_path);
      result.elf_error = range.error;
      return result;
    }
    std::uint64_t load_address = 0;
    const auto alignment = RequiredAlignment(modules[index].metadata);
    if (!AlignUp(cursor, alignment, load_address) ||
        load_address < range.base_address) {
      return Failure(ModuleRuntimeStatus::kLayoutOverflow, index + 1,
                     modules[index].guest_path);
    }
    std::uint64_t next = 0;
    if (!Add(load_address, range.size, next)) {
      return Failure(ModuleRuntimeStatus::kLayoutOverflow, index + 1,
                     modules[index].guest_path);
    }
    if (!memory_.Contains(load_address, range.size)) {
      return Failure(ModuleRuntimeStatus::kMemoryRangeExceeded, index + 1,
                     modules[index].guest_path);
    }
    placements.push_back(
        {range, load_address, load_address - range.base_address});
    cursor = next;
  }

  programs_.reserve(programs_.size() + modules.size());
  for (std::size_t index = 0; index < modules.size(); ++index) {
    auto loaded = loader::LoadExecutable64(modules[index].image, memory_,
                                           placements[index].load_bias);
    if (!loaded) {
      auto result = Failure(ModuleRuntimeStatus::kExecutableLoadFailed,
                            index + 1, modules[index].guest_path);
      result.elf_error = loaded.error;
      return result;
    }
    const auto launch = loader::AnalyzeLaunchMetadata(
        loaded.metadata, placements[index].load_bias);
    if (!launch) {
      auto result = Failure(ModuleRuntimeStatus::kLaunchMetadataFailed,
                            index + 1, modules[index].guest_path);
      result.launch_status = launch.status;
      return result;
    }

    ModuleRuntimeProgram program;
    program.guest_path = std::move(modules[index].guest_path);
    program.metadata = std::move(loaded.metadata);
    program.load_range = placements[index].range;
    program.load_bias = placements[index].load_bias;
    program.module_id = index + 2;
    program.launch = launch.metadata;
    programs_.push_back(std::move(program));
  }
  start_plan_ = std::move(start_plan);
  adjacent_loaded_ = true;
  auto result = RegisterProgramMetadata();
  result.next_load_address = cursor;
  return result;
}

ModuleRuntimeResult ModuleRuntime::RegisterProgramMetadata() {
  for (std::size_t index = 0; index < programs_.size(); ++index) {
    auto& program = programs_[index];
    if (program.launch.tls) {
      const auto& tls = *program.launch.tls;
      program.tls = tls_layout_.RegisterModule(
          program.module_id, tls.memory_size, tls.alignment, tls.image_address);
      if (!program.tls) {
        auto result = Failure(ModuleRuntimeStatus::kTlsLayoutFailed, index,
                              program.guest_path);
        result.tls_status = program.tls.status;
        return result;
      }
    }
    program.exports =
        exports_.RegisterModule(program.metadata, program.load_bias);
    if (!program.exports) {
      auto result = Failure(ModuleRuntimeStatus::kExportRegistrationFailed,
                            index, program.guest_path);
      result.export_status = program.exports.status;
      return result;
    }
  }
  return {};
}

ModuleRuntimeResult ModuleRuntime::RelocateAndPlan(
    const loader::ImportResolver& fallback) {
  if (!main_registered_ || !adjacent_loaded_ || relocated_) {
    return Failure(ModuleRuntimeStatus::kInvalidState, 0, {});
  }

  const loader::LayeredImportResolver imports(exports_, fallback);
  for (std::size_t index = 0; index < programs_.size(); ++index) {
    auto& program = programs_[index];
    const auto tls_module_id = program.module_id;
    const auto tls_static_offset =
        program.tls ? program.tls.module.static_offset : 0;
    program.relocation = loader::ApplyRelocations(
        program.metadata, memory_, imports, program.load_bias, tls_module_id,
        tls_static_offset);
    if (!program.relocation) {
      auto result = Failure(ModuleRuntimeStatus::kRelocationFailed, index,
                            program.guest_path);
      result.relocation_status = program.relocation.status;
      return result;
    }
    program.lifecycle = loader::BuildLifecycleCallPlan(program.launch, memory_,
                                                       program.load_bias);
    if (!program.lifecycle) {
      auto result = Failure(ModuleRuntimeStatus::kLifecyclePlanFailed, index,
                            program.guest_path);
      result.lifecycle_status = program.lifecycle.status;
      return result;
    }
  }
  relocated_ = true;
  ModuleRuntimeResult result;
  bool diagnosed_unresolved_program = false;
  for (std::size_t index = 0; index < programs_.size(); ++index) {
    const auto& program = programs_[index];
    if (program.relocation.unresolved_import_count >
        std::numeric_limits<std::size_t>::max() -
            result.unresolved_import_count) {
      result.unresolved_import_count =
          std::numeric_limits<std::size_t>::max();
    } else {
      result.unresolved_import_count +=
          program.relocation.unresolved_import_count;
    }
    if (program.relocation.unresolved_import_count != 0 &&
        !diagnosed_unresolved_program) {
      // Coverage is per ELF metadata table. Report the first program with an
      // unresolved relocation so adjacent-module diagnostics cannot describe
      // the main program while its unresolved-count is nonzero elsewhere.
      result.program_index = index;
      result.failure_path = program.guest_path;
      result.import_coverage = hle::AnalyzeImportCoverage(program.metadata,
                                                           imports);
      diagnosed_unresolved_program = true;
    }
  }
  return result;
}

CombinedLifecycleResult ModuleRuntime::BuildCombinedLifecycle() const {
  if (!relocated_ || programs_.empty()) {
    CombinedLifecycleResult result;
    result.status = ModuleRuntimeStatus::kInvalidState;
    return result;
  }

  std::vector<loader::LifecyclePlanResult> lifecycles;
  lifecycles.reserve(programs_.size());
  for (const auto& program : programs_) {
    lifecycles.push_back(program.lifecycle);
  }
  return CombineModuleLifecycles(lifecycles, start_plan_.start_order);
}

CombinedLifecycleResult CombineModuleLifecycles(
    std::span<const loader::LifecyclePlanResult> programs,
    std::span<const std::size_t> adjacent_start_order) {
  CombinedLifecycleResult result;
  if (programs.empty() ||
      std::any_of(programs.begin(), programs.end(),
                  [](const auto& program) {
                    return program.status != loader::LifecyclePlanStatus::kOk;
                  }) ||
      std::any_of(adjacent_start_order.begin(), adjacent_start_order.end(),
                  [programs](std::size_t index) {
                    return index >= programs.size() - 1;
                  })) {
    result.status = ModuleRuntimeStatus::kInvalidArgument;
    return result;
  }
  std::vector<bool> planned(programs.size() - 1, false);
  for (const auto index : adjacent_start_order) {
    if (planned[index]) {
      result.status = ModuleRuntimeStatus::kInvalidArgument;
      return result;
    }
    planned[index] = true;
  }

  const auto append_program_start = [&result, programs](std::size_t index) {
    return AppendCalls(result.plan.preinitializers,
                       programs[index].plan.preinitializers) &&
           AppendCalls(result.plan.initializers,
                       programs[index].plan.initializers);
  };
  for (const auto adjacent_index : adjacent_start_order) {
    if (!append_program_start(adjacent_index + 1)) {
      result.status = ModuleRuntimeStatus::kLifecycleLimitExceeded;
      return result;
    }
  }
  if (!append_program_start(0)) {
    result.status = ModuleRuntimeStatus::kLifecycleLimitExceeded;
    return result;
  }
  if (result.plan.preinitializers.size() >
      loader::kMaximumLifecycleCalls - result.plan.initializers.size()) {
    result.status = ModuleRuntimeStatus::kLifecycleLimitExceeded;
    return result;
  }

  if (!AppendCalls(result.plan.finalizers, programs[0].plan.finalizers)) {
    result.status = ModuleRuntimeStatus::kLifecycleLimitExceeded;
    return result;
  }
  for (auto entry = adjacent_start_order.rbegin();
       entry != adjacent_start_order.rend(); ++entry) {
    if (!AppendCalls(result.plan.finalizers,
                     programs[*entry + 1].plan.finalizers)) {
      result.status = ModuleRuntimeStatus::kLifecycleLimitExceeded;
      return result;
    }
  }
  return result;
}

std::span<const ModuleRuntimeProgram> ModuleRuntime::programs() const noexcept {
  return programs_;
}

std::vector<const loader::ElfMetadata*> ModuleRuntime::MetadataPointers()
    const {
  std::vector<const loader::ElfMetadata*> result;
  result.reserve(programs_.size());
  for (const auto& program : programs_) {
    result.push_back(&program.metadata);
  }
  return result;
}

const loader::ModuleExportRegistry& ModuleRuntime::exports() const noexcept {
  return exports_;
}

const loader::StaticTlsLayout& ModuleRuntime::tls_layout() const noexcept {
  return tls_layout_;
}

std::string_view ModuleRuntimeStatusName(ModuleRuntimeStatus status) noexcept {
  switch (status) {
    case ModuleRuntimeStatus::kOk:
      return "ok";
    case ModuleRuntimeStatus::kInvalidState:
      return "invalid-state";
    case ModuleRuntimeStatus::kInvalidArgument:
      return "invalid-argument";
    case ModuleRuntimeStatus::kLoadRangeFailed:
      return "load-range-failed";
    case ModuleRuntimeStatus::kLayoutOverflow:
      return "layout-overflow";
    case ModuleRuntimeStatus::kMemoryRangeExceeded:
      return "memory-range-exceeded";
    case ModuleRuntimeStatus::kExecutableLoadFailed:
      return "executable-load-failed";
    case ModuleRuntimeStatus::kLaunchMetadataFailed:
      return "launch-metadata-failed";
    case ModuleRuntimeStatus::kTlsLayoutFailed:
      return "tls-layout-failed";
    case ModuleRuntimeStatus::kExportRegistrationFailed:
      return "export-registration-failed";
    case ModuleRuntimeStatus::kRelocationFailed:
      return "relocation-failed";
    case ModuleRuntimeStatus::kUnresolvedImports:
      return "unresolved-imports";
    case ModuleRuntimeStatus::kLifecyclePlanFailed:
      return "lifecycle-plan-failed";
    case ModuleRuntimeStatus::kLifecycleLimitExceeded:
      return "lifecycle-limit-exceeded";
  }
  return "unknown";
}

}  // namespace kajps5::runtime
