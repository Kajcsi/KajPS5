// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/memory/guest_memory.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace kajps5::memory {
namespace {

std::size_t ValidateSize(std::uint64_t base_address, std::size_t size) {
  const auto size64 = static_cast<std::uint64_t>(size);
  if (size64 > std::numeric_limits<std::uint64_t>::max() - base_address) {
    throw std::invalid_argument("Guest memory address range overflows.");
  }
  return size;
}

}  // namespace

GuestMemory::GuestMemory(std::uint64_t base_address, std::size_t size)
    : base_address_(base_address), bytes_(ValidateSize(base_address, size)) {}

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

bool GuestMemory::Read(std::uint64_t address,
                       std::span<std::byte> destination) const noexcept {
  if (!Contains(address, destination.size())) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              destination.size(), destination.begin());
  return true;
}

bool GuestMemory::Write(std::uint64_t address,
                        std::span<const std::byte> source) noexcept {
  if (!Contains(address, source.size())) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::copy(source.begin(), source.end(),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
  return true;
}

bool GuestMemory::Fill(std::uint64_t address, std::uint64_t length,
                       std::byte value) noexcept {
  if (!Contains(address, length)) {
    return false;
  }

  const auto offset = OffsetOf(address);
  std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              static_cast<std::size_t>(length), value);
  return true;
}

std::size_t GuestMemory::OffsetOf(std::uint64_t address) const noexcept {
  return static_cast<std::size_t>(address - base_address_);
}

}  // namespace kajps5::memory
