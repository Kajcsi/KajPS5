// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

#include "kernel/status.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::kernel {

inline constexpr std::uint64_t kDefaultLibcHeapAlignment = 16;
inline constexpr std::size_t kMaximumLibcHeapAllocations = 262144;
inline constexpr std::size_t kMaximumLibcMspaces = 256;
inline constexpr std::uint64_t kLibcMspaceMetadataBytes = 64;

struct LibcHeapResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t address = 0;
  std::uint64_t size = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct LibcMspaceStats {
  std::uint64_t capacity = 0;
  std::uint64_t current_in_use = 0;
  std::uint64_t maximum_in_use = 0;
};

class LibcHeapService final {
 public:
  [[nodiscard]] LibcHeapResult Allocate(
      memory::GuestMemory& memory, std::uint64_t requested_size,
      std::uint64_t alignment = kDefaultLibcHeapAlignment,
      bool zero_fill = false);
  [[nodiscard]] LibcHeapResult Reallocate(
      memory::GuestMemory& memory, std::uint64_t address,
      std::uint64_t requested_size);
  [[nodiscard]] KernelStatus Release(memory::GuestMemory& memory,
                                     std::uint64_t address);
  [[nodiscard]] std::optional<std::uint64_t> UsableSize(
      const memory::GuestMemory& memory, std::uint64_t address) const;
  [[nodiscard]] std::size_t allocation_count() const;

  [[nodiscard]] LibcHeapResult CreateMspace(
      memory::GuestMemory& memory, std::uint64_t base_address,
      std::uint64_t size);
  [[nodiscard]] KernelStatus DestroyMspace(
      memory::GuestMemory& memory, std::uint64_t handle);
  [[nodiscard]] LibcHeapResult AllocateMspace(
      memory::GuestMemory& memory, std::uint64_t handle,
      std::uint64_t requested_size,
      std::uint64_t alignment = kDefaultLibcHeapAlignment,
      bool zero_fill = false);
  [[nodiscard]] LibcHeapResult ReallocateMspace(
      memory::GuestMemory& memory, std::uint64_t handle,
      std::uint64_t address, std::uint64_t requested_size,
      std::uint64_t alignment = kDefaultLibcHeapAlignment);
  [[nodiscard]] KernelStatus ReleaseMspace(
      memory::GuestMemory& memory, std::uint64_t handle,
      std::uint64_t address);
  [[nodiscard]] std::optional<LibcMspaceStats> MspaceStats(
      const memory::GuestMemory& memory, std::uint64_t handle) const;
  [[nodiscard]] std::optional<bool> MspaceIsEmpty(
      const memory::GuestMemory& memory, std::uint64_t handle) const;
  [[nodiscard]] std::size_t mspace_count() const;

 private:
  struct Allocation {
    memory::GuestMemory* memory = nullptr;
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
  };

  struct Mspace {
    memory::GuestMemory* memory = nullptr;
    std::uint64_t base_address = 0;
    std::uint64_t size = 0;
    std::uint64_t current_in_use = 0;
    std::uint64_t maximum_in_use = 0;
    std::map<std::uint64_t, std::uint64_t> free_ranges;
    std::map<std::uint64_t, Allocation> allocations;
  };

  [[nodiscard]] LibcHeapResult AllocateLocked(
      memory::GuestMemory& memory, std::uint64_t requested_size,
      std::uint64_t alignment, bool zero_fill);
  [[nodiscard]] LibcHeapResult AllocateMspaceLocked(
      Mspace& mspace, std::uint64_t requested_size,
      std::uint64_t alignment, bool zero_fill);
  [[nodiscard]] static std::optional<std::uint64_t> AlignUp(
      std::uint64_t value, std::uint64_t alignment) noexcept;
  static void AddFreeRange(Mspace& mspace, std::uint64_t address,
                           std::uint64_t size);
  [[nodiscard]] KernelStatus ReleaseMspaceLocked(
      Mspace& mspace, std::uint64_t address);

  mutable std::mutex mutex_;
  std::map<std::uint64_t, Allocation> allocations_;
  std::map<std::uint64_t, Mspace> mspaces_;
  std::size_t mspace_allocation_count_ = 0;
};

}  // namespace kajps5::kernel
