// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/module_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "loader/elf.h"
#include "loader/launch_metadata.h"
#include "loader/module_loader.h"
#include "loader/module_plan.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kProgramOffset = 0x100;
constexpr std::size_t kDynamicOffset = 0x180;
constexpr std::size_t kDynamicEntrySize = 16;
constexpr std::size_t kStringTableOffset = 0x220;
constexpr std::size_t kRelocationOffset = 0x240;
constexpr std::size_t kSymbolOffset = 0x260;
constexpr std::size_t kHashOffset = 0x290;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "module_runtime_test: " << message << '\n';
    std::exit(1);
  }
}

void Write16(std::vector<std::byte>& image, std::size_t offset,
             std::uint16_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::vector<std::byte>& image, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::vector<std::byte>& image, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void WriteDynamic(std::vector<std::byte>& image, std::size_t index,
                  std::int64_t tag, std::uint64_t value) {
  const auto offset = kDynamicOffset + index * kDynamicEntrySize;
  Write64(image, offset, static_cast<std::uint64_t>(tag));
  Write64(image, offset + sizeof(std::uint64_t), value);
}

std::vector<std::byte> MakePublicElf() {
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

std::vector<std::byte> MakeUnresolvedImportElf() {
  std::vector<std::byte> image(0x300);
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
  Write16(image, 56, 2);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 7);
  Write64(image, kProgramHeaderOffset + 8, kProgramOffset);
  Write64(image, kProgramHeaderOffset + 16, 0x1000);
  Write64(image, kProgramHeaderOffset + 32, 0x200);
  Write64(image, kProgramHeaderOffset + 40, 0x200);
  Write64(image, kProgramHeaderOffset + 48, 0x100);

  const auto dynamic_header = kProgramHeaderOffset + 56;
  Write32(image, dynamic_header, 2);
  Write32(image, dynamic_header + 4, 4);
  Write64(image, dynamic_header + 8, kDynamicOffset);
  Write64(image, dynamic_header + 16, 0x1080);
  Write64(image, dynamic_header + 32, 9 * kDynamicEntrySize);
  Write64(image, dynamic_header + 40, 9 * kDynamicEntrySize);
  Write64(image, dynamic_header + 48, 8);

  WriteDynamic(image, 0, 5, 0x1120);
  WriteDynamic(image, 1, 10, 18);
  WriteDynamic(image, 2, 7, 0x1140);
  WriteDynamic(image, 3, 8, 24);
  WriteDynamic(image, 4, 9, 24);
  WriteDynamic(image, 5, 4, 0x1190);
  WriteDynamic(image, 6, 6, 0x1160);
  WriteDynamic(image, 7, 11, 24);
  WriteDynamic(image, 8, 0, 0);

  constexpr std::string_view kMissingSymbol = "adjacent_missing";
  for (std::size_t index = 0; index < kMissingSymbol.size(); ++index) {
    image[kStringTableOffset + 1 + index] =
        static_cast<std::byte>(kMissingSymbol[index]);
  }
  Write32(image, kRelocationOffset, 0x1010);
  Write64(image, kRelocationOffset + 8, (std::uint64_t{1} << 32U) | 6U);
  Write32(image, kSymbolOffset + 24, 1);
  image[kSymbolOffset + 28] = std::byte{0x12};
  Write32(image, kHashOffset, 1);
  Write32(image, kHashOffset + 4, 2);
  Write32(image, kHashOffset + 8, 1);
  image[kProgramOffset] = std::byte{0xc3};
  return image;
}

class EmptyResolver final : public kajps5::loader::ImportResolver {
 public:
  [[nodiscard]] std::optional<std::uint64_t> ResolveImport(
      std::string_view, std::span<const std::string>) const override {
    return std::nullopt;
  }
};

kajps5::loader::AdjacentModuleImage Module(
    std::string path, const std::vector<std::byte>& image,
    const kajps5::loader::ElfMetadata& metadata) {
  return {std::move(path), image, metadata};
}

}  // namespace

int main() {
  using namespace kajps5;

  const auto image = MakePublicElf();
  auto memory = memory::GuestMemory::CreateHostMapped(0x40000);
  Check(memory != nullptr, "guest memory allocation failed");
  const auto parsed = loader::ParseExecutable64(image);
  const auto range = loader::CalculateElfLoadRange(parsed.metadata);
  const auto main_bias = memory->base_address() - range.base_address;
  auto main_loaded = loader::LoadExecutable64(image, *memory, main_bias);
  const auto main_launch =
      loader::AnalyzeLaunchMetadata(main_loaded.metadata, main_bias);
  Check(parsed && range && main_loaded && main_launch,
        "main program fixture preparation failed");

  runtime::ModuleRuntime modules(*memory);
  Check(static_cast<bool>(
            modules.RegisterMain("/app0/eboot.bin", main_loaded.metadata, range,
                                 main_bias, main_launch.metadata)),
        "main program registration failed");
  std::vector<loader::AdjacentModuleImage> adjacent;
  adjacent.push_back(
      Module("/app0/sce_module/first.prx", image, parsed.metadata));
  adjacent.push_back(
      Module("/app0/sce_module/second.sprx", image, parsed.metadata));
  std::vector<loader::ModuleDependencyInput> dependencies;
  dependencies.push_back(
      loader::MakeModuleDependencyInput("first.prx", parsed.metadata));
  dependencies.push_back(
      loader::MakeModuleDependencyInput("second.sprx", parsed.metadata));
  auto start_plan = loader::BuildModuleStartPlan(dependencies);
  const auto loaded =
      modules.LoadAdjacent(std::move(adjacent), std::move(start_plan),
                           memory->base_address() + 0x4000);
  Check(
      loaded && loaded.next_load_address != 0 &&
          modules.programs().size() == 3 &&
          modules.programs()[0].module_id == 1 &&
          modules.programs()[1].module_id == 2 &&
          modules.programs()[2].module_id == 3 &&
          modules.programs()[1].load_bias != modules.programs()[2].load_bias &&
          memory->CanExecute(modules.programs()[1].metadata.entry_point +
                                 modules.programs()[1].load_bias,
                             1) &&
          memory->CanExecute(modules.programs()[2].metadata.entry_point +
                                 modules.programs()[2].load_bias,
                             1) &&
          modules.MetadataPointers().size() == 3 &&
          modules.tls_layout().module_count() == 0,
      "adjacent programs did not receive distinct checked placements");

  EmptyResolver fallback;
  Check(modules.RelocateAndPlan(fallback) && modules.BuildCombinedLifecycle() &&
            modules.BuildCombinedLifecycle().plan.initializers.empty() &&
            modules.exports().size() == 0,
        "module relocation or empty lifecycle planning failed");
  Check(modules.RelocateAndPlan(fallback).status ==
            runtime::ModuleRuntimeStatus::kInvalidState,
        "module runtime accepted a second relocation pass");

  auto import_memory = memory::GuestMemory::CreateHostMapped(0x10000);
  Check(import_memory != nullptr, "import guest memory allocation failed");
  auto import_metadata = loader::ParseExecutable64(image).metadata;
  import_metadata.dynamic_info.symbols.resize(2);
  import_metadata.dynamic_info.symbols[1].name = "missing";
  import_metadata.dynamic_info.relocations.push_back(
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0});
  const auto import_bias = import_memory->base_address() - range.base_address;
  const auto import_loaded =
      loader::LoadExecutable64(image, *import_memory, import_bias);
  Check(static_cast<bool>(import_loaded),
        "import fixture executable load failed");
  runtime::ModuleRuntime import_modules(*import_memory);
  Check(static_cast<bool>(import_modules.RegisterMain(
            "/app0/eboot.bin", import_metadata, range, import_bias,
            main_launch.metadata)),
        "import main program registration failed");
  Check(static_cast<bool>(import_modules.LoadAdjacent(
            std::vector<loader::AdjacentModuleImage>{},
            loader::BuildModuleStartPlan({}),
            import_memory->base_address() + 0x4000)),
        "import empty module intake failed");
  const auto import_result = import_modules.RelocateAndPlan(fallback);
  Check(import_result && import_result.unresolved_import_count == 1 &&
            import_result.import_coverage.unresolved_unique_import_count ==
                1 &&
            import_modules.BuildCombinedLifecycle(),
        "unresolved imports blocked module planning or were not diagnosed");

  auto adjacent_import_memory = memory::GuestMemory::CreateHostMapped(0x10000);
  Check(adjacent_import_memory != nullptr,
        "adjacent import guest memory allocation failed");
  const auto adjacent_main_bias =
      adjacent_import_memory->base_address() - range.base_address;
  const auto adjacent_main_loaded =
      loader::LoadExecutable64(image, *adjacent_import_memory,
                               adjacent_main_bias);
  Check(static_cast<bool>(adjacent_main_loaded),
        "adjacent import main executable load failed");
  runtime::ModuleRuntime adjacent_import_modules(*adjacent_import_memory);
  Check(static_cast<bool>(adjacent_import_modules.RegisterMain(
            "/app0/eboot.bin", adjacent_main_loaded.metadata, range,
            adjacent_main_bias, main_launch.metadata)),
        "adjacent import main program registration failed");
  const auto adjacent_import_image = MakeUnresolvedImportElf();
  const auto adjacent_import_parsed =
      loader::ParseExecutable64(adjacent_import_image);
  Check(static_cast<bool>(adjacent_import_parsed),
        "adjacent unresolved import fixture parse failed");
  const auto adjacent_import_load = adjacent_import_modules.LoadAdjacent(
      {Module("/app0/sce_module/missing.prx", adjacent_import_image,
              adjacent_import_parsed.metadata)},
      loader::BuildModuleStartPlan(
          {loader::MakeModuleDependencyInput("missing.prx",
                                              adjacent_import_parsed.metadata)}),
      adjacent_import_memory->base_address() + 0x4000);
  Check(static_cast<bool>(adjacent_import_load),
        "adjacent unresolved import module intake failed");
  const auto adjacent_import_result =
      adjacent_import_modules.RelocateAndPlan(fallback);
  Check(adjacent_import_result &&
            adjacent_import_result.unresolved_import_count == 1 &&
            adjacent_import_result.program_index == 1 &&
            adjacent_import_result.failure_path ==
                "/app0/sce_module/missing.prx" &&
            adjacent_import_result.import_coverage
                    .unresolved_unique_import_count ==
                1 &&
            adjacent_import_result.import_coverage.imports.size() == 1 &&
            adjacent_import_result.import_coverage.imports[0].symbol ==
                "adjacent_missing",
        "adjacent-only unresolved import coverage described the wrong program");

  std::array<loader::LifecyclePlanResult, 3> lifecycles;
  lifecycles[0].plan = {{100}, {101}, {102}};
  lifecycles[1].plan = {{200}, {201}, {202}};
  lifecycles[2].plan = {{300}, {301}, {302}};
  const std::array<std::size_t, 2> dependency_order = {1, 0};
  const auto combined =
      runtime::CombineModuleLifecycles(lifecycles, dependency_order);
  Check(combined &&
            combined.plan.preinitializers ==
                std::vector<std::uint64_t>({300, 200, 100}) &&
            combined.plan.initializers ==
                std::vector<std::uint64_t>({301, 201, 101}) &&
            combined.plan.finalizers ==
                std::vector<std::uint64_t>({102, 202, 302}),
        "module lifecycle order is incorrect");
  const std::array<std::size_t, 1> invalid_order = {2};
  Check(runtime::CombineModuleLifecycles(lifecycles, invalid_order).status ==
            runtime::ModuleRuntimeStatus::kInvalidArgument,
        "out-of-range module lifecycle index was accepted");
  const std::array<std::size_t, 2> repeated_order = {0, 0};
  Check(runtime::CombineModuleLifecycles(lifecycles, repeated_order).status ==
            runtime::ModuleRuntimeStatus::kInvalidArgument,
        "repeated module lifecycle index was accepted");

  auto small_memory = memory::GuestMemory::CreateHostMapped(0x8000);
  Check(small_memory != nullptr, "small guest memory allocation failed");
  const auto small_bias = small_memory->base_address() - range.base_address;
  auto small_main = loader::LoadExecutable64(image, *small_memory, small_bias);
  const auto small_launch =
      loader::AnalyzeLaunchMetadata(small_main.metadata, small_bias);
  runtime::ModuleRuntime limited(*small_memory);
  Check(static_cast<bool>(
            limited.RegisterMain("/app0/eboot.bin", small_main.metadata, range,
                                 small_bias, small_launch.metadata)),
        "small main program registration failed");
  std::vector<loader::AdjacentModuleImage> one_module;
  one_module.push_back(
      Module("/app0/sce_module/limited.prx", image, parsed.metadata));
  const auto before_range =
      small_memory->IsMapped(small_memory->base_address() + 0x4000, 1);
  const auto limited_result = limited.LoadAdjacent(
      std::move(one_module),
      loader::BuildModuleStartPlan(
          {loader::MakeModuleDependencyInput("limited.prx", parsed.metadata)}),
      small_memory->end_address());
  Check(limited_result.status ==
                runtime::ModuleRuntimeStatus::kMemoryRangeExceeded &&
            !small_memory->IsMapped(small_memory->base_address() + 0x4000, 1) &&
            !before_range,
        "failed module layout changed guest mappings");

  Check(runtime::ModuleRuntimeStatusName(
            runtime::ModuleRuntimeStatus::kUnresolvedImports) ==
            "unresolved-imports",
        "module runtime status name is unstable");
  std::cout << "module runtime tests passed\n";
  return 0;
}
