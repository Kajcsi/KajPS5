// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/title_loader.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "hle/data_symbols.h"
#include "kernel/pthread.h"
#include "loader/layered_import_resolver.h"

namespace kajps5::runtime {
namespace {

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (left > std::numeric_limits<std::uint64_t>::max() - right) {
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

bool AddProgramMemory(const loader::ElfMetadata& metadata,
                      std::uint64_t& memory_size,
                      loader::ElfLoadRangeResult& range) noexcept {
  range = loader::CalculateElfLoadRange(metadata);
  if (!range || range.load_segment_count == 0) {
    return false;
  }
  const auto alignment = RequiredAlignment(metadata);
  return alignment != 0 && (alignment & (alignment - 1)) == 0 &&
         Add(memory_size, alignment - 1, memory_size) &&
         Add(memory_size, range.size, memory_size);
}

}  // namespace

TitleLoadResult PrepareTitleImage(std::span<const std::byte> image,
                                  std::string_view process_image_name,
                                  std::uint64_t maximum_memory_size) {
  return PrepareTitleImageWithModules(image, process_image_name, {},
                                      maximum_memory_size);
}

TitleLoadResult PrepareTitleImageWithModules(
    std::span<const std::byte> image, std::string_view process_image_name,
    loader::AdjacentModuleLoadResult adjacent_modules,
    std::uint64_t maximum_memory_size) {
  TitleLoadResult result;
  if (image.empty() || process_image_name.empty() ||
      process_image_name.find('\0') != std::string_view::npos ||
      maximum_memory_size == 0) {
    result.status = TitleLoadStatus::kInvalidArgument;
    return result;
  }
  result.adjacent_status = adjacent_modules.status;
  if (!adjacent_modules) {
    result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
    return result;
  }
  if (adjacent_modules.modules.size() > loader::kMaximumAdjacentModules) {
    result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
    result.adjacent_status =
        loader::AdjacentModuleLoadStatus::kModuleLimitExceeded;
    return result;
  }
  result.adjacent_module_count = adjacent_modules.modules.size();

  std::uint64_t total_module_bytes = 0;
  std::vector<loader::ModuleDependencyInput> dependency_inputs;
  dependency_inputs.reserve(adjacent_modules.modules.size());
  for (auto& module : adjacent_modules.modules) {
    if (module.guest_path.empty() ||
        module.guest_path.find('\0') != std::string::npos) {
      result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
      result.adjacent_status =
          loader::AdjacentModuleLoadStatus::kInvalidArgument;
      return result;
    }
    if (module.image.empty()) {
      result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
      result.adjacent_status = loader::AdjacentModuleLoadStatus::kEmptyImage;
      return result;
    }
    if (module.image.size() > loader::kMaximumAdjacentModuleSize) {
      result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
      result.adjacent_status = loader::AdjacentModuleLoadStatus::kImageTooLarge;
      return result;
    }
    if (!Add(total_module_bytes, module.image.size(), total_module_bytes) ||
        total_module_bytes > loader::kMaximumAdjacentModuleBytes) {
      result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
      result.adjacent_status =
          loader::AdjacentModuleLoadStatus::kTotalSizeExceeded;
      return result;
    }
    auto module_parsed = loader::ParseExecutable64(module.image);
    if (!module_parsed) {
      result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
      result.adjacent_status = loader::AdjacentModuleLoadStatus::kParseFailed;
      result.elf_error = module_parsed.error;
      return result;
    }
    module.metadata = std::move(module_parsed.metadata);
    dependency_inputs.push_back(
        loader::MakeModuleDependencyInput(module.guest_path, module.metadata));
  }
  adjacent_modules.start_plan = loader::BuildModuleStartPlan(dependency_inputs);
  if (!adjacent_modules.start_plan) {
    result.status = TitleLoadStatus::kAdjacentModuleInputFailed;
    result.adjacent_status = loader::AdjacentModuleLoadStatus::kPlanFailed;
    return result;
  }

  const auto parsed = loader::ParseExecutable64(image);
  if (!parsed) {
    result.status = TitleLoadStatus::kParseFailed;
    result.elf_error = parsed.error;
    return result;
  }
  const auto range = loader::CalculateElfLoadRange(parsed.metadata);
  if (!range) {
    result.status = TitleLoadStatus::kLoadRangeFailed;
    result.elf_error = range.error;
    return result;
  }
  if (range.load_segment_count == 0) {
    result.status = TitleLoadStatus::kNoLoadSegments;
    return result;
  }

  std::uint64_t memory_size = range.size;
  for (const auto& module : adjacent_modules.modules) {
    loader::ElfLoadRangeResult module_range;
    if (!AddProgramMemory(module.metadata, memory_size, module_range)) {
      result.status = TitleLoadStatus::kModuleRuntimeFailed;
      result.modules.status = module_range
                                  ? ModuleRuntimeStatus::kLayoutOverflow
                                  : ModuleRuntimeStatus::kLoadRangeFailed;
      result.modules.elf_error = module_range.error;
      return result;
    }
  }
  if (!Add(memory_size, hle::kHleDataPageSize - 1, memory_size) ||
      !Add(memory_size, hle::kHleDataPageSize, memory_size) ||
      !Add(memory_size, kernel::kPthreadMutexArenaSize, memory_size) ||
      !Add(memory_size, kTitleRuntimeReserveSize, memory_size) ||
      memory_size > std::numeric_limits<std::size_t>::max()) {
    result.status = TitleLoadStatus::kMemorySizeOverflow;
    return result;
  }
  const auto host_granularity = memory::GuestMemory::HostMappingGranularity();
  if (host_granularity == 0 ||
      !AlignUp(memory_size, host_granularity, memory_size)) {
    result.status = TitleLoadStatus::kMemorySizeOverflow;
    return result;
  }
  if (memory_size > maximum_memory_size) {
    result.status = TitleLoadStatus::kMemoryLimitExceeded;
    return result;
  }

  auto guest_memory = memory::GuestMemory::CreateHostMapped(
      static_cast<std::size_t>(memory_size));
  if (!guest_memory) {
    result.status = TitleLoadStatus::kHostMemoryAllocationFailed;
    return result;
  }
  if (guest_memory->base_address() < range.base_address) {
    result.status = TitleLoadStatus::kLoadBiasUnderflow;
    return result;
  }
  result.load_bias = guest_memory->base_address() - range.base_address;

  const auto loaded =
      loader::LoadExecutable64(image, *guest_memory, result.load_bias);
  if (!loaded) {
    result.status = TitleLoadStatus::kExecutableLoadFailed;
    result.elf_error = loaded.error;
    return result;
  }
  const auto launch =
      loader::AnalyzeLaunchMetadata(loaded.metadata, result.load_bias);
  if (!launch) {
    result.status = TitleLoadStatus::kLaunchMetadataFailed;
    result.launch_status = launch.status;
    return result;
  }

  auto session = TitleSession::Create(std::move(guest_memory));
  if (!session) {
    result.status = TitleLoadStatus::kSessionCreationFailed;
    return result;
  }
  session->kernel_runtime().SetProcessParametersAddress(
      launch.metadata.process_parameters.value_or(0));

  auto module_runtime = std::make_unique<ModuleRuntime>(session->memory());
  result.modules = module_runtime->RegisterMain(
      std::string(process_image_name), loaded.metadata, range, result.load_bias,
      launch.metadata);
  if (!result.modules) {
    result.status = TitleLoadStatus::kModuleRuntimeFailed;
    return result;
  }

  std::uint64_t load_end = 0;
  if (!Add(session->memory().base_address(), range.size, load_end)) {
    result.status = TitleLoadStatus::kMemorySizeOverflow;
    return result;
  }
  result.modules = module_runtime->LoadAdjacent(
      std::move(adjacent_modules.modules),
      std::move(adjacent_modules.start_plan), load_end);
  if (!result.modules) {
    result.status = TitleLoadStatus::kModuleRuntimeFailed;
    return result;
  }
  const auto next_load_address = result.modules.next_load_address;

  std::uint64_t data_page_address = 0;
  std::uint64_t pthread_mutex_arena_address = 0;
  if (!AlignUp(next_load_address, hle::kHleDataPageSize, data_page_address) ||
      !Add(data_page_address, hle::kHleDataPageSize,
           pthread_mutex_arena_address) ||
      !Add(pthread_mutex_arena_address, kernel::kPthreadMutexArenaSize,
           result.stack_search_start) ||
      !session->memory().Contains(data_page_address, hle::kHleDataPageSize) ||
      !session->memory().Contains(pthread_mutex_arena_address,
                                  kernel::kPthreadMutexArenaSize) ||
      !session->memory().Contains(result.stack_search_start,
                                  kTitleRuntimeReserveSize)) {
    result.status = TitleLoadStatus::kMemorySizeOverflow;
    return result;
  }

  const auto metadata = module_runtime->MetadataPointers();
  result.hle =
      session->PrepareHleBatch(metadata, data_page_address, process_image_name);
  if (!result.hle) {
    result.status = TitleLoadStatus::kHleSetupFailed;
    return result;
  }
  result.coverage = hle::AnalyzeImportCoverage(
      loaded.metadata, session->hle_exports(), &session->hle_data());
  if (!result.coverage) {
    result.status = TitleLoadStatus::kImportCoverageFailed;
    return result;
  }
  const auto* functions = session->hle_functions();
  if (functions == nullptr) {
    result.status = TitleLoadStatus::kHleSetupFailed;
    return result;
  }
  const loader::LayeredImportResolver imports(*functions, session->hle_data());
  result.modules = module_runtime->RelocateAndPlan(imports);
  result.modules.next_load_address = next_load_address;
  if (!result.modules) {
    result.status =
        result.modules.status == ModuleRuntimeStatus::kUnresolvedImports
            ? TitleLoadStatus::kUnresolvedImports
            : TitleLoadStatus::kModuleRuntimeFailed;
    return result;
  }
  const auto combined_lifecycle = module_runtime->BuildCombinedLifecycle();
  if (!combined_lifecycle) {
    result.status = TitleLoadStatus::kModuleRuntimeFailed;
    result.modules.status = combined_lifecycle.status;
    return result;
  }

  const auto programs = module_runtime->programs();
  if (programs.empty()) {
    result.status = TitleLoadStatus::kModuleRuntimeFailed;
    result.modules.status = ModuleRuntimeStatus::kInvalidState;
    return result;
  }
  result.tls = programs.front().tls;
  result.relocation = programs.front().relocation;
  result.lifecycle = programs.front().lifecycle;

  const auto main_launch = programs.front().launch;
  if (!session->AttachModuleRuntime(std::move(module_runtime)) ||
      !session->Configure(main_launch, combined_lifecycle.plan)) {
    result.status = TitleLoadStatus::kSessionConfigurationFailed;
    return result;
  }
  result.session = std::move(session);
  return result;
}

std::string_view TitleLoadStatusName(TitleLoadStatus status) noexcept {
  switch (status) {
    case TitleLoadStatus::kOk:
      return "ok";
    case TitleLoadStatus::kInvalidArgument:
      return "invalid-argument";
    case TitleLoadStatus::kParseFailed:
      return "parse-failed";
    case TitleLoadStatus::kLoadRangeFailed:
      return "load-range-failed";
    case TitleLoadStatus::kNoLoadSegments:
      return "no-load-segments";
    case TitleLoadStatus::kMemorySizeOverflow:
      return "memory-size-overflow";
    case TitleLoadStatus::kMemoryLimitExceeded:
      return "memory-limit-exceeded";
    case TitleLoadStatus::kHostMemoryAllocationFailed:
      return "host-memory-allocation-failed";
    case TitleLoadStatus::kLoadBiasUnderflow:
      return "load-bias-underflow";
    case TitleLoadStatus::kExecutableLoadFailed:
      return "executable-load-failed";
    case TitleLoadStatus::kLaunchMetadataFailed:
      return "launch-metadata-failed";
    case TitleLoadStatus::kSessionCreationFailed:
      return "session-creation-failed";
    case TitleLoadStatus::kHleSetupFailed:
      return "hle-setup-failed";
    case TitleLoadStatus::kImportCoverageFailed:
      return "import-coverage-failed";
    case TitleLoadStatus::kAdjacentModuleInputFailed:
      return "adjacent-module-input-failed";
    case TitleLoadStatus::kModuleRuntimeFailed:
      return "module-runtime-failed";
    case TitleLoadStatus::kUnresolvedImports:
      return "unresolved-imports";
    case TitleLoadStatus::kStaticTlsLayoutFailed:
      return "static-tls-layout-failed";
    case TitleLoadStatus::kStaticTlsExecutionUnsupported:
      return "static-tls-execution-unsupported";
    case TitleLoadStatus::kRelocationFailed:
      return "relocation-failed";
    case TitleLoadStatus::kLifecyclePlanFailed:
      return "lifecycle-plan-failed";
    case TitleLoadStatus::kSessionConfigurationFailed:
      return "session-configuration-failed";
  }
  return "unknown";
}

}  // namespace kajps5::runtime
