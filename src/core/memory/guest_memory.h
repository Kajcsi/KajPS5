// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kajps5::memory {

enum class GuestMemoryProtection : std::uint8_t {
  kNone = 0,
  kRead = 1U << 0U,
  kWrite = 1U << 1U,
  kExecute = 1U << 2U,
};

[[nodiscard]] constexpr GuestMemoryProtection operator|(
    GuestMemoryProtection left, GuestMemoryProtection right) noexcept {
  return static_cast<GuestMemoryProtection>(
      static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

struct GuestMemoryRegion {
  std::uint64_t address = 0;
  std::uint64_t size = 0;
  GuestMemoryProtection protection = GuestMemoryProtection::kNone;
};

class GuestMemory final {
 public:
  GuestMemory(
      std::uint64_t base_address, std::size_t size,
      GuestMemoryProtection initial_protection =
          GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);

  [[nodiscard]] std::uint64_t base_address() const noexcept;
  [[nodiscard]] std::uint64_t end_address() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;

  [[nodiscard]] bool Contains(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanMap(std::uint64_t address,
                            std::uint64_t length) const noexcept;
  [[nodiscard]] bool Map(std::uint64_t address, std::uint64_t length,
                         GuestMemoryProtection protection);
  [[nodiscard]] bool IsMapped(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanAccess(
      std::uint64_t address, std::uint64_t length,
      GuestMemoryProtection required_protection) const noexcept;
  [[nodiscard]] bool CanExecute(std::uint64_t address,
                                std::uint64_t length) const noexcept;
  [[nodiscard]] std::span<const GuestMemoryRegion> regions() const noexcept;

  [[nodiscard]] bool Read(std::uint64_t address,
                          std::span<std::byte> destination) const noexcept;
  [[nodiscard]] bool Write(std::uint64_t address,
                           std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool Fill(std::uint64_t address, std::uint64_t length,
                          std::byte value) noexcept;
  [[nodiscard]] bool Initialize(
      std::uint64_t address, std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool InitializeFill(std::uint64_t address,
                                    std::uint64_t length,
                                    std::byte value) noexcept;

 private:
  [[nodiscard]] std::size_t FindContainingRegion(
      std::uint64_t address) const noexcept;
  [[nodiscard]] std::size_t OffsetOf(std::uint64_t address) const noexcept;

  std::uint64_t base_address_ = 0;
  std::vector<std::byte> bytes_;
  std::vector<GuestMemoryRegion> regions_;
};

}  // namespace kajps5::memory
