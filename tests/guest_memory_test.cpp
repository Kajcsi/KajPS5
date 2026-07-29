// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "core/memory/guest_memory.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "guest_memory_test: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using kajps5::memory::GuestMemory;

  GuestMemory memory(0x1000, 8);
  Check(memory.base_address() == 0x1000, "base address changed");
  Check(memory.end_address() == 0x1008, "end address is incorrect");
  Check(memory.size() == 8, "size is incorrect");
  Check(memory.Contains(0x1000, 8), "full range was rejected");
  Check(memory.Contains(0x1007, 1), "final byte was rejected");
  Check(!memory.Contains(0x0fff, 1), "address below the base was accepted");
  Check(!memory.Contains(0x1007, 2), "range beyond the end was accepted");
  Check(!memory.Contains(0x1008, 0), "unmapped end address was accepted");
  Check(!memory.Contains(std::numeric_limits<std::uint64_t>::max(), 2),
        "overflowing address range was accepted");

  const std::array input = {std::byte{0x11}, std::byte{0x22},
                            std::byte{0x33}, std::byte{0x44}};
  Check(memory.Write(0x1002, input), "valid write failed");

  std::array<std::byte, 4> output{};
  Check(memory.Read(0x1002, output), "valid read failed");
  Check(output == input, "read data differs from written data");

  const std::array rejected = {std::byte{0xaa}, std::byte{0xbb}};
  Check(!memory.Write(0x1007, rejected), "partial out-of-range write passed");
  Check(memory.Read(0x1002, output), "read after rejected write failed");
  Check(output == input, "rejected write changed memory");

  Check(memory.Fill(0x1003, 2, std::byte{0x7f}), "valid fill failed");
  const std::array expected = {std::byte{0x11}, std::byte{0x7f},
                               std::byte{0x7f}, std::byte{0x44}};
  Check(memory.Read(0x1002, output), "read after fill failed");
  Check(output == expected, "fill produced incorrect data");
  Check(memory.Read(0x1000, {}), "zero-length read at mapped address failed");
  Check(memory.Write(0x1000, {}), "zero-length write at mapped address failed");

  bool rejected_overflow = false;
  try {
    GuestMemory invalid(std::numeric_limits<std::uint64_t>::max() - 3, 8);
  } catch (const std::invalid_argument&) {
    rejected_overflow = true;
  }
  Check(rejected_overflow, "constructor accepted an overflowing range");

  return failures == 0 ? 0 : 1;
}
