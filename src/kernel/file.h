// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kernel/handle_table.h"
#include "kernel/object.h"
#include "kernel/status.h"
#include "kernel/virtual_file_system.h"

namespace kajps5::kernel {

inline constexpr std::uint32_t kFileOpenRead = 0;
inline constexpr std::uint32_t kFileOpenWrite = 1;
inline constexpr std::uint32_t kFileOpenReadWrite = 2;
inline constexpr std::uint32_t kFileOpenDirectory = 0x00020000;

enum class FileSeekWhence {
  kSet,
  kCurrent,
  kEnd,
};

class File final : public KernelObject {
public:
  File(std::string path,
       std::shared_ptr<const std::vector<std::byte>> contents);

  [[nodiscard]] const std::string &path() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] std::uint64_t position() const;
  [[nodiscard]] std::size_t Read(std::span<std::byte> destination);
  [[nodiscard]] std::size_t Pread(std::uint64_t offset,
                                  std::span<std::byte> destination) const;
  [[nodiscard]] std::optional<std::uint64_t> Seek(std::int64_t offset,
                                                  FileSeekWhence whence);

private:
  [[nodiscard]] static bool AddOffset(std::uint64_t base, std::int64_t offset,
                                      std::uint64_t &result) noexcept;
  [[nodiscard]] std::size_t ReadAt(std::uint64_t offset,
                                   std::span<std::byte> destination) const;

  std::string path_;
  std::shared_ptr<const std::vector<std::byte>> contents_;
  mutable std::mutex mutex_;
  std::uint64_t position_ = 0;
};

struct DirectoryEntry {
  std::string name;
  bool is_file = false;
  std::uint32_t inode = 0;
};

struct DirectoryReadResult {
  KernelStatus status = KernelStatus::kOk;
  bool end_of_directory = false;
  std::uint64_t position = 0;
  DirectoryEntry entry;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class Directory final : public KernelObject {
public:
  Directory(std::string path, std::vector<DirectoryEntry> entries);

  [[nodiscard]] const std::string &path() const noexcept;
  [[nodiscard]] DirectoryReadResult ReadNext();

private:
  std::string path_;
  std::vector<DirectoryEntry> entries_;
  std::mutex mutex_;
  std::size_t next_index_ = 0;
};

struct FileOpenResult {
  KernelStatus status = KernelStatus::kOk;
  KernelHandle handle = kInvalidKernelHandle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct FileIoResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct FileStatResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t size = 0;
  std::uint32_t inode = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class FileService final {
public:
  explicit FileService(HandleTable &handles) noexcept;

  FileService(const FileService &) = delete;
  FileService &operator=(const FileService &) = delete;

  [[nodiscard]] KernelStatus
  RegisterReadOnlyFile(std::string path, std::vector<std::byte> contents);
  [[nodiscard]] KernelStatus
  MountReadOnly(std::string guest_root, std::filesystem::path host_root);
  [[nodiscard]] KernelStatus Unmount(std::string_view guest_root);
  [[nodiscard]] FileOpenResult Open(std::string_view path, std::uint32_t flags);
  [[nodiscard]] KernelStatus Close(KernelHandle handle);
  [[nodiscard]] FileIoResult Read(KernelHandle handle,
                                  std::span<std::byte> destination);
  [[nodiscard]] FileIoResult Pread(KernelHandle handle, std::int64_t offset,
                                   std::span<std::byte> destination) const;
  [[nodiscard]] FileIoResult Seek(KernelHandle handle, std::int64_t offset,
                                  FileSeekWhence whence);
  [[nodiscard]] DirectoryReadResult ReadDirectory(KernelHandle handle);
  [[nodiscard]] FileStatResult Stat(std::string_view path) const;
  [[nodiscard]] FileStatResult Fstat(KernelHandle handle) const;

  [[nodiscard]] static std::optional<std::string>
  NormalizeGuestPath(std::string_view path);

private:
  [[nodiscard]] std::shared_ptr<File> Find(KernelHandle handle) const;
  [[nodiscard]] std::shared_ptr<Directory>
  FindDirectory(KernelHandle handle) const;
  [[nodiscard]] std::vector<DirectoryEntry>
  CollectDirectoryEntriesLocked(std::string_view path,
                                bool &exists) const;

  HandleTable &handles_;
  mutable std::mutex files_mutex_;
  std::map<std::string, std::shared_ptr<const std::vector<std::byte>>> files_;
  VirtualFileSystem host_files_;
};

} // namespace kajps5::kernel
