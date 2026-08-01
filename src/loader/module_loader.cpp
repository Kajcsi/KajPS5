// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/module_loader.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace kajps5::loader {
namespace {

constexpr std::array<std::string_view, 2> kDefaultModuleDirectories = {
    "/app0/sce_module", "/app0/sce_modules"};

unsigned char FoldAscii(unsigned char value) noexcept {
  return value >= 'A' && value <= 'Z'
             ? static_cast<unsigned char>(value + ('a' - 'A'))
             : value;
}

std::string FoldAscii(std::string_view value) {
  std::string folded(value);
  std::transform(folded.begin(), folded.end(), folded.begin(), [](char item) {
    return static_cast<char>(FoldAscii(static_cast<unsigned char>(item)));
  });
  return folded;
}

bool IsModuleFile(std::string_view name) {
  const auto folded = FoldAscii(name);
  return folded.ends_with(".prx") || folded.ends_with(".sprx");
}

bool IsCoreRuntimeImage(std::string_view name) {
  const auto folded = FoldAscii(name);
  return folded == "eboot.bin" || folded == "libkernel.prx" ||
         folded == "libkernel_sys.prx";
}

struct ReadFileResult {
  AdjacentModuleLoadStatus status = AdjacentModuleLoadStatus::kOk;
  kernel::KernelStatus kernel_status = kernel::KernelStatus::kOk;
  std::vector<std::byte> image;
};

class FileHandleGuard final {
public:
  FileHandleGuard(kernel::FileService &files,
                  kernel::KernelHandle handle) noexcept
      : files_(files), handle_(handle) {}

  ~FileHandleGuard() {
    if (handle_ != kernel::kInvalidKernelHandle) {
      (void)files_.Close(handle_);
    }
  }

  FileHandleGuard(const FileHandleGuard &) = delete;
  FileHandleGuard &operator=(const FileHandleGuard &) = delete;

  void Release() noexcept { handle_ = kernel::kInvalidKernelHandle; }

private:
  kernel::FileService &files_;
  kernel::KernelHandle handle_;
};

ReadFileResult ReadModuleImage(kernel::FileService &files,
                               std::string_view guest_path,
                               const AdjacentModuleLimits &limits,
                               std::uint64_t total_bytes) {
  const auto opened = files.Open(guest_path, kernel::kFileOpenRead);
  if (!opened) {
    return {AdjacentModuleLoadStatus::kOpenFailed, opened.status, {}};
  }
  FileHandleGuard guard(files, opened.handle);

  const auto stat = files.Fstat(opened.handle);
  if (!stat) {
    return {AdjacentModuleLoadStatus::kStatFailed, stat.status, {}};
  }
  if (stat.size == 0) {
    return {
        AdjacentModuleLoadStatus::kEmptyImage, kernel::KernelStatus::kOk, {}};
  }
  if (stat.size > limits.maximum_module_size ||
      stat.size >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return {AdjacentModuleLoadStatus::kImageTooLarge,
            kernel::KernelStatus::kOk,
            {}};
  }
  if (stat.size > limits.maximum_total_bytes ||
      total_bytes > limits.maximum_total_bytes - stat.size) {
    return {AdjacentModuleLoadStatus::kTotalSizeExceeded,
            kernel::KernelStatus::kOk,
            {}};
  }

  std::vector<std::byte> image;
  try {
    image.resize(static_cast<std::size_t>(stat.size));
  } catch (const std::bad_alloc &) {
    return {AdjacentModuleLoadStatus::kImageTooLarge,
            kernel::KernelStatus::kNoResources,
            {}};
  }
  std::size_t offset = 0;
  while (offset < image.size()) {
    const auto read =
        files.Read(opened.handle, std::span(image).subspan(offset));
    if (!read || read.value == 0 || read.value > image.size() - offset) {
      return {AdjacentModuleLoadStatus::kReadFailed, read.status, {}};
    }
    offset += static_cast<std::size_t>(read.value);
  }
  const auto close_status = files.Close(opened.handle);
  if (close_status != kernel::KernelStatus::kOk) {
    return {AdjacentModuleLoadStatus::kCloseFailed, close_status, {}};
  }
  guard.Release();
  return {AdjacentModuleLoadStatus::kOk, kernel::KernelStatus::kOk,
          std::move(image)};
}

AdjacentModuleLoadResult
Failure(AdjacentModuleLoadStatus status, std::string path = {},
        kernel::KernelStatus kernel_status = kernel::KernelStatus::kOk,
        ElfError elf_error = ElfError::kNone) {
  AdjacentModuleLoadResult result;
  result.status = status;
  result.failure_path = std::move(path);
  result.kernel_status = kernel_status;
  result.elf_error = elf_error;
  return result;
}

} // namespace

AdjacentModuleLoadResult DiscoverAdjacentModules(kernel::FileService &files,
                                                 AdjacentModuleLimits limits) {
  return DiscoverAdjacentModules(files, kDefaultModuleDirectories, limits);
}

AdjacentModuleLoadResult
DiscoverAdjacentModules(kernel::FileService &files,
                        std::span<const std::string_view> guest_directories,
                        AdjacentModuleLimits limits) {
  if (guest_directories.empty() || limits.maximum_modules == 0 ||
      limits.maximum_module_size == 0 || limits.maximum_total_bytes == 0) {
    return Failure(AdjacentModuleLoadStatus::kInvalidArgument);
  }

  std::vector<std::string> module_paths;
  std::set<std::string> seen_paths;
  for (const auto guest_directory : guest_directories) {
    const auto normalized =
        kernel::FileService::NormalizeGuestPath(guest_directory);
    if (!normalized) {
      return Failure(AdjacentModuleLoadStatus::kInvalidArgument,
                     std::string(guest_directory));
    }
    const auto opened = files.Open(*normalized, kernel::kFileOpenDirectory);
    if (!opened) {
      if (opened.status == kernel::KernelStatus::kNotFound) {
        continue;
      }
      return Failure(AdjacentModuleLoadStatus::kOpenFailed, *normalized,
                     opened.status);
    }
    FileHandleGuard guard(files, opened.handle);
    while (true) {
      const auto entry = files.ReadDirectory(opened.handle);
      if (!entry) {
        return Failure(AdjacentModuleLoadStatus::kReadFailed, *normalized,
                       entry.status);
      }
      if (entry.end_of_directory) {
        break;
      }
      if (!entry.entry.is_file || !IsModuleFile(entry.entry.name) ||
          IsCoreRuntimeImage(entry.entry.name)) {
        continue;
      }
      const auto path = *normalized + "/" + entry.entry.name;
      if (seen_paths.insert(FoldAscii(path)).second) {
        module_paths.push_back(path);
        if (module_paths.size() > limits.maximum_modules) {
          return Failure(AdjacentModuleLoadStatus::kModuleLimitExceeded, path);
        }
      }
    }
    const auto close_status = files.Close(opened.handle);
    if (close_status != kernel::KernelStatus::kOk) {
      return Failure(AdjacentModuleLoadStatus::kCloseFailed, *normalized,
                     close_status);
    }
    guard.Release();
  }

  std::vector<AdjacentModuleImage> pending;
  pending.reserve(module_paths.size());
  std::uint64_t total_bytes = 0;
  for (const auto &path : module_paths) {
    auto read = ReadModuleImage(files, path, limits, total_bytes);
    if (read.status != AdjacentModuleLoadStatus::kOk) {
      return Failure(read.status, path, read.kernel_status);
    }
    auto parsed = ParseExecutable64(read.image);
    if (!parsed) {
      return Failure(AdjacentModuleLoadStatus::kParseFailed, path,
                     kernel::KernelStatus::kOk, parsed.error);
    }
    total_bytes += read.image.size();
    pending.push_back(
        {path, std::move(read.image), std::move(parsed.metadata)});
  }

  std::vector<ModuleDependencyInput> dependency_inputs;
  dependency_inputs.reserve(pending.size());
  for (const auto &module : pending) {
    dependency_inputs.push_back(
        MakeModuleDependencyInput(module.guest_path, module.metadata));
  }
  auto start_plan = BuildModuleStartPlan(dependency_inputs);
  if (!start_plan) {
    AdjacentModuleLoadResult result =
        Failure(AdjacentModuleLoadStatus::kPlanFailed);
    result.start_plan = std::move(start_plan);
    return result;
  }

  AdjacentModuleLoadResult result;
  result.modules = std::move(pending);
  result.start_plan = std::move(start_plan);
  return result;
}

std::string_view
AdjacentModuleLoadStatusName(AdjacentModuleLoadStatus status) noexcept {
  switch (status) {
  case AdjacentModuleLoadStatus::kOk:
    return "ok";
  case AdjacentModuleLoadStatus::kInvalidArgument:
    return "invalid-argument";
  case AdjacentModuleLoadStatus::kModuleLimitExceeded:
    return "module-limit-exceeded";
  case AdjacentModuleLoadStatus::kOpenFailed:
    return "open-failed";
  case AdjacentModuleLoadStatus::kStatFailed:
    return "stat-failed";
  case AdjacentModuleLoadStatus::kEmptyImage:
    return "empty-image";
  case AdjacentModuleLoadStatus::kImageTooLarge:
    return "image-too-large";
  case AdjacentModuleLoadStatus::kTotalSizeExceeded:
    return "total-size-exceeded";
  case AdjacentModuleLoadStatus::kReadFailed:
    return "read-failed";
  case AdjacentModuleLoadStatus::kCloseFailed:
    return "close-failed";
  case AdjacentModuleLoadStatus::kParseFailed:
    return "parse-failed";
  case AdjacentModuleLoadStatus::kPlanFailed:
    return "plan-failed";
  }
  return "unknown";
}

} // namespace kajps5::loader
