// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include "core/memory/guest_memory.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "guest_memory_test: " << message << '\n';
    ++failures;
  }
}

bool SameRegions(
    const std::vector<kajps5::memory::GuestMemoryRegion>& expected,
    std::span<const kajps5::memory::GuestMemoryRegion> actual) {
  return expected.size() == actual.size() &&
         std::equal(expected.begin(), expected.end(), actual.begin(),
                    [](const auto& left, const auto& right) {
                      return left.address == right.address &&
                             left.size == right.size &&
                             left.protection == right.protection;
                    });
}

}  // namespace

int main() {
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

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
  bool rejected_protection = false;
  try {
    GuestMemory invalid_protection(
        0x1000, 8, static_cast<GuestMemoryProtection>(0x80));
  } catch (const std::invalid_argument&) {
    rejected_protection = true;
  }
  Check(rejected_protection,
        "constructor accepted unknown protection bits");

  GuestMemory mapped(0x2000, 0x20, GuestMemoryProtection::kNone);
  Check(mapped.regions().empty(), "unmapped memory created a region");
  Check(mapped.Map(0x2008, 4, GuestMemoryProtection::kRead),
        "out-of-order read mapping failed");
  Check(mapped.Map(0x2000, 4,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "read-write mapping failed");
  Check(mapped.Map(0x2004, 4, GuestMemoryProtection::kRead),
        "adjacent read mapping failed");
  Check(mapped.Map(0x2010, 4, GuestMemoryProtection::kExecute),
        "execute mapping failed");
  Check(mapped.Map(0x2014, 4, GuestMemoryProtection::kWrite),
        "write-only mapping failed");
  Check(mapped.Map(0x2018, 4, GuestMemoryProtection::kNone),
        "no-access mapping failed");
  Check(mapped.regions().size() == 5, "mapping count is incorrect");
  Check(mapped.regions()[0].address == 0x2000 &&
            mapped.regions()[1].address == 0x2004 &&
            mapped.regions()[1].size == 8,
        "out-of-order mappings were not sorted and merged");
  Check(!mapped.Map(0x2003, 2, GuestMemoryProtection::kRead),
        "overlapping mapping was accepted");
  Check(!mapped.Map(0x1fff, 2, GuestMemoryProtection::kRead),
        "mapping outside the backing range was accepted");
  Check(!mapped.Map(0x201c, 4,
                    static_cast<GuestMemoryProtection>(0x80)),
        "mapping accepted unknown protection bits");
  const auto queried_region = mapped.QueryRegion(0x2005);
  Check(queried_region && queried_region->address == 0x2004 &&
            queried_region->size == 8 &&
            queried_region->protection == GuestMemoryProtection::kRead &&
            !mapped.QueryRegion(0x200c),
        "region query returned the wrong canonical range");

  const std::array mapped_input = {
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
      std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}};
  Check(mapped.Initialize(0x2000, mapped_input),
        "initialization across adjacent mappings failed");
  std::array<std::byte, 12> mapped_output{};
  Check(mapped.Read(0x2000, mapped_output),
        "read across adjacent readable mappings failed");
  Check(mapped_output == mapped_input,
        "adjacent mapping read returned incorrect data");

  const std::array crossing_write = {std::byte{0xaa}, std::byte{0xbb},
                                     std::byte{0xcc}, std::byte{0xdd}};
  Check(!mapped.Write(0x2002, crossing_write),
        "write across a read-only mapping succeeded");
  Check(mapped.Read(0x2000, mapped_output),
        "read after rejected permission write failed");
  Check(mapped_output == mapped_input,
        "rejected permission write changed memory");

  Check(mapped.CanExecute(0x2010, 4), "execute access was rejected");
  Check(!mapped.Read(0x2010, output), "execute-only memory was readable");
  Check(!mapped.Write(0x2010, rejected), "execute-only memory was writable");
  Check(mapped.Write(0x2014, rejected), "write-only memory rejected a write");
  Check(!mapped.Read(0x2014, output), "write-only memory was readable");
  Check(mapped.IsMapped(0x2018, 4), "no-access region is not mapped");
  Check(!mapped.Read(0x2018, output), "no-access memory was readable");
  Check(!mapped.Write(0x2018, rejected), "no-access memory was writable");
  Check(!mapped.CanExecute(0x2018, 1), "no-access memory was executable");
  Check(!mapped.Read(0x200a, output), "read across an unmapped gap passed");
  Check(mapped.Read(0x2000, {}),
        "zero-length read at a readable mapping failed");
  Check(!mapped.Read(0x200c, {}),
        "zero-length read at an unmapped address passed");

  Check(mapped.InitializeFill(0x2004, 4, std::byte{0}),
        "read-only initialization fill failed");
  std::array<std::byte, 4> cleared{};
  Check(mapped.Read(0x2004, cleared), "cleared read-only range is unreadable");
  Check(cleared == std::array<std::byte, 4>{},
        "initialization fill did not clear the range");

  GuestMemory mutable_regions(0x3000, 0x20,
                              GuestMemoryProtection::kNone);
  Check(mutable_regions.Map(0x3000, 8, GuestMemoryProtection::kRead) &&
            mutable_regions.Map(0x3008, 8,
                                GuestMemoryProtection::kWrite) &&
            mutable_regions.Map(0x3010, 8,
                                GuestMemoryProtection::kExecute),
        "mutable region setup failed");
  Check(mutable_regions.Protect(
            0x3004, 0x10,
            GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite),
        "protection across mapped regions failed");
  Check(mutable_regions.regions().size() == 3 &&
            mutable_regions.regions()[0].address == 0x3000 &&
            mutable_regions.regions()[0].size == 4 &&
            mutable_regions.regions()[0].protection ==
                GuestMemoryProtection::kRead &&
            mutable_regions.regions()[1].address == 0x3004 &&
            mutable_regions.regions()[1].size == 0x10 &&
            mutable_regions.regions()[1].protection ==
                (GuestMemoryProtection::kRead |
                 GuestMemoryProtection::kWrite) &&
            mutable_regions.regions()[2].address == 0x3014 &&
            mutable_regions.regions()[2].size == 4 &&
            mutable_regions.regions()[2].protection ==
                GuestMemoryProtection::kExecute,
        "protection did not split and merge regions canonically");
  Check(!mutable_regions.Protect(0x3000, 0,
                                 GuestMemoryProtection::kRead),
        "zero-length protection succeeded");
  const auto protected_regions =
      std::vector<kajps5::memory::GuestMemoryRegion>(
          mutable_regions.regions().begin(),
          mutable_regions.regions().end());
  Check(!mutable_regions.Protect(
            0x3000, 4, static_cast<GuestMemoryProtection>(0x80)) &&
            SameRegions(protected_regions, mutable_regions.regions()),
        "invalid protection changed the region table");

  const std::array released_data = {
      std::byte{0x41}, std::byte{0x42}, std::byte{0x43}, std::byte{0x44}};
  Check(mutable_regions.Initialize(0x3008, released_data),
        "released-byte setup failed");
  Check(mutable_regions.Unmap(0x3008, 4) &&
            !mutable_regions.IsMapped(0x3008, 1),
        "unmap did not create a checked gap");
  const auto unmapped_regions =
      std::vector<kajps5::memory::GuestMemoryRegion>(
          mutable_regions.regions().begin(),
          mutable_regions.regions().end());
  Check(!mutable_regions.Protect(0x3004, 8,
                                 GuestMemoryProtection::kRead) &&
            !mutable_regions.Unmap(0x3004, 8) &&
            SameRegions(unmapped_regions, mutable_regions.regions()),
        "operation across an existing gap changed the region table");
  Check(mutable_regions.Map(
            0x3008, 4,
            GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite),
        "released range could not be remapped");
  std::array<std::byte, 4> remapped_data{};
  Check(mutable_regions.Read(0x3008, remapped_data) &&
            remapped_data == std::array<std::byte, 4>{},
        "remapped range exposed released bytes");
  Check(!mutable_regions.Unmap(0x3000, 0),
        "zero-length unmap succeeded");

  return failures == 0 ? 0 : 1;
}
