// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/title_loader.h"

#include <limits>

#include "hle/data_symbols.h"
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

}  // namespace

TitleLoadResult PrepareTitleImage(std::span<const std::byte> image,
                                  std::string_view process_image_name,
                                  std::uint64_t maximum_memory_size) {
  TitleLoadResult result;
  if (image.empty() || process_image_name.empty() ||
      process_image_name.find('\0') != std::string_view::npos ||
      maximum_memory_size == 0) {
    result.status = TitleLoadStatus::kInvalidArgument;
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

  std::uint64_t memory_size = 0;
  if (!Add(range.size, hle::kHleDataPageSize - 1, memory_size) ||
      !Add(memory_size, hle::kHleDataPageSize, memory_size) ||
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

  std::uint64_t load_end = 0;
  std::uint64_t data_page_address = 0;
  if (!Add(guest_memory->base_address(), range.size, load_end) ||
      !AlignUp(load_end, hle::kHleDataPageSize, data_page_address) ||
      !Add(data_page_address, hle::kHleDataPageSize,
           result.stack_search_start) ||
      !guest_memory->Contains(data_page_address, hle::kHleDataPageSize) ||
      !guest_memory->Contains(result.stack_search_start,
                              kTitleRuntimeReserveSize)) {
    result.status = TitleLoadStatus::kMemorySizeOverflow;
    return result;
  }

  result.session = TitleSession::Create(std::move(guest_memory));
  if (!result.session) {
    result.status = TitleLoadStatus::kSessionCreationFailed;
    return result;
  }
  result.hle = result.session->PrepareHle(loaded.metadata, data_page_address,
                                          process_image_name);
  if (!result.hle) {
    result.status = TitleLoadStatus::kHleSetupFailed;
    return result;
  }
  result.coverage =
      hle::AnalyzeImportCoverage(loaded.metadata, result.session->hle_exports(),
                                 &result.session->hle_data());
  if (!result.coverage) {
    result.status = TitleLoadStatus::kImportCoverageFailed;
    return result;
  }
  if (result.coverage.unresolved_unique_import_count != 0) {
    result.status = TitleLoadStatus::kUnresolvedImports;
    return result;
  }

  std::uint64_t tls_module_id = 0;
  std::uint64_t tls_static_offset = 0;
  if (launch.metadata.tls) {
    loader::StaticTlsLayout tls_layout;
    const auto& tls = *launch.metadata.tls;
    result.tls = tls_layout.RegisterModule(1, tls.memory_size, tls.alignment,
                                           tls.image_address);
    if (!result.tls) {
      result.status = TitleLoadStatus::kStaticTlsLayoutFailed;
      return result;
    }
    tls_module_id = result.tls.module.module_id;
    tls_static_offset = result.tls.module.static_offset;
  }

  const auto* functions = result.session->hle_functions();
  if (functions == nullptr) {
    result.status = TitleLoadStatus::kHleSetupFailed;
    return result;
  }
  const loader::LayeredImportResolver imports(*functions,
                                              result.session->hle_data());
  result.relocation = loader::ApplyRelocations(
      loaded.metadata, result.session->memory(), imports, result.load_bias,
      tls_module_id, tls_static_offset);
  if (!result.relocation) {
    result.status = TitleLoadStatus::kRelocationFailed;
    return result;
  }
  result.lifecycle = loader::BuildLifecycleCallPlan(
      launch.metadata, result.session->memory(), result.load_bias);
  if (!result.lifecycle) {
    result.status = TitleLoadStatus::kLifecyclePlanFailed;
    return result;
  }
  if (launch.metadata.tls) {
    result.status = TitleLoadStatus::kStaticTlsExecutionUnsupported;
    return result;
  }
  if (!result.session->Configure(launch.metadata, result.lifecycle.plan)) {
    result.status = TitleLoadStatus::kSessionConfigurationFailed;
    return result;
  }
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
