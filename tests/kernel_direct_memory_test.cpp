// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <iostream>
#include <limits>

#include "kernel/direct_memory.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using kajps5::kernel::DirectMemoryService;
  using kajps5::kernel::KernelStatus;
  using kajps5::kernel::kDirectMemorySize;

  DirectMemoryService memory;
  Check(memory.size() == kDirectMemorySize,
        "direct-memory size is incorrect");
  const auto initial = memory.Available(0, kDirectMemorySize, 0);
  Check(initial && initial.address == 0 && initial.size == kDirectMemorySize,
        "initial direct-memory range is incorrect");

  const auto first = memory.Allocate(0, kDirectMemorySize, 0xc000, 0x4000, 1);
  const auto second =
      memory.Allocate(0, kDirectMemorySize, 0x4000, 0x4000, 2);
  Check(first && first.address == 0 && second && second.address == 0xc000 &&
            memory.allocation_count() == 2,
        "first-fit direct-memory allocation failed");
  Check(memory.RegisterMapping(0x200000, 0, 0xc000) ==
                KernelStatus::kOk &&
            memory.RegisterMapping(0x300000, 0, 0x4000) ==
                KernelStatus::kOk &&
            memory.mapping_count() == 2,
        "direct-memory aliases were not recorded");
  Check(memory.RegisterMapping(0x208000, 0, 0x4000) ==
                KernelStatus::kBusy &&
            memory.RegisterMapping(0x400000, 0x10000, 0x4000) ==
                KernelStatus::kNotFound &&
            memory.Release(0, 0x4000) == KernelStatus::kBusy,
        "direct-memory alias checks accepted an unsafe operation");
  memory.UnregisterMappings(0x204000, 0x4000);
  Check(memory.mapping_count() == 3 &&
            memory.Release(0, 0x4000) == KernelStatus::kBusy,
        "partial alias removal did not split the mapping");
  memory.UnregisterMappings(0x200000, 0xc000);
  Check(memory.mapping_count() == 1 &&
            memory.Release(0, 0x4000) == KernelStatus::kBusy,
        "alias removal changed an independent mapping");
  memory.UnregisterMappings(0x300000, 0x4000);
  Check(memory.mapping_count() == 0,
        "direct-memory aliases were not fully removed");
  Check(memory.Release(0x4000, 0x4000) == KernelStatus::kOk &&
            memory.ContainsAllocatedRange(0, 0x4000) &&
            !memory.ContainsAllocatedRange(0x4000, 0x4000) &&
            memory.ContainsAllocatedRange(0x8000, 0x4000) &&
            memory.allocation_count() == 3,
        "partial release did not split its allocation");

  const auto gap = memory.Available(0, 0xc000, 0x4000);
  Check(gap && gap.address == 0x4000 && gap.size == 0x4000,
        "available-range lookup missed a released gap");
  const auto reused = memory.Allocate(0, 0xc000, 0x4000, 0x4000, 3);
  Check(reused && reused.address == 0x4000,
        "released direct memory was not reused");

  Check(memory.Release(0, 0x4000) == KernelStatus::kOk &&
            memory.Release(0x4000, 0x4000) == KernelStatus::kOk &&
            memory.Release(0x8000, 0x4000) == KernelStatus::kOk &&
            memory.Release(0xc000, 0x4000) == KernelStatus::kOk &&
            memory.allocation_count() == 0,
        "direct-memory release did not clear all allocations");
  const auto coalesced = memory.Available(0, kDirectMemorySize, 0x4000);
  Check(coalesced && coalesced.address == 0 &&
            coalesced.size == kDirectMemorySize,
        "released direct memory did not coalesce");

  const auto odd_alignment = memory.Allocate(1, 32, 3, 3, 0);
  Check(odd_alignment && odd_alignment.address == 3,
        "non-power-of-two alignment was not handled safely");
  Check(memory.Release(3, 3) == KernelStatus::kOk,
        "odd-alignment allocation could not be released");

  Check(memory.Allocate(4, 4, 1, 1, 0).status ==
                KernelStatus::kInvalidArgument &&
            memory.Allocate(0, 4, 8, 1, 0).status ==
                KernelStatus::kNoResources &&
            memory.Release(0, 0) == KernelStatus::kInvalidArgument &&
            memory.Release(0, 0x4000) == KernelStatus::kNotFound &&
            !memory.Available(std::numeric_limits<std::uint64_t>::max(),
                              std::numeric_limits<std::uint64_t>::max(), 8),
        "invalid direct-memory range changed service state");

  std::cout << "kernel direct-memory tests passed\n";
  return 0;
}
