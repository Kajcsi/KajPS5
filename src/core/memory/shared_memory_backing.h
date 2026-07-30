// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

namespace kajps5::memory {

inline constexpr std::uint64_t kSharedMemoryBackingPageSize = 0x4000;

class SharedMemoryBacking final {
 public:
  explicit SharedMemoryBacking(std::uint64_t size) noexcept;

  SharedMemoryBacking(const SharedMemoryBacking&) = delete;
  SharedMemoryBacking& operator=(const SharedMemoryBacking&) = delete;

  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] bool Contains(std::uint64_t offset,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool Read(std::uint64_t offset,
                          std::span<std::byte> destination) const noexcept;
  [[nodiscard]] bool Write(
      std::uint64_t offset, std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool Fill(std::uint64_t offset, std::uint64_t length,
                          std::byte value) noexcept;
  void Clear(std::uint64_t offset, std::uint64_t length) noexcept;

 private:
  using Page = std::array<std::byte, kSharedMemoryBackingPageSize>;

  [[nodiscard]] bool EnsurePagesLocked(std::uint64_t offset,
                                       std::uint64_t length) noexcept;

  std::uint64_t size_ = 0;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::unique_ptr<Page>> pages_;
};

}  // namespace kajps5::memory
