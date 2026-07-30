// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "core/project_info.h"
#include "hle/data_symbols.h"
#include "hle/export_registry.h"
#include "hle/import_coverage.h"
#include "hle/import_registry.h"
#include "hle/kernel_exports.h"
#include "hle/libc_exports.h"
#include "kernel/runtime.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"
#include "loader/launch_metadata.h"
#include "loader/lifecycle_plan.h"
#include "loader/relocation_trace.h"
#include "loader/relocator.h"

namespace {

constexpr std::uint64_t kMaximumExecutableFileSize = 512U * 1024U * 1024U;
constexpr std::uint64_t kMaximumTraceMemorySize = 512U * 1024U * 1024U;

std::optional<std::uint64_t> AlignUp(std::uint64_t value,
                                     std::uint64_t alignment) noexcept {
  const auto mask = alignment - 1;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
    return std::nullopt;
  }
  return (value + mask) & ~mask;
}

std::optional<std::vector<std::byte>> ReadExecutableFile(const char* path,
                                                         std::string& error) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "cannot open input file";
    return std::nullopt;
  }

  const auto end = static_cast<std::streamoff>(file.tellg());
  if (end < 0) {
    error = "cannot determine input size";
    return std::nullopt;
  }
  if (static_cast<std::uint64_t>(end) > kMaximumExecutableFileSize) {
    error = "input exceeds the 512 MiB inspection limit";
    return std::nullopt;
  }

  std::vector<std::byte> image(static_cast<std::size_t>(end));
  file.seekg(0, std::ios::beg);
  if (!image.empty() &&
      !file.read(reinterpret_cast<char*>(image.data()),
                 static_cast<std::streamsize>(image.size()))) {
    error = "cannot read the complete input file";
    return std::nullopt;
  }
  return image;
}

int TraceExecutableFile(const char* path) {
  std::string file_error;
  auto image = ReadExecutableFile(path, file_error);
  if (!image) {
    std::cerr << "Executable inspection failed: " << file_error << '\n';
    return 2;
  }

  const auto parsed = kajps5::loader::ParseExecutable64(*image);
  if (!parsed) {
    std::cerr << "Executable inspection failed: "
              << kajps5::loader::ElfErrorName(parsed.error) << '\n';
    return 2;
  }
  std::cout << kajps5::loader::FormatElfTrace(parsed.metadata);

  const auto range = kajps5::loader::CalculateElfLoadRange(parsed.metadata);
  if (!range) {
    std::cerr << "Executable load check failed: "
              << kajps5::loader::ElfErrorName(range.error) << '\n';
    return 3;
  }
  if (range.load_segment_count == 0) {
    std::cout << "load.result=skipped-no-load-segments\n";
    return 0;
  }
  if (range.size > kMaximumTraceMemorySize) {
    std::cerr << "Executable load check failed: guest range exceeds 512 MiB\n";
    return 3;
  }

  const auto load_end = range.base_address + range.size;
  const auto hle_data_address =
      AlignUp(load_end, kajps5::hle::kHleDataPageSize);
  if (!hle_data_address ||
      *hle_data_address > std::numeric_limits<std::uint64_t>::max() -
                              kajps5::hle::kHleDataPageSize) {
    std::cerr << "Executable load check failed: HLE data range overflows\n";
    return 3;
  }
  const auto trace_memory_size =
      *hle_data_address + kajps5::hle::kHleDataPageSize - range.base_address;
  if (trace_memory_size > std::numeric_limits<std::size_t>::max()) {
    std::cerr << "Executable load check failed: guest range is too large\n";
    return 3;
  }

  try {
    kajps5::memory::GuestMemory memory(
        range.base_address, static_cast<std::size_t>(trace_memory_size),
        kajps5::memory::GuestMemoryProtection::kNone);
    const auto loaded = kajps5::loader::LoadExecutable64(*image, memory);
    if (!loaded) {
      std::cerr << "Executable load check failed: "
                << kajps5::loader::ElfErrorName(loaded.error) << '\n';
      return 3;
    }

    std::cout << "load.result=ok\n"
              << "load.guest_base=" << range.base_address << '\n'
              << "load.guest_size=" << range.size << '\n'
              << "load.segments=" << loaded.loaded_segment_count << '\n'
              << "load.file_bytes=" << loaded.loaded_file_bytes << '\n'
              << "load.zero_filled_bytes=" << loaded.zero_filled_bytes
              << '\n';

    const auto launch =
        kajps5::loader::AnalyzeLaunchMetadata(loaded.metadata);
    std::cout << kajps5::loader::FormatLaunchMetadataTrace(launch);
    if (!launch) {
      std::cerr << "Executable launch metadata check failed: "
                << kajps5::loader::LaunchMetadataStatusName(launch.status)
                << '\n';
      return 5;
    }

    kajps5::hle::ImportRegistry hle_data;
    const auto data_status = kajps5::hle::InstallHleDataSymbols(
        hle_data, memory, *hle_data_address,
        std::filesystem::path(path).filename().string());
    std::cout << "hle.data.status="
              << kajps5::hle::HleDataStatusName(data_status.status) << '\n'
              << "hle.data.symbols=" << hle_data.size() << '\n';
    if (!data_status) {
      std::cerr << "HLE data setup failed: "
                << kajps5::hle::HleDataStatusName(data_status.status) << '\n';
      return 7;
    }

    kajps5::kernel::KernelRuntime kernel_runtime;
    kajps5::hle::ExportRegistry hle_exports;
    const auto export_status = kajps5::hle::RegisterKernelExports(
        hle_exports, kernel_runtime);
    if (export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: export registration returned "
                << kajps5::hle::ExportRegistryStatusName(export_status)
                << '\n';
      return 7;
    }
    const auto libc_export_status = kajps5::hle::RegisterLibcExports(
        hle_exports, kernel_runtime.cxa_guards());
    if (libc_export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: libc export registration "
                   "returned "
                << kajps5::hle::ExportRegistryStatusName(libc_export_status)
                << '\n';
      return 7;
    }
    const auto coverage = kajps5::hle::AnalyzeImportCoverage(
        loaded.metadata, hle_exports);
    std::cout << kajps5::hle::FormatImportCoverageTrace(coverage);
    if (!coverage) {
      std::cerr << "HLE coverage check failed: "
                << kajps5::hle::ImportCoverageStatusName(coverage.status)
                << '\n';
      return 7;
    }

    const auto tls_module_id = launch.metadata.tls.has_value() ? 1U : 0U;
    const auto relocated = kajps5::loader::ApplyRelocations(
        loaded.metadata, memory, hle_data, 0, tls_module_id);
    std::cout << kajps5::loader::FormatRelocationTrace(relocated);
    if (!relocated) {
      std::cerr << "Executable relocation check failed: "
                << kajps5::loader::RelocationStatusName(relocated.status)
                << '\n';
      return 4;
    }

    const auto lifecycle =
        kajps5::loader::BuildLifecycleCallPlan(launch.metadata, memory);
    std::cout << kajps5::loader::FormatLifecyclePlanTrace(lifecycle);
    if (!lifecycle) {
      std::cerr << "Executable lifecycle check failed: "
                << kajps5::loader::LifecyclePlanStatusName(lifecycle.status)
                << '\n';
      return 6;
    }
  } catch (const std::exception& exception) {
    std::cerr << "Executable load check failed: " << exception.what() << '\n';
    return 3;
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << kajps5::ProjectName() << ' ' << kajps5::ProjectVersion()
              << '\n';
    return 0;
  }
  if (argc == 3 && std::string_view(argv[1]) == "--trace-elf") {
    return TraceExecutableFile(argv[2]);
  }

  if (argc > 1) {
    std::cerr << "Usage: kajps5 [--version | --trace-elf <path>]\n";
    return 1;
  }

  std::cout << kajps5::ProjectName() << ' ' << kajps5::ProjectVersion()
            << '\n'
            << kajps5::ProjectStatus() << '\n';
  return 0;
}
