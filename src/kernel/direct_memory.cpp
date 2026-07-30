// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/direct_memory.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace kajps5::kernel {

DirectMemoryService::DirectMemoryService() {
  free_ranges_.emplace(0, kDirectMemorySize);
}

std::uint64_t DirectMemoryService::size() const noexcept {
  return kDirectMemorySize;
}

std::optional<std::uint64_t> DirectMemoryService::AlignUp(
    std::uint64_t value, std::uint64_t alignment) noexcept {
  if (alignment == 0) {
    return value;
  }
  const auto remainder = value % alignment;
  const auto increment = remainder == 0 ? 0 : alignment - remainder;
  if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
    return std::nullopt;
  }
  return value + increment;
}

DirectMemoryRangeResult DirectMemoryService::Available(
    std::uint64_t search_start, std::uint64_t search_end,
    std::uint64_t alignment) const {
  if (search_start >= search_end) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  const auto bounded_end = std::min(search_end, kDirectMemorySize);
  if (search_start >= bounded_end) {
    return {KernelStatus::kNoResources, 0, 0};
  }

  std::lock_guard lock(mutex_);
  std::uint64_t best_address = 0;
  std::uint64_t best_size = 0;
  for (const auto& [range_start, range_size] : free_ranges_) {
    if (range_start >= bounded_end) {
      break;
    }
    const auto range_end = range_start + range_size;
    const auto lower_bound = std::max(range_start, search_start);
    const auto aligned = AlignUp(lower_bound, alignment);
    const auto clipped_end = std::min(range_end, bounded_end);
    if (!aligned.has_value() || *aligned < lower_bound ||
        *aligned >= clipped_end || clipped_end - *aligned <= best_size) {
      continue;
    }
    best_address = *aligned;
    best_size = clipped_end - *aligned;
  }
  if (best_size == 0) {
    return {KernelStatus::kNoResources, 0, 0};
  }
  return {KernelStatus::kOk, best_address, best_size};
}

DirectMemoryRangeResult DirectMemoryService::Allocate(
    std::uint64_t search_start, std::uint64_t search_end,
    std::uint64_t length, std::uint64_t alignment,
    std::int32_t memory_type) {
  if (length == 0 || search_start >= search_end) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  const auto bounded_end = std::min(search_end, kDirectMemorySize);
  if (search_start >= bounded_end) {
    return {KernelStatus::kNoResources, 0, 0};
  }

  std::lock_guard lock(mutex_);
  if (allocations_.size() >= kMaximumDirectMemoryAllocations) {
    return {KernelStatus::kNoResources, 0, 0};
  }
  auto range = free_ranges_.upper_bound(search_start);
  if (range != free_ranges_.begin()) {
    --range;
  }
  for (; range != free_ranges_.end() && range->first < bounded_end; ++range) {
    const auto range_end =
        std::min(range->first + range->second, bounded_end);
    const auto lower_bound = std::max(range->first, search_start);
    const auto aligned = AlignUp(lower_bound, alignment);
    if (!aligned.has_value() || *aligned < lower_bound ||
        *aligned > range_end || length > range_end - *aligned) {
      continue;
    }
    const auto selected = *aligned;
    ConsumeFreeRange(range, selected, length);
    allocations_.emplace(selected, Allocation{length, memory_type});
    return {KernelStatus::kOk, selected, length};
  }
  return {KernelStatus::kNoResources, 0, 0};
}

KernelStatus DirectMemoryService::Release(std::uint64_t start,
                                          std::uint64_t length) {
  if (length == 0 || start >= kDirectMemorySize ||
      length > kDirectMemorySize - start) {
    return KernelStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  auto owner = allocations_.upper_bound(start);
  if (owner == allocations_.begin()) {
    return KernelStatus::kNotFound;
  }
  --owner;
  const auto owner_start = owner->first;
  const auto owner_size = owner->second.size;
  const auto owner_type = owner->second.memory_type;
  const auto owner_end = owner_start + owner_size;
  const auto release_end = start + length;
  if (start < owner_start || release_end > owner_end) {
    return KernelStatus::kNotFound;
  }
  if (start > owner_start && release_end < owner_end &&
      allocations_.size() >= kMaximumDirectMemoryAllocations) {
    return KernelStatus::kNoResources;
  }

  allocations_.erase(owner);
  if (owner_start < start) {
    allocations_.emplace(owner_start,
                         Allocation{start - owner_start, owner_type});
  }
  if (release_end < owner_end) {
    allocations_.emplace(release_end,
                         Allocation{owner_end - release_end, owner_type});
  }
  AddFreeRange(start, length);
  return KernelStatus::kOk;
}

bool DirectMemoryService::ContainsAllocatedRange(
    std::uint64_t start, std::uint64_t length) const {
  if (length == 0 || start >= kDirectMemorySize ||
      length > kDirectMemorySize - start) {
    return false;
  }
  std::lock_guard lock(mutex_);
  auto owner = allocations_.upper_bound(start);
  if (owner == allocations_.begin()) {
    return false;
  }
  --owner;
  return start >= owner->first &&
         length <= owner->first + owner->second.size - start;
}

std::size_t DirectMemoryService::allocation_count() const {
  std::lock_guard lock(mutex_);
  return allocations_.size();
}

void DirectMemoryService::ConsumeFreeRange(
    std::map<std::uint64_t, std::uint64_t>::iterator range,
    std::uint64_t start, std::uint64_t length) {
  const auto range_start = range->first;
  const auto range_end = range_start + range->second;
  free_ranges_.erase(range);
  if (range_start < start) {
    free_ranges_.emplace(range_start, start - range_start);
  }
  if (start + length < range_end) {
    free_ranges_.emplace(start + length, range_end - start - length);
  }
}

void DirectMemoryService::AddFreeRange(std::uint64_t start,
                                       std::uint64_t length) {
  auto end = start + length;
  auto next = free_ranges_.lower_bound(start);
  if (next != free_ranges_.begin()) {
    auto previous = std::prev(next);
    const auto previous_end = previous->first + previous->second;
    if (previous_end >= start) {
      start = previous->first;
      end = std::max(end, previous_end);
      next = free_ranges_.erase(previous);
    }
  }
  while (next != free_ranges_.end() && next->first <= end) {
    end = std::max(end, next->first + next->second);
    next = free_ranges_.erase(next);
  }
  free_ranges_.emplace(start, end - start);
}

}  // namespace kajps5::kernel
