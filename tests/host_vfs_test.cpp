// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "kernel/file.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

class TemporaryTree final {
public:
  TemporaryTree() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::current_path() /
            ("kajps5-host-vfs-" + std::to_string(suffix));
    std::error_code error;
    Check(std::filesystem::create_directories(root_, error) && !error,
          "temporary host root creation failed");
  }

  ~TemporaryTree() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  [[nodiscard]] const std::filesystem::path &root() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

void WriteFile(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream stream(path, std::ios::binary);
  Check(static_cast<bool>(stream), "host fixture file did not open");
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  Check(static_cast<bool>(stream), "host fixture file write failed");
}

std::string Text(std::span<const std::byte> bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (const auto value : bytes) {
    result.push_back(static_cast<char>(value));
  }
  return result;
}

} // namespace

int RunTests() {
  using namespace kajps5::kernel;

  TemporaryTree tree;
  const auto app0 = tree.root() / "app0";
  const auto modules = app0 / "sce_module";
  const auto outside = tree.root() / "outside";
  std::error_code error;
  Check(std::filesystem::create_directories(modules, error) && !error,
        "module directory fixture creation failed");
  Check(std::filesystem::create_directories(outside, error) && !error,
        "outside directory fixture creation failed");
  WriteFile(app0 / "host.bin", "host-data");
  WriteFile(app0 / "Data.bin", "case-data");
  WriteFile(modules / "zeta.prx", "z");
  WriteFile(modules / "Alpha.prx", "a");
  WriteFile(outside / "secret.bin", "secret");

  KernelRuntime runtime;
  auto &files = runtime.files();
  Check(files.RegisterReadOnlyFile("/app0/sce_module/Beta.prx",
                                   {std::byte{'b'}}) == KernelStatus::kOk,
        "memory-backed module fixture registration failed");
  Check(files.MountReadOnly("/app0", tree.root() / "missing") ==
            KernelStatus::kNotFound,
        "missing host root was mounted");
  Check(files.MountReadOnly("/app0", app0) == KernelStatus::kOk,
        "read-only app0 mount failed");
  Check(files.MountReadOnly("/app0", app0) == KernelStatus::kBusy,
        "duplicate mount was accepted");

  const auto opened = files.Open("/app0/host.bin", kFileOpenRead);
  Check(static_cast<bool>(opened), "host-backed file did not open");
  std::array<std::byte, 16> bytes{};
  const auto read = files.Read(opened.handle, bytes);
  Check(read && read.value == 9 &&
            Text(std::span(bytes).first(9)) == "host-data",
        "host-backed file returned the wrong bytes");
  Check(files.Fstat(opened.handle).size == 9,
        "host-backed descriptor size is wrong");
  Check(files.Close(opened.handle) == KernelStatus::kOk,
        "host-backed file close failed");
  Check(files.Stat("/app0/host.bin").size == 9,
        "host-backed path size is wrong");
  Check(files.Open("/app0/host.bin", kFileOpenWrite).status ==
            KernelStatus::kPermissionDenied,
        "host-backed write open was accepted");

  const auto directory = files.Open("/app0/sce_module", kFileOpenDirectory);
  Check(static_cast<bool>(directory),
        "host-backed module directory did not open");
  const std::array<std::string_view, 5> expected = {".", "..", "Alpha.prx",
                                                    "Beta.prx", "zeta.prx"};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto entry = files.ReadDirectory(directory.handle);
    Check(entry && !entry.end_of_directory &&
              entry.entry.name == expected[index] &&
              entry.entry.is_file == (index >= 2),
          "host-backed directory order or type is wrong");
  }
  Check(files.ReadDirectory(directory.handle).end_of_directory,
        "host-backed directory did not reach EOF");
  Check(files.Close(directory.handle) == KernelStatus::kOk,
        "host-backed directory close failed");

  Check(files.Open("/etc/passwd", kFileOpenRead).status ==
            KernelStatus::kNotFound,
        "unmounted absolute path reached the host");
  Check(files.Open("/app0/../outside/secret.bin", kFileOpenRead).status ==
            KernelStatus::kInvalidArgument,
        "parent traversal was not rejected");
  Check(files.Open("/app0/C:/Windows/secret.bin", kFileOpenRead).status ==
            KernelStatus::kNotFound,
        "drive-qualified component escaped the mount");
  Check(files.Open(std::string("/app0/bad\0name", 14), kFileOpenRead).status ==
            KernelStatus::kInvalidArgument,
        "malformed mounted path was accepted");
  std::string invalid_utf8 = "/app0/";
  invalid_utf8.push_back(static_cast<char>(0xff));
  Check(files.Open(invalid_utf8, kFileOpenRead).status ==
            KernelStatus::kNotFound,
        "invalid host path encoding did not fail closed");

  if (!std::filesystem::exists(app0 / "DATA.BIN")) {
    Check(files.Open("/app0/DATA.BIN", kFileOpenRead).status ==
              KernelStatus::kNotFound,
          "host case-sensitive miss shadowed another path");
    const auto correctly_cased = files.Open("/app0/Data.bin", kFileOpenRead);
    Check(static_cast<bool>(correctly_cased),
          "correctly cased host path did not open");
    Check(files.Close(correctly_cased.handle) == KernelStatus::kOk,
          "correctly cased host file close failed");
  }

  error.clear();
  std::filesystem::create_directory_symlink(outside, app0 / "link", error);
  if (!error) {
    Check(std::filesystem::exists(app0 / "link" / "secret.bin"),
          "symlink escape fixture did not resolve on the host");
    Check(files.Open("/app0/link/secret.bin", kFileOpenRead).status ==
              KernelStatus::kNotFound,
          "symlink escaped the app0 mount");
  }

  Check(files.Unmount("/app0") == KernelStatus::kOk, "app0 unmount failed");
  Check(files.Open("/app0/host.bin", kFileOpenRead).status ==
            KernelStatus::kNotFound,
        "unmounted host file remained reachable");
  Check(files.Unmount("/app0") == KernelStatus::kNotFound,
        "missing mount unmount succeeded");

  std::cout << "host VFS tests passed\n";
  return 0;
}

int main() {
  try {
    return RunTests();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: unexpected host VFS exception: " << error.what()
              << '\n';
    return 1;
  }
}
