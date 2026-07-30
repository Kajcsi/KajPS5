// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/memory/guest_memory.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kajps5::memory {
namespace {

constexpr std::uint8_t kAllProtectionBits =
    static_cast<std::uint8_t>(GuestMemoryProtection::kRead) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kWrite) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kExecute) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kGpuRead) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kGpuWrite);

bool IsValidProtection(GuestMemoryProtection protection) noexcept {
  return (static_cast<std::uint8_t>(protection) & ~kAllProtectionBits) == 0;
}

std::size_t ValidateSize(std::uint64_t base_address, std::size_t size) {
  const auto size64 = static_cast<std::uint64_t>(size);
  if (size64 > std::numeric_limits<std::uint64_t>::max() - base_address) {
    throw std::invalid_argument("Guest memory address range overflows.");
  }
  return size;
}

}  // namespace

GuestMemory::GuestMemory(std::uint64_t base_address, std::size_t size,
                         GuestMemoryProtection initial_protection)
    : base_address_(base_address), bytes_(ValidateSize(base_address, size)) {
  if (!IsValidProtection(initial_protection)) {
    throw std::invalid_argument("Guest memory protection is invalid.");
  }
  if (!bytes_.empty() && initial_protection != GuestMemoryProtection::kNone) {
    regions_.push_back({base_address_, this->size(), initial_protection});
  }
}

std::uint64_t GuestMemory::base_address() const noexcept {
  return base_address_;
}

std::uint64_t GuestMemory::end_address() const noexcept {
  return base_address_ + size();
}

std::uint64_t GuestMemory::size() const noexcept {
  return static_cast<std::uint64_t>(bytes_.size());
}

bool GuestMemory::Contains(std::uint64_t address,
                           std::uint64_t length) const noexcept {
  if (address < base_address_) {
    return false;
  }

  const auto offset = address - base_address_;
  if (offset >= size()) {
    return false;
  }

  return length <= size() - offset;
}

bool GuestMemory::CanMap(std::uint64_t address,
                         std::uint64_t length) const noexcept {
  if (length == 0 || !Contains(address, length)) {
    return false;
  }

  const auto end_address = address + length;
  const auto insertion = std::lower_bound(
      regions_.begin(), regions_.end(), address,
      [](const GuestMemoryRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  if (insertion != regions_.begin()) {
    const auto& previous = *(insertion - 1);
    if (previous.address + previous.size > address) {
      return false;
    }
  }
  return insertion == regions_.end() || end_address <= insertion->address;
}

std::optional<std::uint64_t> GuestMemory::FindUnmappedRange(
    std::uint64_t search_start, std::uint64_t length,
    std::uint64_t alignment) const noexcept {
  if (length == 0 || alignment == 0 ||
      (alignment & (alignment - 1)) != 0) {
    return std::nullopt;
  }

  const auto align_up = [alignment](std::uint64_t address)
      -> std::optional<std::uint64_t> {
    const auto mask = alignment - 1;
    if (address > std::numeric_limits<std::uint64_t>::max() - mask) {
      return std::nullopt;
    }
    return (address + mask) & ~mask;
  };

  auto candidate = align_up(std::max(search_start, base_address_));
  if (!candidate.has_value()) {
    return std::nullopt;
  }
  for (const auto& region : regions_) {
    const auto region_end = region.address + region.size;
    if (region_end <= *candidate) {
      continue;
    }
    if (*candidate < region.address &&
        length <= region.address - *candidate) {
      return *candidate;
    }
    candidate = align_up(region_end);
    if (!candidate.has_value()) {
      return std::nullopt;
    }
  }
  return Contains(*candidate, length) ? candidate : std::nullopt;
}

bool GuestMemory::Map(std::uint64_t address, std::uint64_t length,
                      GuestMemoryProtection protection) {
  if (!IsValidProtection(protection) || !CanMap(address, length)) {
    return false;
  }

  const auto insertion = std::lower_bound(
      regions_.begin(), regions_.end(), address,
      [](const GuestMemoryRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  regions_.insert(insertion, {address, length, protection});
  CoalesceRegions();
  return true;
}

bool GuestMemory::Protect(std::uint64_t address, std::uint64_t length,
                          GuestMemoryProtection protection) {
  if (length == 0 || !IsValidProtection(protection) ||
      !IsMapped(address, length)) {
    return false;
  }

  const auto range_end = address + length;
  std::vector<GuestMemoryRegion> updated;
  updated.reserve(regions_.size() + 2);
  for (const auto& region : regions_) {
    const auto region_end = region.address + region.size;
    if (range_end <= region.address || address >= region_end) {
      updated.push_back(region);
      continue;
    }

    if (region.address < address) {
      updated.push_back(
          {region.address, address - region.address, region.protection});
    }
    const auto protected_start = std::max(region.address, address);
    const auto protected_end = std::min(region_end, range_end);
    updated.push_back(
        {protected_start, protected_end - protected_start, protection});
    if (range_end < region_end) {
      updated.push_back(
          {range_end, region_end - range_end, region.protection});
    }
  }

  regions_ = std::move(updated);
  CoalesceRegions();
  return true;
}

bool GuestMemory::Unmap(std::uint64_t address, std::uint64_t length) {
  if (length == 0 || !IsMapped(address, length)) {
    return false;
  }

  const auto range_end = address + length;
  std::vector<GuestMemoryRegion> updated;
  updated.reserve(regions_.size() + 1);
  for (const auto& region : regions_) {
    const auto region_end = region.address + region.size;
    if (range_end <= region.address || address >= region_end) {
      updated.push_back(region);
      continue;
    }

    if (region.address < address) {
      updated.push_back(
          {region.address, address - region.address, region.protection});
    }
    if (range_end < region_end) {
      updated.push_back(
          {range_end, region_end - range_end, region.protection});
    }
  }

  const auto offset = OffsetOf(address);
  std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              static_cast<std::size_t>(length), std::byte{0});
  regions_ = std::move(updated);
  CoalesceRegions();
  return true;
}

bool GuestMemory::IsMapped(std::uint64_t address,
                           std::uint64_t length) const noexcept {
  return CanAccess(address, length, GuestMemoryProtection::kNone);
}

bool GuestMemory::CanAccess(
    std::uint64_t address, std::uint64_t length,
    GuestMemoryProtection required_protection) const noexcept {
  if (!Contains(address, length)) {
    return false;
  }

  auto region_index = FindContainingRegion(address);
  if (region_index == regions_.size()) {
    return false;
  }

  auto current_address = address;
  auto remaining = length;
  while (region_index < regions_.size()) {
    const auto& region = regions_[region_index];
    const auto region_end = region.address + region.size;
    if (current_address < region.address || current_address >= region_end) {
      return false;
    }

    const auto actual = static_cast<std::uint8_t>(region.protection);
    const auto required = static_cast<std::uint8_t>(required_protection);
    if ((actual & required) != required) {
      return false;
    }
    if (remaining == 0) {
      return true;
    }

    const auto available = region_end - current_address;
    const auto chunk = std::min(remaining, available);
    remaining -= chunk;
    if (remaining == 0) {
      return true;
    }
    current_address += chunk;
    ++region_index;
  }
  return false;
}

bool GuestMemory::CanExecute(std::uint64_t address,
                             std::uint64_t length) const noexcept {
  return CanAccess(address, length, GuestMemoryProtection::kExecute);
}

std::optional<GuestMemoryRegion> GuestMemory::QueryRegion(
    std::uint64_t address) const noexcept {
  const auto index = FindContainingRegion(address);
  return index == regions_.size()
             ? std::nullopt
             : std::optional<GuestMemoryRegion>(regions_[index]);
}

std::span<const GuestMemoryRegion> GuestMemory::regions() const noexcept {
  return regions_;
}

bool GuestMemory::Read(std::uint64_t address,
                       std::span<std::byte> destination) const noexcept {
  if (!CanAccess(address, destination.size(),
                 GuestMemoryProtection::kRead)) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              destination.size(), destination.begin());
  return true;
}

bool GuestMemory::Write(std::uint64_t address,
                        std::span<const std::byte> source) noexcept {
  if (!CanAccess(address, source.size(),
                 GuestMemoryProtection::kWrite)) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::copy(source.begin(), source.end(),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
  return true;
}

bool GuestMemory::Fill(std::uint64_t address, std::uint64_t length,
                       std::byte value) noexcept {
  if (!CanAccess(address, length, GuestMemoryProtection::kWrite)) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              static_cast<std::size_t>(length), value);
  return true;
}

bool GuestMemory::Initialize(
    std::uint64_t address, std::span<const std::byte> source) noexcept {
  if (!IsMapped(address, source.size())) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::copy(source.begin(), source.end(),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
  return true;
}

bool GuestMemory::InitializeFill(std::uint64_t address,
                                 std::uint64_t length,
                                 std::byte value) noexcept {
  if (!IsMapped(address, length)) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              static_cast<std::size_t>(length), value);
  return true;
}

std::size_t GuestMemory::FindContainingRegion(
    std::uint64_t address) const noexcept {
  const auto insertion = std::lower_bound(
      regions_.begin(), regions_.end(), address,
      [](const GuestMemoryRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  if (insertion != regions_.end() && insertion->address == address) {
    return static_cast<std::size_t>(insertion - regions_.begin());
  }
  if (insertion == regions_.begin()) {
    return regions_.size();
  }

  const auto previous = insertion - 1;
  if (address < previous->address + previous->size) {
    return static_cast<std::size_t>(previous - regions_.begin());
  }
  return regions_.size();
}

std::size_t GuestMemory::OffsetOf(std::uint64_t address) const noexcept {
  return static_cast<std::size_t>(address - base_address_);
}

void GuestMemory::CoalesceRegions() {
  if (regions_.size() < 2) {
    return;
  }

  std::size_t output_index = 0;
  for (std::size_t input_index = 1; input_index < regions_.size();
       ++input_index) {
    const auto current = regions_[input_index];
    auto& previous = regions_[output_index];
    if (previous.address + previous.size == current.address &&
        previous.protection == current.protection) {
      previous.size += current.size;
    } else {
      ++output_index;
      regions_[output_index] = current;
    }
  }
  regions_.resize(output_index + 1);
}

}  // namespace kajps5::memory
