// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/virtual_file_system.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace kajps5::kernel {
namespace {

unsigned char FoldAscii(unsigned char value) noexcept {
  return value >= 'A' && value <= 'Z'
             ? static_cast<unsigned char>(value + ('a' - 'A'))
             : value;
}

bool CaseInsensitiveLess(std::string_view left, std::string_view right) {
  const auto common = std::min(left.size(), right.size());
  for (std::size_t index = 0; index < common; ++index) {
    const auto left_folded = FoldAscii(static_cast<unsigned char>(left[index]));
    const auto right_folded =
        FoldAscii(static_cast<unsigned char>(right[index]));
    if (left_folded != right_folded) {
      return left_folded < right_folded;
    }
  }
  return left.size() != right.size() ? left.size() < right.size()
                                     : left < right;
}

bool PathComponentEqual(const std::filesystem::path &left,
                        const std::filesystem::path &right) {
#ifdef _WIN32
  auto left_text = left.native();
  auto right_text = right.native();
  std::transform(left_text.begin(), left_text.end(), left_text.begin(),
                 [](wchar_t value) { return std::towlower(value); });
  std::transform(right_text.begin(), right_text.end(), right_text.begin(),
                 [](wchar_t value) { return std::towlower(value); });
  return left_text == right_text;
#else
  return left == right;
#endif
}

bool IsWithin(const std::filesystem::path &root,
              const std::filesystem::path &candidate) {
  auto root_component = root.begin();
  auto candidate_component = candidate.begin();
  for (; root_component != root.end();
       ++root_component, ++candidate_component) {
    if (candidate_component == candidate.end() ||
        !PathComponentEqual(*root_component, *candidate_component)) {
      return false;
    }
  }
  return true;
}

bool IsMountMatch(std::string_view guest_path, std::string_view guest_root) {
  return guest_path == guest_root || (guest_path.size() > guest_root.size() &&
                                      guest_path.starts_with(guest_root) &&
                                      guest_path[guest_root.size()] == '/');
}

bool IsValidUtf8(std::string_view text) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7f) {
      ++index;
      continue;
    }

    std::size_t continuation_count = 0;
    unsigned char second_minimum = 0x80;
    unsigned char second_maximum = 0xbf;
    if (first >= 0xc2 && first <= 0xdf) {
      continuation_count = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuation_count = 2;
      if (first == 0xe0) {
        second_minimum = 0xa0;
      } else if (first == 0xed) {
        second_maximum = 0x9f;
      }
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuation_count = 3;
      if (first == 0xf0) {
        second_minimum = 0x90;
      } else if (first == 0xf4) {
        second_maximum = 0x8f;
      }
    } else {
      return false;
    }
    if (continuation_count > text.size() - index - 1) {
      return false;
    }
    const auto second = static_cast<unsigned char>(text[index + 1]);
    if (second < second_minimum || second > second_maximum) {
      return false;
    }
    for (std::size_t offset = 2; offset <= continuation_count; ++offset) {
      const auto value = static_cast<unsigned char>(text[index + offset]);
      if (value < 0x80 || value > 0xbf) {
        return false;
      }
    }
    index += continuation_count + 1;
  }
  return true;
}

bool AppendSafeRelativePath(std::string_view relative,
                            std::filesystem::path &path) {
  std::size_t cursor = 0;
  while (cursor < relative.size()) {
    const auto end = relative.find('/', cursor);
    const auto component = relative.substr(
        cursor,
        (end == std::string_view::npos ? relative.size() : end) - cursor);
    if (component.empty() || component.find(':') != std::string_view::npos ||
        !IsValidUtf8(component)) {
      return false;
    }
    try {
      const std::u8string utf8_component(
          reinterpret_cast<const char8_t *>(component.data()),
          component.size());
      const std::filesystem::path host_component(utf8_component);
      if (host_component.is_absolute() || host_component.has_root_name() ||
          host_component.has_root_directory()) {
        return false;
      }
      path /= host_component;
    } catch (const std::filesystem::filesystem_error &) {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    cursor = end + 1;
  }
  return true;
}

std::optional<std::string> FilenameUtf8(const std::filesystem::path &path) {
  try {
    const auto utf8 = path.filename().u8string();
    return std::string(reinterpret_cast<const char *>(utf8.data()),
                       utf8.size());
  } catch (const std::filesystem::filesystem_error &) {
    return std::nullopt;
  }
}

} // namespace

KernelStatus VirtualFileSystem::MountReadOnly(std::string guest_root,
                                              std::filesystem::path host_root) {
  const auto normalized = NormalizeGuestPath(guest_root);
  if (!normalized || *normalized == "/") {
    return KernelStatus::kInvalidArgument;
  }

  std::error_code error;
  auto canonical_root = std::filesystem::canonical(host_root, error);
  if (error || !std::filesystem::is_directory(canonical_root, error) || error) {
    return KernelStatus::kNotFound;
  }

  std::lock_guard lock(mounts_mutex_);
  const auto inserted =
      mounts_.emplace(*normalized, std::move(canonical_root)).second;
  return inserted ? KernelStatus::kOk : KernelStatus::kBusy;
}

KernelStatus VirtualFileSystem::Unmount(std::string_view guest_root) {
  const auto normalized = NormalizeGuestPath(guest_root);
  if (!normalized || *normalized == "/") {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mounts_mutex_);
  return mounts_.erase(*normalized) == 1 ? KernelStatus::kOk
                                         : KernelStatus::kNotFound;
}

VirtualFileReadResult
VirtualFileSystem::ReadFile(std::string_view guest_path,
                            std::uintmax_t maximum_size) const {
  const auto resolved = ResolveExistingPath(guest_path);
  if (!resolved) {
    return {KernelStatus::kNotFound, {}};
  }

  std::error_code error;
  if (!std::filesystem::is_regular_file(resolved->host_path, error) || error) {
    return {KernelStatus::kNotFound, {}};
  }
  const auto size = std::filesystem::file_size(resolved->host_path, error);
  if (error) {
    return {KernelStatus::kNotFound, {}};
  }
  if (size > maximum_size ||
      size > static_cast<std::uintmax_t>(
                 std::numeric_limits<std::size_t>::max())) {
    return {KernelStatus::kNoResources, {}};
  }

  std::ifstream stream(resolved->host_path, std::ios::binary);
  if (!stream) {
    return {KernelStatus::kPermissionDenied, {}};
  }
  std::vector<std::byte> contents(static_cast<std::size_t>(size));
  if (!contents.empty() &&
      !stream.read(reinterpret_cast<char *>(contents.data()),
                   static_cast<std::streamsize>(contents.size()))) {
    return {KernelStatus::kNotFound, {}};
  }
  return {KernelStatus::kOk, std::move(contents)};
}

VirtualFileStatResult
VirtualFileSystem::Stat(std::string_view guest_path) const {
  const auto resolved = ResolveExistingPath(guest_path);
  if (!resolved) {
    return {KernelStatus::kNotFound, false, 0};
  }

  std::error_code error;
  const auto is_file =
      std::filesystem::is_regular_file(resolved->host_path, error);
  if (error) {
    return {KernelStatus::kNotFound, false, 0};
  }
  if (!is_file && !std::filesystem::is_directory(resolved->host_path, error)) {
    return {KernelStatus::kNotFound, false, 0};
  }
  if (error) {
    return {KernelStatus::kNotFound, false, 0};
  }
  if (!is_file) {
    return {KernelStatus::kOk, false, 0};
  }
  const auto size = std::filesystem::file_size(resolved->host_path, error);
  return error ? VirtualFileStatResult{KernelStatus::kNotFound, false, 0}
               : VirtualFileStatResult{KernelStatus::kOk, true, size};
}

VirtualDirectoryResult
VirtualFileSystem::ListDirectory(std::string_view guest_path) const {
  const auto resolved = ResolveExistingPath(guest_path);
  if (!resolved) {
    return {KernelStatus::kNotFound, {}};
  }

  std::error_code error;
  if (!std::filesystem::is_directory(resolved->host_path, error) || error) {
    return {KernelStatus::kNotFound, {}};
  }

  std::vector<VirtualDirectoryEntry> entries;
  std::filesystem::directory_iterator iterator(resolved->host_path, error);
  const std::filesystem::directory_iterator end;
  while (!error && iterator != end) {
    const auto canonical_entry =
        std::filesystem::canonical(iterator->path(), error);
    if (error || !IsWithin(resolved->host_path, canonical_entry)) {
      error.clear();
      iterator.increment(error);
      continue;
    }
    const auto is_file = iterator->is_regular_file(error);
    if (error) {
      break;
    }
    const auto is_directory = iterator->is_directory(error);
    if (error) {
      break;
    }
    const auto name = FilenameUtf8(iterator->path());
    if ((is_file || is_directory) && name) {
      entries.push_back({*name, is_file});
    }
    iterator.increment(error);
  }
  if (error) {
    return {KernelStatus::kNotFound, {}};
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto &left, const auto &right) {
              return CaseInsensitiveLess(left.name, right.name);
            });
  return {KernelStatus::kOk, std::move(entries)};
}

std::optional<std::string>
VirtualFileSystem::NormalizeGuestPath(std::string_view path) {
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

std::optional<VirtualFileSystem::ResolvedPath>
VirtualFileSystem::ResolveExistingPath(std::string_view guest_path) const {
  const auto normalized = NormalizeGuestPath(guest_path);
  if (!normalized) {
    return std::nullopt;
  }

  std::filesystem::path root;
  std::string guest_root;
  {
    std::lock_guard lock(mounts_mutex_);
    for (const auto &[candidate_guest_root, candidate_host_root] : mounts_) {
      if (IsMountMatch(*normalized, candidate_guest_root) &&
          candidate_guest_root.size() > guest_root.size()) {
        guest_root = candidate_guest_root;
        root = candidate_host_root;
      }
    }
  }
  if (guest_root.empty()) {
    return std::nullopt;
  }

  auto candidate = root;
  if (normalized->size() > guest_root.size()) {
    const auto relative =
        std::string_view(*normalized).substr(guest_root.size() + 1);
    if (!AppendSafeRelativePath(relative, candidate)) {
      return std::nullopt;
    }
  }

  std::error_code error;
  candidate = std::filesystem::canonical(candidate, error);
  if (error || !IsWithin(root, candidate)) {
    return std::nullopt;
  }
  return ResolvedPath{*normalized, std::move(candidate)};
}

} // namespace kajps5::kernel
