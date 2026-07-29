// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/file.h"

#include <algorithm>
#include <limits>
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

FileOpenResult FileService::Open(std::string_view path, std::uint32_t flags) {
  if (flags > kFileOpenReadWrite) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }
  if (flags != kFileOpenRead) {
    return {KernelStatus::kPermissionDenied, kInvalidKernelHandle};
  }

  const auto normalized = NormalizeGuestPath(path);
  if (!normalized) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  std::shared_ptr<const std::vector<std::byte>> contents;
  {
    std::lock_guard lock(files_mutex_);
    const auto found = files_.find(*normalized);
    if (found == files_.end()) {
      return {KernelStatus::kNotFound, kInvalidKernelHandle};
    }
    contents = found->second;
  }

  auto file = std::make_shared<File>(*normalized, std::move(contents));
  const auto handle = handles_.Insert(std::move(file));
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }
  return {KernelStatus::kOk, *handle};
}

KernelStatus FileService::Close(KernelHandle handle) {
  return handles_.Remove(handle, KernelObjectType::kFile)
             ? KernelStatus::kOk
             : KernelStatus::kNotFound;
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

FileStatResult FileService::Stat(std::string_view path) const {
  const auto normalized = NormalizeGuestPath(path);
  if (!normalized) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }

  std::lock_guard lock(files_mutex_);
  const auto found = files_.find(*normalized);
  return found != files_.end()
             ? FileStatResult{KernelStatus::kOk, found->second->size(),
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
  if (path.empty() || path.size() > kMaximumGuestPathLength ||
      path.find('\0') != std::string_view::npos) {
    return std::nullopt;
  }

  std::string separators_fixed(path);
  std::replace(separators_fixed.begin(), separators_fixed.end(), '\\', '/');
  if (separators_fixed.front() != '/') {
    return std::nullopt;
  }

  std::string normalized;
  normalized.reserve(separators_fixed.size());
  normalized.push_back('/');
  std::size_t cursor = 1;
  while (cursor <= separators_fixed.size()) {
    const auto end = separators_fixed.find('/', cursor);
    const auto length =
        (end == std::string::npos ? separators_fixed.size() : end) - cursor;
    const std::string_view component(separators_fixed.data() + cursor, length);
    if (component == "..") {
      return std::nullopt;
    }
    if (!component.empty() && component != ".") {
      if (normalized.size() > 1) {
        normalized.push_back('/');
      }
      normalized.append(component);
    }
    if (end == std::string::npos) {
      break;
    }
    cursor = end + 1;
  }
  return normalized;
}

std::shared_ptr<File> FileService::Find(KernelHandle handle) const {
  return std::static_pointer_cast<File>(
      handles_.Find(handle, KernelObjectType::kFile));
}

} // namespace kajps5::kernel
