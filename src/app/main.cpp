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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "core/project_info.h"
#include "cpu/native_guest_executor.h"
#include "cpu/native_hle_import_table.h"
#include "hle/data_symbols.h"
#include "hle/export_registry.h"
#include "hle/import_coverage.h"
#include "hle/import_registry.h"
#include "hle/json_exports.h"
#include "hle/kernel_exports.h"
#include "hle/libc_exports.h"
#include "hle/libc_thread_exports.h"
#include "kernel/runtime.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"
#include "loader/launch_metadata.h"
#include "loader/layered_import_resolver.h"
#include "loader/lifecycle_plan.h"
#include "loader/module_loader.h"
#include "loader/relocation_trace.h"
#include "loader/relocator.h"
#include "loader/static_tls_layout.h"
#include "runtime/title_loader.h"

namespace {

constexpr std::uint64_t kMaximumExecutableFileSize = 512U * 1024U * 1024U;
constexpr std::uint64_t kMaximumTraceMemorySize = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumTitleRunSlices = 1000000;
constexpr std::size_t kMaximumModuleOverlayDirectories = 16;

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
              << "load.zero_filled_bytes=" << loaded.zero_filled_bytes << '\n';

    const auto launch = kajps5::loader::AnalyzeLaunchMetadata(loaded.metadata);
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
    const auto export_status =
        kajps5::hle::RegisterKernelExports(hle_exports, kernel_runtime);
    if (export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: export registration returned "
                << kajps5::hle::ExportRegistryStatusName(export_status) << '\n';
      return 7;
    }
    const auto libc_export_status = kajps5::hle::RegisterLibcExports(
        hle_exports, kernel_runtime.cxa_guards(),
        kernel_runtime.process_lifecycle(), kernel_runtime.libc_heap(), memory);
    if (libc_export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: libc export registration "
                   "returned "
                << kajps5::hle::ExportRegistryStatusName(libc_export_status)
                << '\n';
      return 7;
    }
    const auto libc_thread_export_status =
        kajps5::hle::RegisterLibcThreadExports(hle_exports,
                                               kernel_runtime.pthreads());
    if (libc_thread_export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: libc thread export "
                   "registration returned "
                << kajps5::hle::ExportRegistryStatusName(
                       libc_thread_export_status)
                << '\n';
      return 7;
    }
    const auto json_export_status = kajps5::hle::RegisterJsonExports(
        hle_exports, kernel_runtime.json_values());
    if (json_export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: JSON export registration "
                   "returned "
                << kajps5::hle::ExportRegistryStatusName(json_export_status)
                << '\n';
      return 7;
    }
    const auto coverage = kajps5::hle::AnalyzeImportCoverage(
        loaded.metadata, hle_exports, &hle_data);
    std::cout << kajps5::hle::FormatImportCoverageTrace(coverage);
    if (!coverage) {
      std::cerr << "HLE coverage check failed: "
                << kajps5::hle::ImportCoverageStatusName(coverage.status)
                << '\n';
      return 7;
    }

    kajps5::cpu::NativeGuestExecutionContext execution_context;
    kajps5::cpu::NativeHleImportTable hle_functions(memory, hle_exports,
                                                    &execution_context);
    const auto function_status = hle_functions.Build(
        loaded.metadata, kajps5::hle::kMaximumCapturedHleStackArguments);
    std::cout << "hle.trampolines.status="
              << kajps5::cpu::NativeHleImportTableStatusName(
                     function_status.status)
              << '\n'
              << "hle.trampolines.imports=" << function_status.import_count
              << '\n'
              << "hle.trampolines.resolved_imports="
              << function_status.resolved_import_count << '\n'
              << "hle.trampolines.unresolved_imports="
              << function_status.unresolved_import_count << '\n'
              << "hle.trampolines.created=" << function_status.trampoline_count
              << '\n';
    if (!function_status) {
      std::cerr << "HLE trampoline setup failed: "
                << kajps5::cpu::NativeHleImportTableStatusName(
                       function_status.status)
                << '\n';
      return 7;
    }

    kajps5::loader::StaticTlsLayout tls_layout;
    std::uint64_t tls_module_id = 0;
    std::uint64_t tls_static_offset = 0;
    if (launch.metadata.tls) {
      const auto& tls = *launch.metadata.tls;
      const auto registered = tls_layout.RegisterModule(
          1, tls.memory_size, tls.alignment, tls.image_address);
      std::cout << "tls.layout.status="
                << kajps5::loader::StaticTlsLayoutStatusName(registered.status)
                << '\n';
      if (!registered) {
        std::cerr << "Static TLS layout failed: "
                  << kajps5::loader::StaticTlsLayoutStatusName(
                         registered.status)
                  << '\n';
        return 4;
      }
      tls_module_id = registered.module.module_id;
      tls_static_offset = registered.module.static_offset;
      std::cout << "tls.layout.module_id=" << tls_module_id << '\n'
                << "tls.layout.static_offset=" << tls_static_offset << '\n'
                << "tls.layout.total_size=" << tls_layout.total_size() << '\n';
    } else {
      std::cout << "tls.layout.status=skipped-no-tls\n";
    }
    const kajps5::loader::LayeredImportResolver imports(hle_functions,
                                                        hle_data);
    const auto relocated = kajps5::loader::ApplyRelocations(
        loaded.metadata, memory, imports, 0, tls_module_id, tls_static_offset);
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

int RunExecutableFile(
    const char* path,
    std::span<const std::string_view> module_overlay_directories) {
  std::string file_error;
  auto image = ReadExecutableFile(path, file_error);
  if (!image) {
    std::cerr << "Title preparation failed: " << file_error << '\n';
    return 2;
  }

  const auto process_image_name =
      std::filesystem::path(path).filename().string();
  std::error_code path_error;
  const auto title_path = std::filesystem::absolute(path, path_error);
  if (path_error || title_path.parent_path().empty()) {
    std::cerr
        << "Title preparation failed: cannot resolve the title directory\n";
    return 2;
  }
  const auto title_root = title_path.parent_path();
  if (module_overlay_directories.size() >
      kMaximumModuleOverlayDirectories) {
    std::cerr << "Title preparation failed: too many module directories\n";
    return 2;
  }

  kajps5::kernel::KernelRuntime intake_runtime;
  const auto mount_status =
      intake_runtime.files().MountReadOnly("/app0", title_root);
  if (mount_status != kajps5::kernel::KernelStatus::kOk) {
    std::cerr << "Title preparation failed: cannot mount the title directory"
              << " (kernel status " << static_cast<int>(mount_status) << ")\n";
    return 2;
  }
  std::vector<std::filesystem::path> module_overlay_roots;
  std::vector<std::string> module_guest_directories = {
      "/app0/sce_module", "/app0/sce_modules"};
  module_overlay_roots.reserve(module_overlay_directories.size());
  module_guest_directories.reserve(2 + module_overlay_directories.size());
  for (std::size_t index = 0; index < module_overlay_directories.size();
       ++index) {
    const auto directory = module_overlay_directories[index];
    if (directory.empty() || directory.find('\0') != std::string_view::npos) {
      std::cerr << "Title preparation failed: invalid module directory\n";
      return 2;
    }
    std::error_code overlay_error;
    auto overlay_root = std::filesystem::absolute(
        std::filesystem::path(std::string(directory)), overlay_error);
    if (overlay_error) {
      std::cerr << "Title preparation failed: cannot resolve module directory\n";
      return 2;
    }
    auto guest_root =
        "/module_overlay/" + std::to_string(module_overlay_roots.size());
    const auto overlay_mount_status =
        intake_runtime.files().MountReadOnly(guest_root, overlay_root);
    if (overlay_mount_status != kajps5::kernel::KernelStatus::kOk) {
      std::cerr << "Title preparation failed: cannot mount module directory"
                << " (kernel status "
                << static_cast<int>(overlay_mount_status) << ")\n";
      return 2;
    }
    module_overlay_roots.push_back(std::move(overlay_root));
    module_guest_directories.push_back(std::move(guest_root));
  }
  std::vector<std::string_view> module_search_directories;
  module_search_directories.reserve(module_guest_directories.size());
  for (const auto& directory : module_guest_directories) {
    module_search_directories.push_back(directory);
  }
  auto adjacent = kajps5::loader::DiscoverAdjacentModules(
      intake_runtime.files(), module_search_directories);
  std::cout << "title.modules.status="
            << kajps5::loader::AdjacentModuleLoadStatusName(adjacent.status)
            << '\n'
            << "title.modules.count=" << adjacent.modules.size() << '\n'
            << "title.module_overlays.count=" << module_overlay_roots.size()
            << '\n';
  if (!adjacent) {
    std::cerr << "Title module intake failed: "
              << kajps5::loader::AdjacentModuleLoadStatusName(adjacent.status);
    if (!adjacent.failure_path.empty()) {
      std::cerr << " (" << adjacent.failure_path << ')';
    }
    std::cerr << '\n';
    return 3;
  }

  auto prepared = kajps5::runtime::PrepareTitleImageWithModules(
      *image, process_image_name, std::move(adjacent));
  std::cout << "title.load.status="
            << kajps5::runtime::TitleLoadStatusName(prepared.status) << '\n';
  if (prepared.hle) {
    std::cout << kajps5::hle::FormatImportCoverageTrace(prepared.coverage);
  }
  if (!prepared) {
    std::cerr << "Title preparation failed: "
              << kajps5::runtime::TitleLoadStatusName(prepared.status);
    if (prepared.status ==
        kajps5::runtime::TitleLoadStatus::kUnresolvedImports) {
      std::cerr << " (" << prepared.modules.unresolved_import_count
                << " unresolved imports in " << prepared.modules.failure_path
                << ')';
    } else if (prepared.status == kajps5::runtime::TitleLoadStatus::
                                      kAdjacentModuleInputFailed) {
      std::cerr << " ("
                << kajps5::loader::AdjacentModuleLoadStatusName(
                       prepared.adjacent_status)
                << ')';
    } else if (prepared.status ==
               kajps5::runtime::TitleLoadStatus::kModuleRuntimeFailed) {
      std::cerr << " ("
                << kajps5::runtime::ModuleRuntimeStatusName(
                       prepared.modules.status);
      if (!prepared.modules.failure_path.empty()) {
        std::cerr << " in " << prepared.modules.failure_path;
      }
      std::cerr << ')';
    } else if (prepared.elf_error != kajps5::loader::ElfError::kNone) {
      std::cerr << " (" << kajps5::loader::ElfErrorName(prepared.elf_error)
                << ')';
    } else if (prepared.launch_status !=
               kajps5::loader::LaunchMetadataStatus::kOk) {
      std::cerr << " ("
                << kajps5::loader::LaunchMetadataStatusName(
                       prepared.launch_status)
                << ')';
    }
    std::cerr << '\n';
    return 3;
  }

  const auto runtime_mount_status =
      prepared.session->kernel_runtime().files().MountReadOnly("/app0",
                                                               title_root);
  if (runtime_mount_status != kajps5::kernel::KernelStatus::kOk) {
    std::cerr << "Title preparation failed: cannot mount runtime title files"
              << " (kernel status " << static_cast<int>(runtime_mount_status)
              << ")\n";
    return 3;
  }
  for (std::size_t index = 0; index < module_overlay_roots.size(); ++index) {
    const auto overlay_mount_status =
        prepared.session->kernel_runtime().files().MountReadOnly(
            module_guest_directories[index + 2],
            module_overlay_roots[index]);
    if (overlay_mount_status != kajps5::kernel::KernelStatus::kOk) {
      std::cerr << "Title preparation failed: cannot mount runtime module "
                   "directory"
                << " (kernel status "
                << static_cast<int>(overlay_mount_status) << ")\n";
      return 3;
    }
  }

  const auto started =
      prepared.session->Start(process_image_name, prepared.stack_search_start);
  std::cout << "title.start.status="
            << kajps5::runtime::TitleSessionStatusName(started.status) << '\n'
            << "title.start.phase="
            << kajps5::runtime::TitleSessionPhaseName(started.phase) << '\n';
  if (started.status != kajps5::runtime::TitleSessionStatus::kPending &&
      started.status != kajps5::runtime::TitleSessionStatus::kBlocked) {
    std::cerr << "Title startup failed: "
              << kajps5::runtime::TitleSessionStatusName(started.status)
              << '\n';
    return 4;
  }

  const auto completed = prepared.session->Run(kMaximumTitleRunSlices);
  std::cout << "title.run.status="
            << kajps5::runtime::TitleSessionStatusName(completed.status) << '\n'
            << "title.run.phase="
            << kajps5::runtime::TitleSessionPhaseName(completed.phase) << '\n'
            << "title.run.slices=" << completed.slices << '\n'
            << "title.run.exit_value=" << completed.exit_value << '\n';
  if (!completed) {
    std::cerr << "Title execution stopped: "
              << kajps5::runtime::TitleSessionStatusName(completed.status)
              << '\n';
    return 5;
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
  if (argc >= 3 && std::string_view(argv[1]) == "--run-elf") {
    std::vector<std::string_view> module_directories;
    for (int index = 3; index < argc;) {
      if (std::string_view(argv[index]) != "--module-dir" ||
          index + 1 >= argc) {
        std::cerr << "Usage: kajps5 --run-elf <path> [--module-dir <path>]\n";
        return 1;
      }
      module_directories.emplace_back(argv[index + 1]);
      index += 2;
    }
    return RunExecutableFile(argv[2], module_directories);
  }

  if (argc > 1) {
    std::cerr << "Usage: kajps5 [--version | --trace-elf <path> | --run-elf "
                 "<path> [--module-dir <path>]]\n";
    return 1;
  }

  std::cout << kajps5::ProjectName() << ' ' << kajps5::ProjectVersion() << '\n'
            << kajps5::ProjectStatus() << '\n';
  return 0;
}
