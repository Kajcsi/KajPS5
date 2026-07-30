// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "core/project_info.h"
#include "hle/import_registry.h"
#include "loader/elf.h"
#include "loader/elf_trace.h"
#include "loader/launch_metadata.h"
#include "loader/relocation_trace.h"
#include "loader/relocator.h"

namespace {

constexpr std::uint64_t kMaximumExecutableFileSize = 512U * 1024U * 1024U;
constexpr std::uint64_t kMaximumTraceMemorySize = 512U * 1024U * 1024U;

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

  try {
    kajps5::memory::GuestMemory memory(
        range.base_address, static_cast<std::size_t>(range.size),
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

    const kajps5::hle::ImportRegistry empty_registry;
    const auto tls_module_id = launch.metadata.tls.has_value() ? 1U : 0U;
    const auto relocated = kajps5::loader::ApplyRelocations(
        loaded.metadata, memory, empty_registry, 0, tls_module_id);
    std::cout << kajps5::loader::FormatRelocationTrace(relocated);
    if (!relocated) {
      std::cerr << "Executable relocation check failed: "
                << kajps5::loader::RelocationStatusName(relocated.status)
                << '\n';
      return 4;
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
