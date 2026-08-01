// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/title_loader.h"

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
  constexpr std::size_t code_size = 6;
  std::vector<std::byte> image(kProgramOffset + code_size);
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
  Write16(image, 56, 1);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 5);
  Write64(image, kProgramHeaderOffset + 8, kProgramOffset);
  Write64(image, kProgramHeaderOffset + 16, kProgramAddress);
  Write64(image, kProgramHeaderOffset + 32, code_size);
  Write64(image, kProgramHeaderOffset + 40, code_size);
  Write64(image, kProgramHeaderOffset + 48, 0x100);

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
  const auto started =
      prepared.session->Start("public-run.elf", prepared.stack_search_start);
  Check(started.status == TitleSessionStatus::kPending &&
            started.phase == TitleSessionPhase::kRunning,
        "prepared title did not create its main thread");
  const auto completed = prepared.session->Run(8);
  Check(completed && completed.exit_value == 42 && completed.slices == 1,
        "public title did not execute through the title session");

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
                TitleLoadStatus::kStaticTlsExecutionUnsupported) ==
                "static-tls-execution-unsupported",
        "title load status names are unstable");

  Check(argc == 2, "CLI executable path was not supplied");
  const auto fixture_path =
      std::filesystem::current_path() / "kajps5-public-run.elf";
  {
    std::ofstream fixture(fixture_path, std::ios::binary | std::ios::trunc);
    Check(static_cast<bool>(fixture), "public CLI fixture could not be opened");
    fixture.write(reinterpret_cast<const char*>(image.data()),
                  static_cast<std::streamsize>(image.size()));
    Check(static_cast<bool>(fixture),
          "public CLI fixture could not be written");
  }
#if defined(_WIN32)
  const std::string command = "\"\"" + std::string(argv[1]) +
                              "\" --run-elf \"" + fixture_path.string() +
                              "\"\"";
#else
  const std::string command = '"' + std::string(argv[1]) + "\" --run-elf \"" +
                              fixture_path.string() + '"';
#endif
  const auto cli_status = std::system(command.c_str());
  std::error_code remove_error;
  std::filesystem::remove(fixture_path, remove_error);
  if (cli_status != 0) {
    std::cerr << "title_loader_test: CLI command=" << command << '\n';
  }
  Check(cli_status == 0, "public title CLI run failed");
  return 0;
#endif
}
