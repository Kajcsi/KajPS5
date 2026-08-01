// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "kernel/runtime.h"
#include "loader/module_loader.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kProgramOffset = 0x100;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "module_loader_test: " << message << '\n';
    std::exit(1);
  }
}

void Write16(std::vector<std::byte> &image, std::size_t offset,
             std::uint16_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::vector<std::byte> &image, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::vector<std::byte> &image, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::vector<std::byte> MakePublicModuleElf() {
  std::vector<std::byte> image(kProgramOffset + 1);
  image[0] = std::byte{0x7f};
  image[1] = std::byte{'E'};
  image[2] = std::byte{'L'};
  image[3] = std::byte{'F'};
  image[4] = std::byte{2};
  image[5] = std::byte{1};
  image[6] = std::byte{1};
  Write16(image, 16, 3);
  Write16(image, 18, 62);
  Write32(image, 20, 1);
  Write64(image, 24, 0x1000);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, 56);
  Write16(image, 56, 1);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 5);
  Write64(image, kProgramHeaderOffset + 8, kProgramOffset);
  Write64(image, kProgramHeaderOffset + 16, 0x1000);
  Write64(image, kProgramHeaderOffset + 32, 1);
  Write64(image, kProgramHeaderOffset + 40, 1);
  Write64(image, kProgramHeaderOffset + 48, 0x100);
  image[kProgramOffset] = std::byte{0xc3};
  return image;
}

void Register(kajps5::kernel::FileService &files, std::string path,
              const std::vector<std::byte> &image) {
  Check(files.RegisterReadOnlyFile(std::move(path), image) ==
            kajps5::kernel::KernelStatus::kOk,
        "module fixture registration failed");
}

} // namespace

int main() {
  using namespace kajps5;

  const auto image = MakePublicModuleElf();
  kernel::KernelRuntime runtime;
  auto &files = runtime.files();
  Register(files, "/app0/sce_module/zeta.prx", image);
  Register(files, "/app0/sce_module/Alpha.sprx", image);
  Register(files, "/app0/sce_module/ignored.txt", image);
  Register(files, "/app0/sce_module/libkernel.prx", image);
  Register(files, "/app0/sce_modules/Beta.PRX", image);

  const auto loaded = loader::DiscoverAdjacentModules(files);
  Check(loaded && loaded.modules.size() == 3 &&
            loaded.modules[0].guest_path == "/app0/sce_module/Alpha.sprx" &&
            loaded.modules[1].guest_path == "/app0/sce_module/zeta.prx" &&
            loaded.modules[2].guest_path == "/app0/sce_modules/Beta.PRX" &&
            loaded.modules[0].metadata.entry_point == 0x1000 &&
            loaded.start_plan.start_order.empty() &&
            runtime.handles().size() == 0,
        "adjacent modules were not parsed in stable directory order");

  kernel::KernelRuntime missing_runtime;
  const auto missing = loader::DiscoverAdjacentModules(missing_runtime.files());
  Check(missing && missing.modules.empty(),
        "missing optional module directories failed discovery");

  kernel::KernelRuntime malformed_runtime;
  Register(malformed_runtime.files(), "/app0/sce_module/good.prx", image);
  Register(malformed_runtime.files(), "/app0/sce_modules/broken.prx",
           {std::byte{0}});
  const auto malformed =
      loader::DiscoverAdjacentModules(malformed_runtime.files());
  Check(malformed.status == loader::AdjacentModuleLoadStatus::kParseFailed &&
            malformed.failure_path == "/app0/sce_modules/broken.prx" &&
            malformed.elf_error == loader::ElfError::kImageTooSmall &&
            malformed.modules.empty() &&
            malformed_runtime.handles().size() == 0,
        "malformed module did not fail the complete parse batch");

  kernel::KernelRuntime empty_runtime;
  Register(empty_runtime.files(), "/app0/sce_module/empty.prx", {});
  Check(loader::DiscoverAdjacentModules(empty_runtime.files()).status ==
            loader::AdjacentModuleLoadStatus::kEmptyImage,
        "empty module image was accepted");

  loader::AdjacentModuleLimits small_file_limit;
  small_file_limit.maximum_module_size = image.size() - 1;
  Check(loader::DiscoverAdjacentModules(files, small_file_limit).status ==
            loader::AdjacentModuleLoadStatus::kImageTooLarge,
        "per-module byte limit was ignored");

  loader::AdjacentModuleLimits small_total_limit;
  small_total_limit.maximum_total_bytes = image.size() * 2 - 1;
  Check(loader::DiscoverAdjacentModules(files, small_total_limit).status ==
            loader::AdjacentModuleLoadStatus::kTotalSizeExceeded,
        "aggregate module byte limit was ignored");

  kernel::KernelRuntime duplicate_runtime;
  Register(duplicate_runtime.files(), "/app0/sce_module/shared.prx", image);
  Register(duplicate_runtime.files(), "/app0/sce_modules/SHARED.PRX", image);
  const auto duplicate =
      loader::DiscoverAdjacentModules(duplicate_runtime.files());
  Check(duplicate.status == loader::AdjacentModuleLoadStatus::kPlanFailed &&
            duplicate.start_plan.status ==
                loader::ModulePlanStatus::kDuplicateModuleIdentity &&
            duplicate.modules.empty(),
        "duplicate module identity escaped the checked plan");

  kernel::KernelRuntime limited_runtime;
  Register(limited_runtime.files(), "/app0/sce_module/a.prx", image);
  Register(limited_runtime.files(), "/app0/sce_module/b.prx", image);
  loader::AdjacentModuleLimits one_module;
  one_module.maximum_modules = 1;
  Check(loader::DiscoverAdjacentModules(limited_runtime.files(), one_module)
                .status ==
            loader::AdjacentModuleLoadStatus::kModuleLimitExceeded,
        "adjacent module count limit was ignored");

  const std::string_view invalid_directory = "relative/sce_module";
  Check(loader::DiscoverAdjacentModules(
            files, std::span<const std::string_view>(&invalid_directory, 1))
                .status == loader::AdjacentModuleLoadStatus::kInvalidArgument,
        "relative module directory was accepted");
  Check(loader::AdjacentModuleLoadStatusName(
            loader::AdjacentModuleLoadStatus::kTotalSizeExceeded) ==
            "total-size-exceeded",
        "adjacent module status name is unstable");

  std::cout << "module loader tests passed\n";
  return 0;
}
