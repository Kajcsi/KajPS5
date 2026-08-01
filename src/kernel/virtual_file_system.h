// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::size_t kMaximumGuestPathLength = 1'024;
inline constexpr std::uintmax_t kMaximumHostFileReadSize = 1ULL << 30;

struct VirtualFileReadResult {
  KernelStatus status = KernelStatus::kOk;
  std::vector<std::byte> contents;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct VirtualFileStatResult {
  KernelStatus status = KernelStatus::kOk;
  bool is_file = false;
  std::uintmax_t size = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct VirtualDirectoryEntry {
  std::string name;
  bool is_file = false;
};

struct VirtualDirectoryResult {
  KernelStatus status = KernelStatus::kOk;
  std::vector<VirtualDirectoryEntry> entries;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class VirtualFileSystem final {
public:
  VirtualFileSystem() = default;

  VirtualFileSystem(const VirtualFileSystem &) = delete;
  VirtualFileSystem &operator=(const VirtualFileSystem &) = delete;

  [[nodiscard]] KernelStatus MountReadOnly(std::string guest_root,
                                           std::filesystem::path host_root);
  [[nodiscard]] KernelStatus Unmount(std::string_view guest_root);

  [[nodiscard]] VirtualFileReadResult
  ReadFile(std::string_view guest_path,
           std::uintmax_t maximum_size = kMaximumHostFileReadSize) const;
  [[nodiscard]] VirtualFileStatResult Stat(std::string_view guest_path) const;
  [[nodiscard]] VirtualDirectoryResult
  ListDirectory(std::string_view guest_path) const;

  [[nodiscard]] static std::optional<std::string>
  NormalizeGuestPath(std::string_view path);

private:
  struct ResolvedPath {
    std::string normalized_guest_path;
    std::filesystem::path host_path;
  };

  [[nodiscard]] std::optional<ResolvedPath>
  ResolveExistingPath(std::string_view guest_path) const;

  mutable std::mutex mounts_mutex_;
  std::map<std::string, std::filesystem::path> mounts_;
};

} // namespace kajps5::kernel
