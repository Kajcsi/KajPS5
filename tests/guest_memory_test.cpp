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
#include "core/memory/shared_memory_backing.h"

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
  Check(mapped.FindUnmappedRange(0x2000, 4, 4) == 0x200c,
        "first-fit search missed the first aligned gap");
  Check(mapped.FindUnmappedRange(0x200d, 4, 4) == 0x201c,
        "first-fit search did not realign after a mapped range");
  Check(!mapped.FindUnmappedRange(0x2000, 8, 3).has_value() &&
            !mapped.FindUnmappedRange(0x2000, 0, 4).has_value() &&
            !mapped.FindUnmappedRange(
                 std::numeric_limits<std::uint64_t>::max(), 4, 8)
                 .has_value(),
        "first-fit search accepted invalid or overflowing input");
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

  auto host_mapped = GuestMemory::CreateHostMapped(0x10000);
  Check(host_mapped && host_mapped->host_mapped(),
        "host-mapped guest memory allocation failed");
  if (host_mapped) {
    const auto host_base = host_mapped->base_address();
    const auto host_read_write = GuestMemoryProtection::kRead |
                                 GuestMemoryProtection::kWrite;
    Check(host_mapped->Map(host_base, 0x1000,
                           GuestMemoryProtection::kRead |
                               GuestMemoryProtection::kExecute),
          "host-mapped executable range did not map");
    const auto host_region = host_mapped->QueryRegion(host_base);
    const auto host_page_size = host_region ? host_region->size : 0;
    Check(host_page_size >= 0x1000 &&
              (host_page_size & (host_page_size - 1)) == 0 &&
              host_mapped->mapping_granularity() == host_page_size,
          "host-mapped range did not expand to a host page");
    Check(host_mapped->FindUnmappedRange(host_base + 1, 1, 1) ==
              host_base + host_page_size,
          "host-mapped free-range search ignored page granularity");
    Check(host_mapped->Initialize(host_base, input),
          "host-mapped executable range did not initialize");
    std::array<std::byte, input.size()> host_output{};
    Check(host_mapped->Read(host_base, host_output) &&
              host_output == input &&
              !host_mapped->Write(host_base, rejected),
          "host-mapped reads bypassed the logical protection model");
    Check(host_mapped->Protect(host_base, 1,
                               GuestMemoryProtection::kRead) &&
              !host_mapped->Write(host_base, rejected) &&
              host_mapped->Protect(host_base, host_page_size,
                                   host_read_write) &&
              host_mapped->Write(host_base, rejected) &&
              host_mapped->Unmap(host_base, 1) &&
              host_mapped->Map(host_base, 1, host_read_write),
          "host-mapped page protection or reuse failed");
    std::array<std::byte, 2> reused{};
    Check(host_mapped->Read(host_base, reused) &&
              reused == std::array<std::byte, 2>{},
          "host-mapped reuse exposed released bytes");
    const auto rejected_shared_backing =
        std::make_shared<kajps5::memory::SharedMemoryBacking>(0x4000);
    Check(!host_mapped->Map(host_base + 1, 0x1000,
                            GuestMemoryProtection::kRead) &&
              !host_mapped->Protect(host_base + 1, host_page_size,
                                    GuestMemoryProtection::kRead) &&
              !host_mapped->MapShared(host_base + 0x4000, 0x4000,
                                      host_read_write,
                                      rejected_shared_backing, 0),
          "host-mapped memory accepted an unsafe mapping layout");
  }

  auto shared_backing =
      std::make_shared<kajps5::memory::SharedMemoryBacking>(0xc000);
  GuestMemory shared(0x4000, 0x20000, GuestMemoryProtection::kNone);
  const auto shared_protection = GuestMemoryProtection::kRead |
                                 GuestMemoryProtection::kWrite;
  Check(shared.MapShared(0x4000, 0xc000, shared_protection,
                         shared_backing, 0) &&
            shared.MapShared(0x14000, 0xc000, shared_protection,
                             shared_backing, 0),
        "shared guest aliases could not be mapped");
  Check(!shared.MapShared(0xf000, 0x4000, shared_protection,
                          shared_backing, 0) &&
            !shared.MapShared(0x20000, 0x8000, shared_protection,
                              shared_backing, 0x8000) &&
            !shared.MapShared(0x10000, 0x4000, shared_protection,
                              nullptr, 0),
        "invalid shared guest mapping was accepted");
  const std::array shared_input = {
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
      std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80}};
  std::array<std::byte, shared_input.size()> shared_output{};
  Check(shared.Write(0x7ffc, shared_input) &&
            shared.Read(0x17ffc, shared_output) &&
            shared_output == shared_input,
        "shared guest aliases lost a cross-page write");
  Check(shared.Fill(0x18000, 0x4000, std::byte{0x5a}) &&
            shared.Read(0x8000, shared_output) &&
            std::all_of(shared_output.begin(), shared_output.end(),
                        [](std::byte value) {
                          return value == std::byte{0x5a};
                        }),
        "shared guest fill did not reach its alias");
  Check(shared.Unmap(0x8000, 0x4000) &&
            shared.Map(0x8000, 0x4000, shared_protection) &&
            shared.Read(0x8000, shared_output) &&
            shared_output == std::array<std::byte, shared_input.size()>{},
        "anonymous remap retained a removed shared alias");
  const std::array suffix_input = {std::byte{0xa1}, std::byte{0xb2},
                                   std::byte{0xc3}, std::byte{0xd4}};
  std::array<std::byte, suffix_input.size()> suffix_output{};
  Check(shared.Write(0x1c000, suffix_input) &&
            shared.Read(0xc000, suffix_output) &&
            suffix_output == suffix_input,
        "partial unmap broke the remaining shared suffix");
  const std::array first_page_tail = {
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
  std::array<std::byte, first_page_tail.size()> remapped_shared_output{};
  Check(shared.Unmap(0x4000, 0x4000) &&
            shared.MapShared(0x10000, 0x4000, shared_protection,
                             shared_backing, 0) &&
            shared.Read(0x13ffc, remapped_shared_output) &&
            remapped_shared_output == first_page_tail,
        "shared contents did not survive unmap and remap");
  shared_backing->Clear(0, shared_backing->size());
  shared_output.fill(std::byte{0xff});
  Check(shared.Read(0x14000, shared_output) &&
            shared_output == std::array<std::byte, shared_input.size()>{},
        "cleared shared backing retained guest data");

  return failures == 0 ? 0 : 1;
}
