// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kajps5::memory {

class SharedMemoryBacking;

enum class GuestMemoryProtection : std::uint8_t {
  kNone = 0,
  kRead = 1U << 0U,
  kWrite = 1U << 1U,
  kExecute = 1U << 2U,
  kGpuRead = 0x10,
  kGpuWrite = 0x20,
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
  ~GuestMemory();

  GuestMemory(const GuestMemory&) = delete;
  GuestMemory& operator=(const GuestMemory&) = delete;

  [[nodiscard]] static std::unique_ptr<GuestMemory> CreateHostMapped(
      std::size_t size,
      GuestMemoryProtection initial_protection =
          GuestMemoryProtection::kNone) noexcept;

  [[nodiscard]] std::uint64_t base_address() const noexcept;
  [[nodiscard]] std::uint64_t end_address() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] bool host_mapped() const noexcept;
  [[nodiscard]] std::uint64_t mapping_granularity() const noexcept;

  [[nodiscard]] bool Contains(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanMap(std::uint64_t address,
                            std::uint64_t length) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> FindUnmappedRange(
      std::uint64_t search_start, std::uint64_t length,
      std::uint64_t alignment) const noexcept;
  [[nodiscard]] bool Map(std::uint64_t address, std::uint64_t length,
                         GuestMemoryProtection protection);
  [[nodiscard]] bool MapShared(
      std::uint64_t address, std::uint64_t length,
      GuestMemoryProtection protection,
      std::shared_ptr<SharedMemoryBacking> backing,
      std::uint64_t backing_offset);
  [[nodiscard]] bool Protect(std::uint64_t address, std::uint64_t length,
                             GuestMemoryProtection protection);
  [[nodiscard]] bool Unmap(std::uint64_t address, std::uint64_t length);
  [[nodiscard]] bool IsMapped(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanAccess(
      std::uint64_t address, std::uint64_t length,
      GuestMemoryProtection required_protection) const noexcept;
  [[nodiscard]] bool CanExecute(std::uint64_t address,
                                std::uint64_t length) const noexcept;
  [[nodiscard]] std::optional<GuestMemoryRegion> QueryRegion(
      std::uint64_t address) const noexcept;
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
  GuestMemory(std::byte* host_mapping, std::size_t size,
              std::size_t mapping_granularity) noexcept;

  struct SharedMapping {
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::uint64_t backing_offset = 0;
    std::shared_ptr<SharedMemoryBacking> backing;
  };

  [[nodiscard]] std::size_t FindContainingRegion(
      std::uint64_t address) const noexcept;
  [[nodiscard]] std::size_t FindSharedMapping(
      std::uint64_t address) const noexcept;
  [[nodiscard]] std::size_t OffsetOf(std::uint64_t address) const noexcept;
  [[nodiscard]] bool ReadBytes(
      std::uint64_t address, std::span<std::byte> destination) const noexcept;
  [[nodiscard]] bool WriteBytes(
      std::uint64_t address, std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool FillBytes(std::uint64_t address,
                               std::uint64_t length,
                               std::byte value) noexcept;
  void CoalesceRegions();

  std::uint64_t base_address_ = 0;
  std::size_t storage_size_ = 0;
  std::size_t mapping_granularity_ = 1;
  std::vector<std::byte> bytes_;
  std::byte* host_mapping_ = nullptr;
  std::vector<GuestMemoryRegion> regions_;
  std::vector<SharedMapping> shared_mappings_;
};

}  // namespace kajps5::memory
