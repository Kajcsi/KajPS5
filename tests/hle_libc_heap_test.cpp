// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/libc_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_libc_heap_test: " << message << '\n';
    ++failures;
  }
}

void SetArguments(kajps5::hle::HleCallContext& context,
                  std::initializer_list<std::uint64_t> arguments) {
  constexpr std::array registers = {
      kajps5::hle::HleRegister::kRdi,
      kajps5::hle::HleRegister::kRsi,
      kajps5::hle::HleRegister::kRdx};
  std::size_t index = 0;
  for (const auto argument : arguments) {
    Check(index < registers.size() &&
              context.SetRegister(registers[index], argument),
          "heap argument setup failed");
    ++index;
  }
}

std::uint64_t ReturnValue(const kajps5::hle::HleCallContext& context) {
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kOutputAddress = 0x10000;
  GuestMemory memory(0x10000, 0x10000, GuestMemoryProtection::kNone);
  Check(memory.Map(kOutputAddress, 8,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "heap output cell did not map");

  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcExports(
            registry, runtime.cxa_guards(), runtime.process_lifecycle(),
            runtime.libc_heap(), memory) == ExportRegistryStatus::kOk &&
            registry.size() == 36,
        "libc heap exports did not register atomically");
  const std::vector<std::string> libc_scope = {kajps5::hle::kLibcName};

  HleCallContext malloc_call(memory);
  SetArguments(malloc_call, {24});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMallocNid, libc_scope, malloc_call)),
        "malloc dispatch failed");
  const auto first = ReturnValue(malloc_call);
  Check(first != 0 && first % 16 == 0 &&
            runtime.libc_heap().allocation_count() == 1,
        "malloc did not reserve aligned guest memory");

  const std::array pattern = {
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
      std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80}};
  Check(memory.Write(first, pattern), "malloc contents were not writable");
  HleCallContext usable_call(memory);
  SetArguments(usable_call, {first});
  Check(registry.Dispatch(kajps5::hle::kLibcMallocUsableSizeNid,
                          libc_scope, usable_call) &&
            ReturnValue(usable_call) == 24,
        "malloc usable size was not retained");

  HleCallContext realloc_call(memory);
  SetArguments(realloc_call, {first, 64});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcReallocName, libc_scope, realloc_call)),
        "realloc dispatch failed");
  const auto resized = ReturnValue(realloc_call);
  std::array<std::byte, pattern.size()> copied{};
  Check(resized != 0 && resized != first && memory.Read(resized, copied) &&
            copied == pattern && !memory.QueryRegion(first).has_value(),
        "realloc did not move the checked guest allocation");

  HleCallContext calloc_call(memory);
  SetArguments(calloc_call, {4, 8});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcCallocNid, libc_scope, calloc_call)),
        "calloc dispatch failed");
  const auto cleared = ReturnValue(calloc_call);
  std::array<std::byte, 32> zero_bytes{};
  std::array<std::byte, 32> cleared_bytes{};
  Check(cleared != 0 && memory.Read(cleared, cleared_bytes) &&
            cleared_bytes == zero_bytes,
        "calloc did not clear the guest allocation");

  HleCallContext overflow_call(memory);
  SetArguments(overflow_call,
               {std::numeric_limits<std::uint64_t>::max(), 2});
  Check(registry.Dispatch(kajps5::hle::kLibcCallocName, libc_scope,
                          overflow_call) &&
            ReturnValue(overflow_call) == 0,
        "overflowing calloc did not fail with a null result");

  HleCallContext memalign_call(memory);
  SetArguments(memalign_call, {64, 17});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMemalignNid, libc_scope, memalign_call)),
        "memalign dispatch failed");
  const auto aligned = ReturnValue(memalign_call);
  Check(aligned != 0 && aligned % 64 == 0,
        "memalign returned a misaligned guest address");

  HleCallContext invalid_aligned_alloc(memory);
  SetArguments(invalid_aligned_alloc, {64, 65});
  Check(registry.Dispatch(kajps5::hle::kLibcAlignedAllocNid, libc_scope,
                          invalid_aligned_alloc) &&
            ReturnValue(invalid_aligned_alloc) == 0,
        "aligned_alloc accepted a partial alignment unit");
  HleCallContext aligned_alloc_call(memory);
  SetArguments(aligned_alloc_call, {64, 64});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcAlignedAllocName, libc_scope,
            aligned_alloc_call)),
        "aligned_alloc dispatch failed");
  const auto aligned_alloc = ReturnValue(aligned_alloc_call);
  Check(aligned_alloc != 0 && aligned_alloc % 64 == 0,
        "aligned_alloc returned a misaligned guest address");

  const std::array output_marker = {
      std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa},
      std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa}};
  Check(memory.Write(kOutputAddress, output_marker),
        "posix_memalign output setup failed");
  HleCallContext invalid_posix(memory);
  SetArguments(invalid_posix, {kOutputAddress, 12, 32});
  Check(registry.Dispatch(kajps5::hle::kLibcPosixMemalignNid, libc_scope,
                          invalid_posix) &&
            ReturnValue(invalid_posix) == 22,
        "posix_memalign accepted an invalid alignment");
  std::uint64_t output = 1;
  Check(memory.Read(kOutputAddress,
                    std::as_writable_bytes(std::span(&output, 1))) &&
            output == 0,
        "invalid posix_memalign did not clear its output");

  HleCallContext posix_call(memory);
  SetArguments(posix_call, {kOutputAddress, 128, 9});
  Check(registry.Dispatch(kajps5::hle::kLibcPosixMemalignName, libc_scope,
                          posix_call) &&
            ReturnValue(posix_call) == 0 &&
            memory.Read(kOutputAddress,
                        std::as_writable_bytes(std::span(&output, 1))) &&
            output != 0 && output % 128 == 0,
        "posix_memalign did not return an aligned guest address");
  const auto posix_allocation = output;

  const std::array allocations = {
      resized, cleared, aligned, aligned_alloc, posix_allocation};
  for (const auto allocation : allocations) {
    HleCallContext free_call(memory);
    SetArguments(free_call, {allocation});
    Check(registry.Dispatch(kajps5::hle::kLibcFreeNid, libc_scope,
                            free_call) &&
              ReturnValue(free_call) == 0,
          "free dispatch failed");
  }
  HleCallContext unknown_free(memory);
  SetArguments(unknown_free, {0x1ffff});
  Check(registry.Dispatch(kajps5::hle::kLibcFreeName, libc_scope,
                          unknown_free) &&
            runtime.libc_heap().allocation_count() == 0,
        "unknown free changed the guest heap");

  const std::vector<std::string> wrong_scope = {"libkernel"};
  Check(registry.Dispatch(kajps5::hle::kLibcMallocNid, wrong_scope,
                          malloc_call)
                .status == ExportRegistryStatus::kNotFound,
        "libc heap export escaped its library scope");
  return failures == 0 ? 0 : 1;
}
