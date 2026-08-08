// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/title_loader.h"

#include "hle/call_context.h"
#include "hle/data_symbols.h"
#include "hle/kernel_exports.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kProgramOffset = 0x100;
constexpr std::uint64_t kProgramAddress = 0x1000;
constexpr std::uint64_t kProcessParametersOffset = 0x20;
constexpr std::uint64_t kProcessParametersAddress =
    kProgramAddress + kProcessParametersOffset;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "title_loader_test: " << message << '\n';
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

std::vector<std::byte> MakePublicRunElf() {
  constexpr std::size_t load_file_size = 0x30;
  constexpr std::size_t load_memory_size = 0x40;
  constexpr std::size_t program_header_size = 56;
  std::vector<std::byte> image(kProgramOffset + load_file_size);
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
  Write64(image, 24, kProgramAddress);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, 56);
  Write16(image, 56, 2);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 5);
  Write64(image, kProgramHeaderOffset + 8, kProgramOffset);
  Write64(image, kProgramHeaderOffset + 16, kProgramAddress);
  Write64(image, kProgramHeaderOffset + 32, load_file_size);
  Write64(image, kProgramHeaderOffset + 40, load_memory_size);
  Write64(image, kProgramHeaderOffset + 48, 0x100);

  const auto process_parameters_header =
      kProgramHeaderOffset + program_header_size;
  Write32(image, process_parameters_header, 0x61000001);
  Write32(image, process_parameters_header + 4, 4);
  Write64(image, process_parameters_header + 8,
          kProgramOffset + kProcessParametersOffset);
  Write64(image, process_parameters_header + 16, kProcessParametersAddress);
  Write64(image, process_parameters_header + 32, 0x10);
  Write64(image, process_parameters_header + 40, 0x10);
  Write64(image, process_parameters_header + 48, 8);

  image[kProgramOffset] = std::byte{0xb8};
  image[kProgramOffset + 1] = std::byte{42};
  image[kProgramOffset + 2] = std::byte{0};
  image[kProgramOffset + 3] = std::byte{0};
  image[kProgramOffset + 4] = std::byte{0};
  image[kProgramOffset + 5] = std::byte{0xc3};
  return image;
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(_M_X64) && !defined(__x86_64__)
  return 0;
#else
  using kajps5::runtime::PrepareTitleImage;
  using kajps5::runtime::PrepareTitleImageWithModules;
  using kajps5::runtime::TitleLoadStatus;
  using kajps5::runtime::TitleSessionPhase;
  using kajps5::runtime::TitleSessionStatus;

  const auto image = MakePublicRunElf();
  auto prepared = PrepareTitleImage(image, "public-run.elf");
  if (!prepared) {
    std::cerr << "title_loader_test: preparation status="
              << kajps5::runtime::TitleLoadStatusName(prepared.status)
              << " elf=" << kajps5::loader::ElfErrorName(prepared.elf_error)
              << " launch="
              << kajps5::loader::LaunchMetadataStatusName(
                     prepared.launch_status)
              << " hle="
              << kajps5::runtime::TitleHleSetupStatusName(prepared.hle.status)
              << '\n';
  }
  Check(
      prepared && prepared.load_bias != 0 && prepared.stack_search_start != 0 &&
          prepared.coverage.unresolved_unique_import_count == 0 &&
          prepared.relocation.status == kajps5::loader::RelocationStatus::kOk &&
          prepared.lifecycle.status ==
              kajps5::loader::LifecyclePlanStatus::kOk &&
          prepared.session->memory().CanExecute(
              kProgramAddress + prepared.load_bias, 1),
      "public title image preparation failed");
  Check(prepared.session->kernel_runtime().process_parameters_address() ==
                prepared.load_bias + kProcessParametersAddress,
        "title loader did not configure the process-parameter guest address");
  const auto pthread_arena = prepared.hle.data.page_address +
                             kajps5::hle::kHleDataPageSize;
  Check(prepared.session->memory().CanAccess(
            pthread_arena, kajps5::kernel::kPthreadMutexArenaSize,
            kajps5::memory::GuestMemoryProtection::kWrite) &&
            prepared.stack_search_start ==
                pthread_arena + kajps5::kernel::kPthreadMutexArenaSize &&
            !prepared.session->memory().IsMapped(
                prepared.stack_search_start, 1),
        "title loader overlapped the pthread arena with stack search space");
  kajps5::hle::HleCallContext hle_data_context(prepared.session->memory());
  std::uint64_t malloc_replace_size = 0;
  std::uint64_t new_replace_size = 0;
  Check(prepared.session->kernel_runtime().sanitizer_malloc_replace_address() ==
                prepared.hle.data.sanitizer_malloc_replace_address &&
            prepared.session->kernel_runtime().sanitizer_new_replace_address() ==
                prepared.hle.data.sanitizer_new_replace_address &&
            prepared.session->memory().CanAccess(
                prepared.hle.data.sanitizer_malloc_replace_address,
                kajps5::hle::kHleSanitizerMallocReplaceSize,
                kajps5::memory::GuestMemoryProtection::kWrite) &&
            prepared.session->memory().CanAccess(
                prepared.hle.data.sanitizer_new_replace_address,
                kajps5::hle::kHleSanitizerNewReplaceSize,
                kajps5::memory::GuestMemoryProtection::kWrite) &&
            hle_data_context.ReadUInt64(
                prepared.hle.data.sanitizer_malloc_replace_address,
                malloc_replace_size) == kajps5::hle::HleContextStatus::kOk &&
            hle_data_context.ReadUInt64(
                prepared.hle.data.sanitizer_new_replace_address,
                new_replace_size) == kajps5::hle::HleContextStatus::kOk &&
            malloc_replace_size == kajps5::hle::kHleSanitizerMallocReplaceSize &&
            new_replace_size == kajps5::hle::kHleSanitizerNewReplaceSize,
        "title loader did not retain sanitizer replacement guest objects");
  const std::vector<std::string> libraries = {"libkernel"};
  kajps5::hle::HleCallContext proc_param_context(prepared.session->memory());
  Check(prepared.session->hle_exports()
                .Dispatch(kajps5::hle::kKernelGetProcParamNid, libraries,
                          proc_param_context) &&
            proc_param_context.GetRegister(kajps5::hle::HleRegister::kRax)
                    .value_or(0) ==
                prepared.load_bias + kProcessParametersAddress,
        "title HLE proc-param NID did not return the load-biased address");
  const auto started =
      prepared.session->Start("public-run.elf", prepared.stack_search_start);
  Check(started.status == TitleSessionStatus::kPending &&
            started.phase == TitleSessionPhase::kRunning,
        "prepared title did not create its main thread");
  const auto completed = prepared.session->Run(8);
  Check(completed && completed.exit_value == 42 && completed.slices == 1,
        "public title did not execute through the title session");

  const auto parsed_module = kajps5::loader::ParseExecutable64(image);
  Check(static_cast<bool>(parsed_module),
        "adjacent module fixture did not parse");
  kajps5::loader::AdjacentModuleLoadResult adjacent;
  adjacent.modules.push_back(
      {"/app0/sce_module/public-module.prx", image, parsed_module.metadata});
  adjacent.start_plan = kajps5::loader::BuildModuleStartPlan(
      {kajps5::loader::MakeModuleDependencyInput(
          adjacent.modules.front().guest_path,
          adjacent.modules.front().metadata)});
  auto prepared_with_module = PrepareTitleImageWithModules(
      image, "public-run.elf", std::move(adjacent));
  Check(prepared_with_module &&
            prepared_with_module.adjacent_module_count == 1 &&
            prepared_with_module.session->module_runtime() != nullptr &&
            prepared_with_module.session->module_runtime()->programs().size() ==
                2 &&
            prepared_with_module.session->module_runtime()
                    ->programs()[0]
                    .module_id == 1 &&
            prepared_with_module.session->module_runtime()
                    ->programs()[1]
                    .module_id == 2 &&
            prepared_with_module.session->module_runtime()
                    ->programs()[0]
                    .load_bias != prepared_with_module.session->module_runtime()
                                      ->programs()[1]
                                      .load_bias,
        "adjacent module was not retained in the title session");
  const auto module_started = prepared_with_module.session->Start(
      "public-run.elf", prepared_with_module.stack_search_start);
  Check(module_started.status == TitleSessionStatus::kPending,
        "multi-module title did not create its main thread");
  const auto module_completed = prepared_with_module.session->Run(8);
  Check(module_completed && module_completed.exit_value == 42,
        "multi-module title did not execute its main program");

  kajps5::loader::AdjacentModuleLoadResult malformed_adjacent;
  malformed_adjacent.modules.push_back(
      {"/app0/sce_module/malformed.prx", {std::byte{0}}, {}});
  const auto malformed_module = PrepareTitleImageWithModules(
      image, "public-run.elf", std::move(malformed_adjacent));
  Check(malformed_module.status ==
                TitleLoadStatus::kAdjacentModuleInputFailed &&
            malformed_module.adjacent_status ==
                kajps5::loader::AdjacentModuleLoadStatus::kParseFailed &&
            malformed_module.session == nullptr,
        "malformed adjacent input exposed a partial title session");

  auto invalid = image;
  invalid[0] = std::byte{0};
  Check(PrepareTitleImage(invalid, "invalid.elf").status ==
            TitleLoadStatus::kParseFailed,
        "invalid title did not fail parsing");
  Check(
      PrepareTitleImage(image, "").status == TitleLoadStatus::kInvalidArgument,
      "empty process name was accepted");
  Check(PrepareTitleImage(image, "limited.elf", 0x1000).status ==
            TitleLoadStatus::kMemoryLimitExceeded,
        "title runtime memory limit was ignored");
  auto no_load = image;
  Write16(no_load, 56, 0);
  Check(PrepareTitleImage(no_load, "empty.elf").status ==
            TitleLoadStatus::kNoLoadSegments,
        "title without load segments reached runtime setup");
  Check(kajps5::runtime::TitleLoadStatusName(
            TitleLoadStatus::kUnresolvedImports) == "unresolved-imports" &&
            kajps5::runtime::TitleLoadStatusName(
                TitleLoadStatus::kModuleRuntimeFailed) ==
                "module-runtime-failed" &&
            kajps5::runtime::TitleLoadStatusName(
                TitleLoadStatus::kStaticTlsExecutionUnsupported) ==
                "static-tls-execution-unsupported",
        "title load status names are unstable");

  Check(argc == 2, "CLI executable path was not supplied");
  const auto fixture_path =
      std::filesystem::current_path() / "kajps5-public-run.elf";
  const auto module_directory =
      std::filesystem::current_path() / "kajps5-public-modules";
  std::error_code setup_error;
  std::filesystem::remove_all(module_directory, setup_error);
  setup_error.clear();
  Check(std::filesystem::create_directory(module_directory, setup_error) &&
            !setup_error,
        "public CLI module directory could not be created");
  {
    std::ofstream fixture(fixture_path, std::ios::binary | std::ios::trunc);
    Check(static_cast<bool>(fixture), "public CLI fixture could not be opened");
    fixture.write(reinterpret_cast<const char*>(image.data()),
                  static_cast<std::streamsize>(image.size()));
    Check(static_cast<bool>(fixture),
          "public CLI fixture could not be written");
  }
  {
    std::ofstream module(module_directory / "public-module.prx",
                         std::ios::binary | std::ios::trunc);
    Check(static_cast<bool>(module), "public CLI module could not be opened");
    module.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    Check(static_cast<bool>(module), "public CLI module could not be written");
  }
#if defined(_WIN32)
  const std::string command = "\"\"" + std::string(argv[1]) +
                              "\" --run-elf \"" + fixture_path.string() +
                              "\" --headless --module-dir \"" +
                              module_directory.string() + "\"\"";
#else
  const std::string command = '"' + std::string(argv[1]) + "\" --run-elf \"" +
                              fixture_path.string() + "\" --headless --module-dir \"" +
                              module_directory.string() + '"';
#endif
  const auto cli_status = std::system(command.c_str());
  std::error_code remove_error;
  std::filesystem::remove(fixture_path, remove_error);
  remove_error.clear();
  std::filesystem::remove_all(module_directory, remove_error);
  if (cli_status != 0) {
    std::cerr << "title_loader_test: CLI command=" << command << '\n';
  }
  Check(cli_status == 0, "public title CLI run failed");
  return 0;
#endif
}
