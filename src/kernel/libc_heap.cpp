// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/libc_heap.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

#include "core/memory/guest_memory.h"

namespace kajps5::kernel {
namespace {

constexpr std::size_t kHeapCopyChunkSize = 4096;

bool IsPowerOfTwo(std::uint64_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

LibcHeapResult LibcHeapService::Allocate(
    memory::GuestMemory& memory, std::uint64_t requested_size,
    std::uint64_t alignment, bool zero_fill) {
  std::lock_guard lock(mutex_);
  return AllocateLocked(memory, requested_size, alignment, zero_fill);
}

LibcHeapResult LibcHeapService::AllocateLocked(
    memory::GuestMemory& memory, std::uint64_t requested_size,
    std::uint64_t alignment, bool zero_fill) {
  if (!IsPowerOfTwo(alignment)) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  alignment = std::max(alignment, kDefaultLibcHeapAlignment);
  const auto size = requested_size == 0 ? 1 : requested_size;
  if (size > static_cast<std::uint64_t>(
                 std::numeric_limits<std::size_t>::max()) ||
      allocations_.size() >= kMaximumLibcHeapAllocations) {
    return {KernelStatus::kNoResources, 0, 0};
  }

  auto search_start = memory.base_address();
  if (search_start == 0) {
    search_start = alignment;
  }
  const auto address =
      memory.FindUnmappedRange(search_start, size, alignment);
  if (!address) {
    return {KernelStatus::kNoResources, 0, 0};
  }

  try {
    if (!memory.Map(*address, size,
                    memory::GuestMemoryProtection::kRead |
                        memory::GuestMemoryProtection::kWrite)) {
      return {KernelStatus::kNoResources, 0, 0};
    }
    if (zero_fill && !memory.Fill(*address, size, std::byte{0})) {
      (void)memory.Unmap(*address, size);
      return {KernelStatus::kInvalidArgument, 0, 0};
    }
    try {
      const auto inserted = allocations_
                                .emplace(*address,
                                         Allocation{&memory, size, alignment})
                                .second;
      if (!inserted) {
        (void)memory.Unmap(*address, size);
        return {KernelStatus::kNoResources, 0, 0};
      }
    } catch (...) {
      (void)memory.Unmap(*address, size);
      return {KernelStatus::kNoResources, 0, 0};
    }
  } catch (...) {
    return {KernelStatus::kNoResources, 0, 0};
  }
  return {KernelStatus::kOk, *address, size};
}

LibcHeapResult LibcHeapService::Reallocate(
    memory::GuestMemory& memory, std::uint64_t address,
    std::uint64_t requested_size) {
  if (address == 0) {
    return Allocate(memory, requested_size);
  }
  if (requested_size == 0) {
    const auto released = Release(memory, address);
    return {released, 0, 0};
  }

  std::lock_guard lock(mutex_);
  const auto found = allocations_.find(address);
  if (found == allocations_.end() || found->second.memory != &memory) {
    return {KernelStatus::kNotFound, 0, 0};
  }

  const auto old_size = found->second.size;
  const auto resized = AllocateLocked(
      memory, requested_size, found->second.alignment, false);
  if (!resized) {
    return resized;
  }

  std::array<std::byte, kHeapCopyChunkSize> bytes{};
  const auto copy_size = std::min(old_size, resized.size);
  std::uint64_t copied = 0;
  while (copied < copy_size) {
    const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(
        bytes.size(), copy_size - copied));
    const auto view = std::span(bytes).first(chunk);
    if (!memory.Read(address + copied, view) ||
        !memory.Write(resized.address + copied, view)) {
      (void)memory.Unmap(resized.address, resized.size);
      allocations_.erase(resized.address);
      return {KernelStatus::kInvalidArgument, 0, 0};
    }
    copied += chunk;
  }

  if (!memory.Unmap(address, old_size)) {
    (void)memory.Unmap(resized.address, resized.size);
    allocations_.erase(resized.address);
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  allocations_.erase(found);
  return resized;
}

KernelStatus LibcHeapService::Release(memory::GuestMemory& memory,
                                      std::uint64_t address) {
  if (address == 0) {
    return KernelStatus::kOk;
  }
  std::lock_guard lock(mutex_);
  const auto found = allocations_.find(address);
  if (found == allocations_.end() || found->second.memory != &memory) {
    return KernelStatus::kNotFound;
  }
  if (!memory.Unmap(address, found->second.size)) {
    return KernelStatus::kInvalidArgument;
  }
  allocations_.erase(found);
  return KernelStatus::kOk;
}

std::optional<std::uint64_t> LibcHeapService::UsableSize(
    const memory::GuestMemory& memory, std::uint64_t address) const {
  std::lock_guard lock(mutex_);
  const auto found = allocations_.find(address);
  if (found != allocations_.end() && found->second.memory == &memory) {
    return found->second.size;
  }
  for (const auto& [handle, mspace] : mspaces_) {
    (void)handle;
    if (mspace.memory != &memory) {
      continue;
    }
    const auto allocation = mspace.allocations.find(address);
    if (allocation != mspace.allocations.end()) {
      return allocation->second.size;
    }
  }
  return std::nullopt;
}

std::size_t LibcHeapService::allocation_count() const {
  std::lock_guard lock(mutex_);
  return allocations_.size() + mspace_allocation_count_;
}

std::optional<std::uint64_t> LibcHeapService::AlignUp(
    std::uint64_t value, std::uint64_t alignment) noexcept {
  if (!IsPowerOfTwo(alignment)) {
    return std::nullopt;
  }
  const auto mask = alignment - 1;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
    return std::nullopt;
  }
  return (value + mask) & ~mask;
}

LibcHeapResult LibcHeapService::CreateMspace(
    memory::GuestMemory& memory, std::uint64_t base_address,
    std::uint64_t size) {
  if (base_address == 0 ||
      size <= kLibcMspaceMetadataBytes ||
      base_address > std::numeric_limits<std::uint64_t>::max() - size ||
      !memory.CanAccess(base_address, size,
                        memory::GuestMemoryProtection::kRead |
                            memory::GuestMemoryProtection::kWrite)) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  const auto first_address = AlignUp(
      base_address + kLibcMspaceMetadataBytes,
      kDefaultLibcHeapAlignment);
  const auto end_address = base_address + size;
  if (!first_address || *first_address >= end_address) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }

  std::lock_guard lock(mutex_);
  if (mspaces_.size() >= kMaximumLibcMspaces ||
      mspaces_.contains(base_address)) {
    return {KernelStatus::kNoResources, 0, 0};
  }
  for (const auto& [handle, existing] : mspaces_) {
    (void)handle;
    if (existing.memory == &memory &&
        base_address < existing.base_address + existing.size &&
        existing.base_address < end_address) {
      return {KernelStatus::kInvalidArgument, 0, 0};
    }
  }
  Mspace mspace;
  mspace.memory = &memory;
  mspace.base_address = base_address;
  mspace.size = size;
  try {
    mspace.free_ranges.emplace(*first_address,
                               end_address - *first_address);
    if (!mspaces_.emplace(base_address, std::move(mspace)).second) {
      return {KernelStatus::kNoResources, 0, 0};
    }
  } catch (...) {
    return {KernelStatus::kNoResources, 0, 0};
  }
  return {KernelStatus::kOk, base_address, size};
}

KernelStatus LibcHeapService::DestroyMspace(
    memory::GuestMemory& memory, std::uint64_t handle) {
  std::lock_guard lock(mutex_);
  const auto found = mspaces_.find(handle);
  if (found == mspaces_.end() || found->second.memory != &memory) {
    return KernelStatus::kNotFound;
  }
  if (!found->second.allocations.empty()) {
    return KernelStatus::kBusy;
  }
  mspaces_.erase(found);
  return KernelStatus::kOk;
}

LibcHeapResult LibcHeapService::AllocateMspace(
    memory::GuestMemory& memory, std::uint64_t handle,
    std::uint64_t requested_size, std::uint64_t alignment,
    bool zero_fill) {
  std::lock_guard lock(mutex_);
  const auto found = mspaces_.find(handle);
  if (found == mspaces_.end() || found->second.memory != &memory) {
    return {KernelStatus::kNotFound, 0, 0};
  }
  return AllocateMspaceLocked(found->second, requested_size, alignment,
                              zero_fill);
}

LibcHeapResult LibcHeapService::AllocateMspaceLocked(
    Mspace& mspace, std::uint64_t requested_size,
    std::uint64_t alignment, bool zero_fill) {
  if (!IsPowerOfTwo(alignment)) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  alignment = std::max(alignment, kDefaultLibcHeapAlignment);
  const auto size = requested_size == 0 ? 1 : requested_size;
  if (mspace_allocation_count_ >= kMaximumLibcHeapAllocations) {
    return {KernelStatus::kNoResources, 0, 0};
  }

  for (auto range = mspace.free_ranges.begin();
       range != mspace.free_ranges.end(); ++range) {
    const auto address = AlignUp(range->first, alignment);
    if (!address || *address < range->first) {
      continue;
    }
    const auto prefix = *address - range->first;
    if (prefix > range->second || size > range->second - prefix) {
      continue;
    }
    const auto range_size = range->second;
    const auto suffix_address = *address + size;
    const auto suffix_size = range_size - prefix - size;
    try {
      if (!mspace.allocations
               .emplace(*address,
                        Allocation{mspace.memory, size, alignment})
               .second) {
        return {KernelStatus::kNoResources, 0, 0};
      }
    } catch (...) {
      return {KernelStatus::kNoResources, 0, 0};
    }
    if (prefix != 0 && suffix_size != 0) {
      try {
        mspace.free_ranges.emplace(suffix_address, suffix_size);
      } catch (...) {
        mspace.allocations.erase(*address);
        return {KernelStatus::kNoResources, 0, 0};
      }
      range->second = prefix;
    } else if (prefix != 0) {
      range->second = prefix;
    } else if (suffix_size != 0) {
      auto suffix = mspace.free_ranges.extract(range);
      suffix.key() = suffix_address;
      suffix.mapped() = suffix_size;
      mspace.free_ranges.insert(std::move(suffix));
    } else {
      mspace.free_ranges.erase(range);
    }
    mspace.current_in_use += size;
    ++mspace_allocation_count_;
    mspace.maximum_in_use =
        std::max(mspace.maximum_in_use, mspace.current_in_use);
    if (zero_fill &&
        !mspace.memory->Fill(*address, size, std::byte{0})) {
      (void)ReleaseMspaceLocked(mspace, *address);
      return {KernelStatus::kInvalidArgument, 0, 0};
    }
    return {KernelStatus::kOk, *address, size};
  }
  return {KernelStatus::kNoResources, 0, 0};
}

void LibcHeapService::AddFreeRange(Mspace& mspace,
                                   std::uint64_t address,
                                   std::uint64_t size) {
  if (size == 0) {
    return;
  }
  auto next = mspace.free_ranges.lower_bound(address);
  if (next != mspace.free_ranges.begin()) {
    auto previous = std::prev(next);
    if (previous->first + previous->second == address) {
      address = previous->first;
      size += previous->second;
      next = mspace.free_ranges.erase(previous);
    }
  }
  if (next != mspace.free_ranges.end() && address + size == next->first) {
    size += next->second;
    mspace.free_ranges.erase(next);
  }
  mspace.free_ranges[address] = size;
}

KernelStatus LibcHeapService::ReleaseMspaceLocked(
    Mspace& mspace, std::uint64_t address) {
  if (address == 0) {
    return KernelStatus::kOk;
  }
  const auto found = mspace.allocations.find(address);
  if (found == mspace.allocations.end()) {
    return KernelStatus::kNotFound;
  }
  const auto size = found->second.size;
  mspace.allocations.erase(found);
  AddFreeRange(mspace, address, size);
  mspace.current_in_use -= size;
  --mspace_allocation_count_;
  return KernelStatus::kOk;
}

KernelStatus LibcHeapService::ReleaseMspace(
    memory::GuestMemory& memory, std::uint64_t handle,
    std::uint64_t address) {
  std::lock_guard lock(mutex_);
  const auto found = mspaces_.find(handle);
  if (found == mspaces_.end() || found->second.memory != &memory) {
    return KernelStatus::kNotFound;
  }
  return ReleaseMspaceLocked(found->second, address);
}

LibcHeapResult LibcHeapService::ReallocateMspace(
    memory::GuestMemory& memory, std::uint64_t handle,
    std::uint64_t address, std::uint64_t requested_size,
    std::uint64_t alignment) {
  std::lock_guard lock(mutex_);
  const auto found_space = mspaces_.find(handle);
  if (found_space == mspaces_.end() ||
      found_space->second.memory != &memory) {
    return {KernelStatus::kNotFound, 0, 0};
  }
  auto& mspace = found_space->second;
  if (address == 0) {
    return AllocateMspaceLocked(mspace, requested_size, alignment, false);
  }
  if (requested_size == 0) {
    const auto released = ReleaseMspaceLocked(mspace, address);
    return {released, 0, 0};
  }
  if (!IsPowerOfTwo(alignment)) {
    return {KernelStatus::kInvalidArgument, 0, 0};
  }
  alignment = std::max(alignment, kDefaultLibcHeapAlignment);

  const auto found = mspace.allocations.find(address);
  if (found == mspace.allocations.end()) {
    return {KernelStatus::kNotFound, 0, 0};
  }
  const auto old_size = found->second.size;
  if (address % alignment == 0 && requested_size <= old_size) {
    const auto released_size = old_size - requested_size;
    found->second.size = requested_size;
    found->second.alignment = alignment;
    if (released_size != 0) {
      AddFreeRange(mspace, address + requested_size, released_size);
      mspace.current_in_use -= released_size;
    }
    return {KernelStatus::kOk, address, requested_size};
  }
  if (address % alignment == 0 &&
      address <= std::numeric_limits<std::uint64_t>::max() - old_size) {
    const auto next = mspace.free_ranges.find(address + old_size);
    const auto growth = requested_size > old_size
                            ? requested_size - old_size
                            : 0;
    if (growth != 0 && next != mspace.free_ranges.end() &&
        next->second >= growth) {
      const auto remaining = next->second - growth;
      const auto remaining_address = next->first + growth;
      auto remaining_range = mspace.free_ranges.extract(next);
      if (remaining != 0) {
        remaining_range.key() = remaining_address;
        remaining_range.mapped() = remaining;
        mspace.free_ranges.insert(std::move(remaining_range));
      }
      found->second.size = requested_size;
      found->second.alignment = alignment;
      mspace.current_in_use += growth;
      mspace.maximum_in_use =
          std::max(mspace.maximum_in_use, mspace.current_in_use);
      return {KernelStatus::kOk, address, requested_size};
    }
  }

  const auto resized =
      AllocateMspaceLocked(mspace, requested_size, alignment, false);
  if (!resized) {
    return resized;
  }
  std::array<std::byte, kHeapCopyChunkSize> bytes{};
  const auto copy_size = std::min(old_size, resized.size);
  std::uint64_t copied = 0;
  while (copied < copy_size) {
    const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(
        bytes.size(), copy_size - copied));
    const auto view = std::span(bytes).first(chunk);
    if (!memory.Read(address + copied, view) ||
        !memory.Write(resized.address + copied, view)) {
      (void)ReleaseMspaceLocked(mspace, resized.address);
      return {KernelStatus::kInvalidArgument, 0, 0};
    }
    copied += chunk;
  }
  (void)ReleaseMspaceLocked(mspace, address);
  return resized;
}

std::optional<LibcMspaceStats> LibcHeapService::MspaceStats(
    const memory::GuestMemory& memory, std::uint64_t handle) const {
  std::lock_guard lock(mutex_);
  const auto found = mspaces_.find(handle);
  if (found == mspaces_.end() || found->second.memory != &memory) {
    return std::nullopt;
  }
  return LibcMspaceStats{found->second.size,
                         found->second.current_in_use,
                         found->second.maximum_in_use};
}

std::optional<bool> LibcHeapService::MspaceIsEmpty(
    const memory::GuestMemory& memory, std::uint64_t handle) const {
  std::lock_guard lock(mutex_);
  const auto found = mspaces_.find(handle);
  if (found == mspaces_.end() || found->second.memory != &memory) {
    return std::nullopt;
  }
  return found->second.allocations.empty();
}

std::size_t LibcHeapService::mspace_count() const {
  std::lock_guard lock(mutex_);
  return mspaces_.size();
}

}  // namespace kajps5::kernel
