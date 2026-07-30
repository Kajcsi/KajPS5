// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/memory/shared_memory_backing.h"

#include <algorithm>

namespace kajps5::memory {

SharedMemoryBacking::SharedMemoryBacking(std::uint64_t size) noexcept
    : size_(size) {}

std::uint64_t SharedMemoryBacking::size() const noexcept {
  return size_;
}

bool SharedMemoryBacking::Contains(std::uint64_t offset,
                                   std::uint64_t length) const noexcept {
  return offset <= size_ && length <= size_ - offset;
}

bool SharedMemoryBacking::Read(
    std::uint64_t offset,
    std::span<std::byte> destination) const noexcept {
  if (!Contains(offset, destination.size())) {
    return false;
  }

  std::lock_guard lock(mutex_);
  std::size_t output_offset = 0;
  while (output_offset < destination.size()) {
    const auto current = offset + output_offset;
    const auto page_index = current / kSharedMemoryBackingPageSize;
    const auto page_offset = current % kSharedMemoryBackingPageSize;
    const auto chunk = std::min<std::size_t>(
        destination.size() - output_offset,
        static_cast<std::size_t>(kSharedMemoryBackingPageSize - page_offset));
    const auto page = pages_.find(page_index);
    if (page == pages_.end()) {
      std::fill_n(destination.begin() +
                      static_cast<std::ptrdiff_t>(output_offset),
                  chunk, std::byte{0});
    } else {
      std::copy_n(page->second->begin() +
                      static_cast<std::ptrdiff_t>(page_offset),
                  chunk,
                  destination.begin() +
                      static_cast<std::ptrdiff_t>(output_offset));
    }
    output_offset += chunk;
  }
  return true;
}

bool SharedMemoryBacking::EnsurePagesLocked(std::uint64_t offset,
                                            std::uint64_t length) noexcept {
  if (length == 0) {
    return true;
  }
  const auto first_page = offset / kSharedMemoryBackingPageSize;
  const auto last_page =
      (offset + length - 1) / kSharedMemoryBackingPageSize;
  try {
    for (auto page = first_page; page <= last_page; ++page) {
      if (!pages_.contains(page)) {
        pages_.emplace(page, std::make_unique<Page>());
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

bool SharedMemoryBacking::Write(
    std::uint64_t offset, std::span<const std::byte> source) noexcept {
  if (!Contains(offset, source.size())) {
    return false;
  }

  std::lock_guard lock(mutex_);
  if (!EnsurePagesLocked(offset, source.size())) {
    return false;
  }
  std::size_t input_offset = 0;
  while (input_offset < source.size()) {
    const auto current = offset + input_offset;
    const auto page_index = current / kSharedMemoryBackingPageSize;
    const auto page_offset = current % kSharedMemoryBackingPageSize;
    const auto chunk = std::min<std::size_t>(
        source.size() - input_offset,
        static_cast<std::size_t>(kSharedMemoryBackingPageSize - page_offset));
    auto& page = *pages_.at(page_index);
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(input_offset),
                chunk,
                page.begin() + static_cast<std::ptrdiff_t>(page_offset));
    input_offset += chunk;
  }
  return true;
}

bool SharedMemoryBacking::Fill(std::uint64_t offset, std::uint64_t length,
                               std::byte value) noexcept {
  if (!Contains(offset, length)) {
    return false;
  }
  if (value == std::byte{0}) {
    Clear(offset, length);
    return true;
  }

  std::lock_guard lock(mutex_);
  if (!EnsurePagesLocked(offset, length)) {
    return false;
  }
  std::uint64_t filled = 0;
  while (filled < length) {
    const auto current = offset + filled;
    const auto page_index = current / kSharedMemoryBackingPageSize;
    const auto page_offset = current % kSharedMemoryBackingPageSize;
    const auto chunk = std::min(
        length - filled, kSharedMemoryBackingPageSize - page_offset);
    auto& page = *pages_.at(page_index);
    std::fill_n(page.begin() + static_cast<std::ptrdiff_t>(page_offset),
                static_cast<std::size_t>(chunk), value);
    filled += chunk;
  }
  return true;
}

void SharedMemoryBacking::Clear(std::uint64_t offset,
                                std::uint64_t length) noexcept {
  if (!Contains(offset, length) || length == 0) {
    return;
  }

  const auto end = offset + length;
  std::lock_guard lock(mutex_);
  for (auto page = pages_.begin(); page != pages_.end();) {
    const auto page_start = page->first * kSharedMemoryBackingPageSize;
    const auto page_end = page_start + kSharedMemoryBackingPageSize;
    const auto overlap_start = std::max(offset, page_start);
    const auto overlap_end = std::min(end, page_end);
    if (overlap_start >= overlap_end) {
      ++page;
      continue;
    }
    if (overlap_start == page_start && overlap_end == page_end) {
      page = pages_.erase(page);
      continue;
    }
    std::fill_n(page->second->begin() + static_cast<std::ptrdiff_t>(
                    overlap_start - page_start),
                static_cast<std::size_t>(overlap_end - overlap_start),
                std::byte{0});
    ++page;
  }
}

}  // namespace kajps5::memory
