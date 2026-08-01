// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/file.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace kajps5::kernel {
namespace {

std::uint32_t StablePathHash(std::string_view path) noexcept {
  constexpr std::uint32_t kOffsetBasis = 2'166'136'261U;
  constexpr std::uint32_t kPrime = 16'777'619U;
  auto hash = kOffsetBasis;
  for (const auto character : path) {
    hash ^= static_cast<unsigned char>(character);
    hash *= kPrime;
  }
  return hash;
}

unsigned char FoldAscii(unsigned char value) noexcept {
  return value >= 'A' && value <= 'Z'
             ? static_cast<unsigned char>(value + ('a' - 'A'))
             : value;
}

bool CaseInsensitiveLess(std::string_view left, std::string_view right) {
  const auto common = std::min(left.size(), right.size());
  for (std::size_t index = 0; index < common; ++index) {
    const auto left_folded = FoldAscii(
        static_cast<unsigned char>(left[index]));
    const auto right_folded = FoldAscii(
        static_cast<unsigned char>(right[index]));
    if (left_folded != right_folded) {
      return left_folded < right_folded;
    }
  }
  return left.size() != right.size() ? left.size() < right.size()
                                     : left < right;
}

}  // namespace

File::File(std::string path,
           std::shared_ptr<const std::vector<std::byte>> contents)
    : KernelObject(KernelObjectType::kFile), path_(std::move(path)),
      contents_(std::move(contents)) {}

const std::string &File::path() const noexcept { return path_; }

std::uint64_t File::size() const noexcept { return contents_->size(); }

std::uint64_t File::position() const {
  std::lock_guard lock(mutex_);
  return position_;
}

std::size_t File::Read(std::span<std::byte> destination) {
  std::lock_guard lock(mutex_);
  const auto bytes_read = ReadAt(position_, destination);
  position_ += bytes_read;
  return bytes_read;
}

std::size_t File::Pread(std::uint64_t offset,
                        std::span<std::byte> destination) const {
  return ReadAt(offset, destination);
}

std::optional<std::uint64_t> File::Seek(std::int64_t offset,
                                        FileSeekWhence whence) {
  std::lock_guard lock(mutex_);

  std::uint64_t base = 0;
  switch (whence) {
  case FileSeekWhence::kSet:
    break;
  case FileSeekWhence::kCurrent:
    base = position_;
    break;
  case FileSeekWhence::kEnd:
    base = contents_->size();
    break;
  default:
    return std::nullopt;
  }

  std::uint64_t result = 0;
  if (!AddOffset(base, offset, result)) {
    return std::nullopt;
  }
  position_ = result;
  return result;
}

bool File::AddOffset(std::uint64_t base, std::int64_t offset,
                     std::uint64_t &result) noexcept {
  if (offset >= 0) {
    const auto positive = static_cast<std::uint64_t>(offset);
    constexpr auto kMaximumPosition =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (base > kMaximumPosition || positive > kMaximumPosition - base) {
      return false;
    }
    result = base + positive;
    return true;
  }

  const auto magnitude =
      static_cast<std::uint64_t>(-(offset + 1)) + std::uint64_t{1};
  if (magnitude > base) {
    return false;
  }
  result = base - magnitude;
  return true;
}

std::size_t File::ReadAt(std::uint64_t offset,
                         std::span<std::byte> destination) const {
  if (offset >= contents_->size() || destination.empty()) {
    return 0;
  }

  const auto start = static_cast<std::size_t>(offset);
  const auto count = std::min(destination.size(), contents_->size() - start);
  std::copy_n(contents_->data() + start, count, destination.data());
  return count;
}

Directory::Directory(std::string path, std::vector<DirectoryEntry> entries)
    : KernelObject(KernelObjectType::kDirectory), path_(std::move(path)),
      entries_(std::move(entries)) {}

const std::string &Directory::path() const noexcept { return path_; }

DirectoryReadResult Directory::ReadNext() {
  std::lock_guard lock(mutex_);
  if (next_index_ >= entries_.size()) {
    return {KernelStatus::kOk, true, next_index_, {}};
  }
  const auto position = next_index_;
  return {KernelStatus::kOk, false, position, entries_[next_index_++]};
}

FileService::FileService(HandleTable &handles) noexcept : handles_(handles) {}

KernelStatus
FileService::RegisterReadOnlyFile(std::string path,
                                  std::vector<std::byte> contents) {
  const auto normalized = NormalizeGuestPath(path);
  if (!normalized || *normalized == "/") {
    return KernelStatus::kInvalidArgument;
  }

  auto data =
      std::make_shared<const std::vector<std::byte>>(std::move(contents));
  std::lock_guard lock(files_mutex_);
  const auto inserted = files_.emplace(*normalized, std::move(data)).second;
  return inserted ? KernelStatus::kOk : KernelStatus::kBusy;
}

KernelStatus FileService::MountReadOnly(std::string guest_root,
                                        std::filesystem::path host_root) {
  return host_files_.MountReadOnly(std::move(guest_root),
                                   std::move(host_root));
}

KernelStatus FileService::Unmount(std::string_view guest_root) {
  return host_files_.Unmount(guest_root);
}

FileOpenResult FileService::Open(std::string_view path, std::uint32_t flags) {
  constexpr std::uint32_t kAccessMask = 3;
  if ((flags & ~(kAccessMask | kFileOpenDirectory)) != 0 ||
      (flags & kAccessMask) > kFileOpenReadWrite) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  const auto normalized = NormalizeGuestPath(path);
  if (!normalized) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  const auto access = flags & kAccessMask;
  const auto wants_directory = (flags & kFileOpenDirectory) != 0;
  std::shared_ptr<const std::vector<std::byte>> contents;
  std::vector<DirectoryEntry> directory_entries;
  bool directory_exists = false;
  bool has_memory_file = false;
  {
    std::lock_guard lock(files_mutex_);
    const auto found = files_.find(*normalized);
    if (found != files_.end()) {
      contents = found->second;
      has_memory_file = true;
    }
    if (wants_directory || !contents) {
      directory_entries =
          CollectDirectoryEntriesLocked(*normalized, directory_exists);
    }
  }

  if (!has_memory_file) {
    const auto host_stat = host_files_.Stat(*normalized);
    if (host_stat && host_stat.is_file && !directory_exists &&
        !wants_directory) {
      if (access != kFileOpenRead) {
        return {KernelStatus::kPermissionDenied, kInvalidKernelHandle};
      }
      auto host_file = host_files_.ReadFile(*normalized);
      if (!host_file) {
        return {host_file.status, kInvalidKernelHandle};
      }
      contents = std::make_shared<const std::vector<std::byte>>(
          std::move(host_file.contents));
    } else if (host_stat && !host_stat.is_file) {
      const auto host_directory = host_files_.ListDirectory(*normalized);
      if (!host_directory) {
        return {host_directory.status, kInvalidKernelHandle};
      }
      if (!directory_exists) {
        directory_entries = {
            {".", false, StablePathHash(".")},
            {"..", false, StablePathHash("..")},
        };
      }
      directory_exists = true;
      for (const auto &entry : host_directory.entries) {
        const auto duplicate = std::find_if(
            directory_entries.begin(), directory_entries.end(),
            [&entry](const auto &existing) {
              return existing.name == entry.name;
            });
        if (duplicate == directory_entries.end()) {
          directory_entries.push_back(
              {entry.name, entry.is_file, StablePathHash(entry.name)});
        }
      }
      std::sort(directory_entries.begin() + 2, directory_entries.end(),
                [](const auto &left, const auto &right) {
                  return CaseInsensitiveLess(left.name, right.name);
                });
    }
  }

  if (wants_directory || !contents) {
    if (!directory_exists) {
      return {KernelStatus::kNotFound, kInvalidKernelHandle};
    }
    if (access != kFileOpenRead) {
      return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
    }
    auto directory = std::make_shared<Directory>(
        *normalized, std::move(directory_entries));
    const auto handle = handles_.Insert(std::move(directory));
    return handle ? FileOpenResult{KernelStatus::kOk, *handle}
                  : FileOpenResult{KernelStatus::kNoResources,
                                   kInvalidKernelHandle};
  }
  if (access != kFileOpenRead) {
    return {KernelStatus::kPermissionDenied, kInvalidKernelHandle};
  }

  auto file = std::make_shared<File>(*normalized, std::move(contents));
  const auto handle = handles_.Insert(std::move(file));
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }
  return {KernelStatus::kOk, *handle};
}

KernelStatus FileService::Close(KernelHandle handle) {
  if (handles_.Remove(handle, KernelObjectType::kFile) ||
      handles_.Remove(handle, KernelObjectType::kDirectory)) {
    return KernelStatus::kOk;
  }
  return KernelStatus::kNotFound;
}

FileIoResult FileService::Read(KernelHandle handle,
                               std::span<std::byte> destination) {
  const auto file = Find(handle);
  if (!file) {
    return {KernelStatus::kNotFound, 0};
  }
  return {KernelStatus::kOk, file->Read(destination)};
}

FileIoResult FileService::Pread(KernelHandle handle, std::int64_t offset,
                                std::span<std::byte> destination) const {
  const auto file = Find(handle);
  if (!file) {
    return {KernelStatus::kNotFound, 0};
  }
  if (offset < 0) {
    return {KernelStatus::kInvalidArgument, 0};
  }
  return {KernelStatus::kOk,
          file->Pread(static_cast<std::uint64_t>(offset), destination)};
}

FileIoResult FileService::Seek(KernelHandle handle, std::int64_t offset,
                               FileSeekWhence whence) {
  const auto file = Find(handle);
  if (!file) {
    return {KernelStatus::kNotFound, 0};
  }
  const auto position = file->Seek(offset, whence);
  return position ? FileIoResult{KernelStatus::kOk, *position}
                  : FileIoResult{KernelStatus::kInvalidArgument, 0};
}

DirectoryReadResult FileService::ReadDirectory(KernelHandle handle) {
  const auto directory = FindDirectory(handle);
  if (!directory) {
    return {Find(handle) ? KernelStatus::kInvalidArgument
                         : KernelStatus::kNotFound,
            false, 0, {}};
  }
  return directory->ReadNext();
}

FileStatResult FileService::Stat(std::string_view path) const {
  const auto normalized = NormalizeGuestPath(path);
  if (!normalized) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }

  {
    std::lock_guard lock(files_mutex_);
    const auto found = files_.find(*normalized);
    if (found != files_.end()) {
      return {KernelStatus::kOk, found->second->size(),
              StablePathHash(*normalized)};
    }
  }
  const auto host_stat = host_files_.Stat(*normalized);
  return host_stat && host_stat.is_file
             ? FileStatResult{KernelStatus::kOk, host_stat.size,
                              StablePathHash(*normalized)}
             : FileStatResult{KernelStatus::kNotFound, 0, 0};
}

FileStatResult FileService::Fstat(KernelHandle handle) const {
  const auto file = Find(handle);
  return file ? FileStatResult{KernelStatus::kOk, file->size(),
                               StablePathHash(file->path())}
              : FileStatResult{KernelStatus::kNotFound, 0, 0};
}

std::optional<std::string>
FileService::NormalizeGuestPath(std::string_view path) {
  return VirtualFileSystem::NormalizeGuestPath(path);
}

std::shared_ptr<File> FileService::Find(KernelHandle handle) const {
  return std::static_pointer_cast<File>(
      handles_.Find(handle, KernelObjectType::kFile));
}

std::shared_ptr<Directory>
FileService::FindDirectory(KernelHandle handle) const {
  return std::static_pointer_cast<Directory>(
      handles_.Find(handle, KernelObjectType::kDirectory));
}

std::vector<DirectoryEntry> FileService::CollectDirectoryEntriesLocked(
    std::string_view path, bool &exists) const {
  exists = path == "/";
  const auto prefix = path == "/" ? std::string("/")
                                  : std::string(path) + "/";
  std::map<std::string, bool> children;
  for (const auto &[file_path, contents] : files_) {
    (void)contents;
    if (!file_path.starts_with(prefix)) {
      continue;
    }
    const auto remainder =
        std::string_view(file_path).substr(prefix.size());
    if (remainder.empty()) {
      continue;
    }
    exists = true;
    const auto separator = remainder.find('/');
    const auto name = std::string(remainder.substr(0, separator));
    const auto is_file = separator == std::string_view::npos;
    const auto [entry, inserted] = children.emplace(name, is_file);
    if (!inserted && !is_file) {
      entry->second = false;
    }
  }

  std::vector<DirectoryEntry> entries = {
      {".", false, StablePathHash(".")},
      {"..", false, StablePathHash("..")},
  };
  if (!exists) {
    return {};
  }
  std::vector<std::pair<std::string, bool>> ordered(children.begin(),
                                                    children.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto &left,
                                                const auto &right) {
    return CaseInsensitiveLess(left.first, right.first);
  });
  entries.reserve(entries.size() + ordered.size());
  for (auto &[name, is_file] : ordered) {
    const auto inode = StablePathHash(name);
    entries.push_back({std::move(name), is_file, inode});
  }
  return entries;
}

} // namespace kajps5::kernel
