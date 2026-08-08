// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/memory/guest_memory.h"
#include "core/project_info.h"
#include "cpu/native_guest_executor.h"
#include "cpu/native_hle_import_table.h"
#include "hle/ampr_exports.h"
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

#if defined(_WIN32)
#include "platform/windows/native_guest_instruction_sampler.h"
#include "platform/windows/vulkan_window.h"
#endif

namespace {

constexpr std::uint64_t kMaximumExecutableFileSize = 512U * 1024U * 1024U;
constexpr std::uint64_t kMaximumTraceMemorySize = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumTitleRunSlices = 1000000;
constexpr std::size_t kTitleRunChunkSlices = 1024;
constexpr std::size_t kMaximumTitleRunIterations = 1000000;
constexpr std::size_t kMaximumModuleOverlayDirectories = 16;
#if defined(_WIN32)
constexpr std::uint32_t kMaximumGuestWatchdogMilliseconds = 4294967294U;
constexpr std::size_t kMaximumGuestWatchdogMetadataBytes = 64;

std::string EncodeGuestWatchdogMetadata(std::string_view value) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  const auto size =
      std::min(value.size(), kMaximumGuestWatchdogMetadataBytes);
  std::string encoded;
  encoded.reserve(size * 2);
  for (std::size_t index = 0; index < size; ++index) {
    const auto byte = static_cast<unsigned char>(value[index]);
    encoded.push_back(kHexDigits[byte >> 4U]);
    encoded.push_back(kHexDigits[byte & 0x0fU]);
  }
  return encoded;
}

std::optional<std::uint32_t> ParsePositiveMilliseconds(
    const std::string_view value) noexcept {
  std::uint32_t milliseconds = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                      milliseconds);
  if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
      milliseconds == 0 || milliseconds > kMaximumGuestWatchdogMilliseconds) {
    return std::nullopt;
  }
  return milliseconds;
}
#endif

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
    const auto ampr_export_status = kajps5::hle::RegisterAmprExports(
        hle_exports, kernel_runtime.ampr_command_buffers(), memory);
    if (ampr_export_status != kajps5::hle::ExportRegistryStatus::kOk) {
      std::cerr << "HLE coverage check failed: AMPR export registration "
                   "returned "
                << kajps5::hle::ExportRegistryStatusName(ampr_export_status)
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
    std::span<const std::string_view> module_overlay_directories,
    bool headless,
    std::optional<std::uint32_t> guest_watchdog_milliseconds) {
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

#if defined(_WIN32)
  // This must outlive the prepared session so its Vulkan surface is destroyed
  // before the HWND on every return path.
  std::unique_ptr<kajps5::platform::windows::VulkanWindow> title_window;
  VkExtent2D title_window_extent{};
#endif
  auto prepared = kajps5::runtime::PrepareTitleImageWithModules(
      *image, process_image_name, std::move(adjacent));
  std::cout << "title.load.status="
            << kajps5::runtime::TitleLoadStatusName(prepared.status) << '\n';
  if (prepared.hle) {
    std::cout << kajps5::hle::FormatImportCoverageTrace(prepared.coverage);
  }
  if (!prepared) {
    if (prepared.status ==
            kajps5::runtime::TitleLoadStatus::kUnresolvedImports &&
        prepared.modules.import_coverage) {
      std::cout << kajps5::hle::FormatImportCoverageTrace(
          prepared.modules.import_coverage, "title.module_coverage");
    }
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

#if defined(_WIN32)
  if (!headless) {
    title_window = kajps5::platform::windows::VulkanWindow::CreateVisible();
    if (!title_window) {
      std::cerr << "Title presentation failed: cannot create a visible Vulkan window\n";
      return 4;
    }
    const auto presentation = prepared.session->gpu_runtime()
                                  .InitializeVulkanPresentation(
                                      title_window->surface_factory());
    if (!presentation) {
      std::cerr << "Title presentation initialization failed: "
                << kajps5::gpu::vulkan::VulkanPresentationStatusName(
                       presentation.status);
      if (!presentation.diagnostics.empty() &&
          !presentation.diagnostics.front().message.empty()) {
        std::cerr << " (" << presentation.diagnostics.front().message << ')';
      }
      std::cerr << '\n';
      return 4;
    }
    title_window_extent = title_window->client_extent();
    prepared.session->gpu_runtime().EnableVulkanActionExecution(true);
  } else {
    prepared.session->gpu_runtime().EnableVulkanActionExecution(false);
  }
#else
  if (!headless) {
    std::cerr << "Visible title rendering is only available on Windows; use --headless\n";
    return 4;
  }
  prepared.session->gpu_runtime().EnableVulkanActionExecution(false);
#endif

#if defined(_WIN32)
  std::unique_ptr<kajps5::platform::windows::NativeGuestInstructionSampler>
      guest_instruction_sampler;
  if (guest_watchdog_milliseconds) {
    guest_instruction_sampler =
        kajps5::platform::windows::NativeGuestInstructionSampler::StartForCallingThread(
            std::chrono::milliseconds(*guest_watchdog_milliseconds),
            [&session = *prepared.session](const auto& sample) {
              const bool in_guest_memory = session.memory().Contains(
                  sample.instruction_pointer, 1);
              std::cerr << "title.guest_watchdog.sample=" << sample.index << '\n'
                        << "title.guest_watchdog.rip=0x" << std::hex
                        << sample.instruction_pointer << std::dec << '\n'
                        << "title.guest_watchdog.in_guest_memory="
                        << (in_guest_memory ? "true" : "false") << '\n';
              if (!in_guest_memory) {
                const auto* const hle_functions = session.hle_functions();
                const auto active_dispatch =
                    hle_functions == nullptr
                        ? std::optional<kajps5::cpu::NativeHleDispatchSnapshot>{}
                        : hle_functions->active_dispatch();
                const auto active = active_dispatch.has_value();
                const auto guest_return_instruction_pointer =
                    active ? active_dispatch->guest_return_instruction_pointer
                           : 0;
                const auto guest_stack_pointer =
                    active ? active_dispatch->guest_stack_pointer : 0;
                const auto symbol =
                    active ? EncodeGuestWatchdogMetadata(active_dispatch->symbol)
                           : std::string{};
                const auto library =
                    active ? EncodeGuestWatchdogMetadata(active_dispatch->library)
                           : std::string{};
                std::cerr << "title.guest_watchdog.hle_active="
                          << (active ? "true" : "false") << '\n'
                          << "title.guest_watchdog.hle_guest_rip=0x"
                          << std::hex << guest_return_instruction_pointer
                          << std::dec << '\n'
                          << "title.guest_watchdog.hle_guest_rsp=0x"
                          << std::hex << guest_stack_pointer << std::dec << '\n'
                          << "title.guest_watchdog.hle_symbol_hex=" << symbol
                          << '\n'
                          << "title.guest_watchdog.hle_library_hex=" << library
                          << '\n';
              }
              std::cerr << std::flush;
            });
    if (!guest_instruction_sampler) {
      std::cerr << "Guest watchdog unavailable: cannot start Windows sampler\n";
      return 4;
    }
  }
#else
  (void)guest_watchdog_milliseconds;
#endif

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

  std::size_t guest_slices = 0;
  std::size_t iterations = 0;
  while (guest_slices < kMaximumTitleRunSlices &&
         iterations < kMaximumTitleRunIterations) {
#if defined(_WIN32)
    if (title_window) {
      title_window->PumpMessages();
      if (title_window->closed()) {
        std::cout << "title.run.status=window-closed\n"
                  << "title.run.phase="
                  << kajps5::runtime::TitleSessionPhaseName(
                         prepared.session->phase())
                  << '\n'
                  << "title.run.slices=" << guest_slices << '\n'
                  << "title.run.exit_value=" << prepared.session->exit_value()
                  << '\n';
        std::cerr << "Title execution stopped: window closed\n";
        return 5;
      }
      const auto extent = title_window->client_extent();
      if (extent.width != title_window_extent.width ||
          extent.height != title_window_extent.height) {
        (void)prepared.session->gpu_runtime().ResizeVulkanPresentation(extent);
        title_window_extent = extent;
      }
    }
#endif
    (void)prepared.session->video_out().PollPresentation();
    const auto remaining_slices = kMaximumTitleRunSlices - guest_slices;
    const auto completed = prepared.session->Run(
        std::min(kTitleRunChunkSlices, remaining_slices));
    guest_slices += completed.slices;
    ++iterations;
    if (completed.status == kajps5::runtime::TitleSessionStatus::kExited) {
      std::cout << "title.run.status="
                << kajps5::runtime::TitleSessionStatusName(completed.status)
                << '\n'
                << "title.run.phase="
                << kajps5::runtime::TitleSessionPhaseName(completed.phase)
                << '\n'
                << "title.run.slices=" << guest_slices << '\n'
                << "title.run.exit_value=" << completed.exit_value << '\n';
      return 0;
    }
    if (completed.status == kajps5::runtime::TitleSessionStatus::kSliceLimitReached ||
        completed.status == kajps5::runtime::TitleSessionStatus::kPending ||
        completed.status == kajps5::runtime::TitleSessionStatus::kBlocked) {
      continue;
    }
    std::cout << "title.run.status="
              << kajps5::runtime::TitleSessionStatusName(completed.status)
              << '\n'
              << "title.run.phase="
              << kajps5::runtime::TitleSessionPhaseName(completed.phase)
              << '\n'
              << "title.run.slices=" << guest_slices << '\n'
              << "title.run.exit_value=" << completed.exit_value << '\n';
    std::cerr << "Title execution stopped: "
              << kajps5::runtime::TitleSessionStatusName(completed.status)
              << '\n';
    return 5;
  }
  std::cout << "title.run.status=run-budget-exhausted\n"
            << "title.run.phase="
            << kajps5::runtime::TitleSessionPhaseName(prepared.session->phase())
            << '\n'
            << "title.run.slices=" << guest_slices << '\n'
            << "title.run.exit_value=" << prepared.session->exit_value() << '\n';
  std::cerr << "Title execution stopped: run budget exhausted\n";
  return 5;
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
    bool headless = false;
    std::optional<std::uint32_t> guest_watchdog_milliseconds;
    for (int index = 3; index < argc;) {
      const std::string_view argument = argv[index];
      if (argument == "--headless") {
        headless = true;
        ++index;
      } else if (argument == "--module-dir" && index + 1 < argc) {
        module_directories.emplace_back(argv[index + 1]);
        index += 2;
      } else if (argument == "--guest-watchdog-ms" && index + 1 < argc) {
#if !defined(_WIN32)
        std::cerr << "--guest-watchdog-ms is only available on Windows\n";
        return 1;
#else
        guest_watchdog_milliseconds = ParsePositiveMilliseconds(argv[index + 1]);
        if (!guest_watchdog_milliseconds) {
          std::cerr << "--guest-watchdog-ms must be a positive whole number of milliseconds\n";
          return 1;
        }
        index += 2;
#endif
      } else {
        std::cerr << "Usage: kajps5 --run-elf <path> [--headless] [--module-dir <path>] [--guest-watchdog-ms <positive-ms>]\n";
        return 1;
      }
    }
    return RunExecutableFile(argv[2], module_directories, headless,
                             guest_watchdog_milliseconds);
  }

  if (argc > 1) {
    std::cerr << "Usage: kajps5 [--version | --trace-elf <path> | --run-elf "
                  "<path> [--headless] [--module-dir <path>] "
                  "[--guest-watchdog-ms <positive-ms>]]\n";
    return 1;
  }

  std::cout << kajps5::ProjectName() << ' ' << kajps5::ProjectVersion() << '\n'
            << kajps5::ProjectStatus() << '\n';
  return 0;
}
