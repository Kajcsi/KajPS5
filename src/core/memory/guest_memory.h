// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kajps5::memory {

class GuestMemory final {
 public:
  GuestMemory(std::uint64_t base_address, std::size_t size);

  [[nodiscard]] std::uint64_t base_address() const noexcept;
  [[nodiscard]] std::uint64_t end_address() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;

  [[nodiscard]] bool Contains(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool Read(std::uint64_t address,
                          std::span<std::byte> destination) const noexcept;
  [[nodiscard]] bool Write(std::uint64_t address,
                           std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool Fill(std::uint64_t address, std::uint64_t length,
                          std::byte value) noexcept;

 private:
  [[nodiscard]] std::size_t OffsetOf(std::uint64_t address) const noexcept;

  std::uint64_t base_address_ = 0;
  std::vector<std::byte> bytes_;
};

}  // namespace kajps5::memory
