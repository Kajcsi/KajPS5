// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::uint64_t kDirectMemorySize = 0x360000000;
inline constexpr std::size_t kMaximumDirectMemoryAllocations = 262144;

struct DirectMemoryRangeResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t address = 0;
  std::uint64_t size = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class DirectMemoryService final {
 public:
  DirectMemoryService();

  DirectMemoryService(const DirectMemoryService&) = delete;
  DirectMemoryService& operator=(const DirectMemoryService&) = delete;

  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] DirectMemoryRangeResult Available(
      std::uint64_t search_start, std::uint64_t search_end,
      std::uint64_t alignment) const;
  [[nodiscard]] DirectMemoryRangeResult Allocate(
      std::uint64_t search_start, std::uint64_t search_end,
      std::uint64_t length, std::uint64_t alignment,
      std::int32_t memory_type);
  [[nodiscard]] KernelStatus Release(std::uint64_t start,
                                     std::uint64_t length);
  [[nodiscard]] bool ContainsAllocatedRange(std::uint64_t start,
                                            std::uint64_t length) const;
  [[nodiscard]] std::size_t allocation_count() const;

 private:
  struct Allocation {
    std::uint64_t size = 0;
    std::int32_t memory_type = 0;
  };

  [[nodiscard]] static std::optional<std::uint64_t> AlignUp(
      std::uint64_t value, std::uint64_t alignment) noexcept;
  void ConsumeFreeRange(std::map<std::uint64_t, std::uint64_t>::iterator range,
                        std::uint64_t start, std::uint64_t length);
  void AddFreeRange(std::uint64_t start, std::uint64_t length);

  mutable std::mutex mutex_;
  std::map<std::uint64_t, Allocation> allocations_;
  std::map<std::uint64_t, std::uint64_t> free_ranges_;
};

}  // namespace kajps5::kernel
