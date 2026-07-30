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

struct LibcHeapResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t address = 0;
  std::uint64_t size = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
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

 private:
  struct Allocation {
    memory::GuestMemory* memory = nullptr;
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
  };

  [[nodiscard]] LibcHeapResult AllocateLocked(
      memory::GuestMemory& memory, std::uint64_t requested_size,
      std::uint64_t alignment, bool zero_fill);

  mutable std::mutex mutex_;
  std::map<std::uint64_t, Allocation> allocations_;
};

}  // namespace kajps5::kernel
