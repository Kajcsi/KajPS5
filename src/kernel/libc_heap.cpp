// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/libc_heap.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>

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
  if (found == allocations_.end() || found->second.memory != &memory) {
    return std::nullopt;
  }
  return found->second.size;
}

std::size_t LibcHeapService::allocation_count() const {
  std::lock_guard lock(mutex_);
  return allocations_.size();
}

}  // namespace kajps5::kernel
