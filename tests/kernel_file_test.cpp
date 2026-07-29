// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "kernel/event_flag.h"
#include "kernel/file.h"
#include "kernel/object.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::vector<std::byte> Bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const auto character : text) {
    result.push_back(static_cast<std::byte>(character));
  }
  return result;
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

int main() {
  using namespace kajps5::kernel;

  KernelRuntime runtime;
  auto &files = runtime.files();

  Check(!FileService::NormalizeGuestPath("relative/file"),
        "relative guest path was accepted");
  Check(!FileService::NormalizeGuestPath("/app0/../secret"),
        "parent traversal was accepted");
  Check(!FileService::NormalizeGuestPath("C:\\Windows\\file"),
        "host drive path was accepted");
  Check(!FileService::NormalizeGuestPath(std::string("/app0/a\0b", 9)),
        "embedded null was accepted");
  Check(!FileService::NormalizeGuestPath(std::string(1'025, '/')),
        "overlong guest path was accepted");
  Check(FileService::NormalizeGuestPath("/app0//data/./test.bin") ==
            "/app0/data/test.bin",
        "guest path normalization failed");

  Check(files.RegisterReadOnlyFile("/app0\\data//test.bin", Bytes("abcdef")) ==
            KernelStatus::kOk,
        "memory-backed file registration failed");
  Check(files.RegisterReadOnlyFile("/app0/data/test.bin", Bytes("other")) ==
            KernelStatus::kBusy,
        "duplicate file registration replaced data");
  Check(files.RegisterReadOnlyFile("relative", {}) ==
            KernelStatus::kInvalidArgument,
        "invalid registered path was accepted");
  Check(files.RegisterReadOnlyFile("/app0/data/Alpha.bin", Bytes("A")) ==
            KernelStatus::kOk &&
            files.RegisterReadOnlyFile("/app0/data/nested/leaf.bin",
                                       Bytes("N")) == KernelStatus::kOk,
        "directory fixture registration failed");

  Check(files.Open("/app0/data/test.bin", kFileOpenWrite).status ==
            KernelStatus::kPermissionDenied,
        "write open was accepted by the read-only service");
  Check(files.Open("/app0/data/test.bin", 3).status ==
            KernelStatus::kInvalidArgument,
        "unknown open flags were accepted");
  Check(files.Open("/app0/missing.bin", kFileOpenRead).status ==
            KernelStatus::kNotFound,
        "missing file was opened");
  Check(files.Open("/app0/data/test.bin", kFileOpenDirectory).status ==
            KernelStatus::kNotFound,
        "regular file opened as a directory");

  const auto directory =
      files.Open("/app0/data", kFileOpenDirectory);
  Check(static_cast<bool>(directory) &&
            runtime.handles().Find(directory.handle,
                                   KernelObjectType::kDirectory) != nullptr,
        "derived memory-backed directory did not open");
  const std::array<std::string_view, 5> expected_entries = {
      ".", "..", "Alpha.bin", "nested", "test.bin"};
  for (std::size_t index = 0; index < expected_entries.size(); ++index) {
    const auto entry = files.ReadDirectory(directory.handle);
    Check(entry && !entry.end_of_directory &&
              entry.entry.name == expected_entries[index] &&
              entry.entry.is_file == (index == 2 || index == 4) &&
              entry.entry.inode != 0,
          "directory entry order or type is incorrect");
  }
  const auto directory_end = files.ReadDirectory(directory.handle);
  Check(directory_end && directory_end.end_of_directory,
        "directory did not reach stable EOF");
  Check(files.Close(directory.handle) == KernelStatus::kOk &&
            files.ReadDirectory(directory.handle).status ==
                KernelStatus::kNotFound,
        "closed directory handle remained valid");

  const auto implicit_directory = files.Open("/app0", kFileOpenRead);
  Check(implicit_directory &&
            runtime.handles().Find(implicit_directory.handle,
                                   KernelObjectType::kDirectory) != nullptr,
        "directory without O_DIRECTORY did not open");
  Check(files.Close(implicit_directory.handle) == KernelStatus::kOk,
        "implicit directory close failed");

  const auto opened = files.Open("/app0//data/test.bin", kFileOpenRead);
  Check(static_cast<bool>(opened), "registered file did not open");
  Check(runtime.handles().Find(opened.handle, KernelObjectType::kFile) !=
            nullptr,
        "typed file lookup failed");
  Check(files.ReadDirectory(opened.handle).status ==
            KernelStatus::kInvalidArgument,
        "regular file was accepted by directory enumeration");

  const auto event = runtime.event_flags().Create("event", 0, 0);
  Check(static_cast<bool>(event), "event fixture creation failed");
  std::array<std::byte, 2> buffer{};
  Check(files.Read(event.handle, buffer).status == KernelStatus::kNotFound,
        "event handle was accepted as a file");

  auto io = files.Read(opened.handle, buffer);
  Check(io && io.value == 2 && Text(buffer) == "ab",
        "sequential read returned the wrong bytes");

  io = files.Pread(opened.handle, 4, buffer);
  Check(io && io.value == 2 && Text(buffer) == "ef",
        "positioned read returned the wrong bytes");
  io = files.Seek(opened.handle, 0, FileSeekWhence::kCurrent);
  Check(io && io.value == 2, "positioned read changed the file offset");
  Check(files.Pread(opened.handle, -1, buffer).status ==
            KernelStatus::kInvalidArgument,
        "negative positioned-read offset was accepted");

  io = files.Seek(opened.handle, -1, FileSeekWhence::kEnd);
  Check(io && io.value == 5, "end-relative seek failed");
  io = files.Read(opened.handle, buffer);
  Check(io && io.value == 1 && Text(std::span(buffer).first<1>()) == "f",
        "end-of-file read returned the wrong bytes");
  io = files.Read(opened.handle, buffer);
  Check(io && io.value == 0, "end-of-file read did not return zero");

  Check(files.Seek(opened.handle, -7, FileSeekWhence::kEnd).status ==
            KernelStatus::kInvalidArgument,
        "negative seek underflow was accepted");
  Check(files.Seek(opened.handle, 0, static_cast<FileSeekWhence>(99)).status ==
            KernelStatus::kInvalidArgument,
        "unknown seek origin was accepted");
  Check(static_cast<bool>(files.Seek(opened.handle,
                                     std::numeric_limits<std::int64_t>::max(),
                                     FileSeekWhence::kSet)),
        "large valid seek was rejected");
  Check(files.Seek(opened.handle, 1, FileSeekWhence::kCurrent).status ==
            KernelStatus::kInvalidArgument,
        "seek overflow was accepted");

  const auto stat = files.Stat("/app0/data/test.bin");
  const auto fstat = files.Fstat(opened.handle);
  Check(stat && stat.size == 6 && stat.inode != 0 && fstat &&
            fstat.size == 6 && fstat.inode == stat.inode,
        "file size metadata is incorrect");

  Check(files.Close(opened.handle) == KernelStatus::kOk, "file close failed");
  Check(files.Close(opened.handle) == KernelStatus::kNotFound,
        "closed handle remained valid");
  Check(files.Read(opened.handle, buffer).status == KernelStatus::kNotFound,
        "read accepted a closed handle");

  std::cout << "kernel file tests passed\n";
  return 0;
}
