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
#include "hle/data_symbols.h"
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
      kajps5::hle::HleRegister::kRdx,
      kajps5::hle::HleRegister::kRcx};
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
  constexpr std::uint64_t kHeapTraceStorageAddress = 0x11000;
  GuestMemory memory(0x10000, 0x10000, GuestMemoryProtection::kNone);
  Check(memory.Map(kOutputAddress, 0x100,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "heap output cell did not map");
  Check(memory.Map(kHeapTraceStorageAddress,
                   kajps5::hle::kHleLibcHeapTraceStorageSize,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "heap trace storage did not map");

  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcExports(
            registry, runtime.cxa_guards(), runtime.process_lifecycle(),
            runtime.libc_heap(), memory, kHeapTraceStorageAddress) ==
                ExportRegistryStatus::kOk &&
            registry.size() == 110,
        "libc heap exports did not register atomically");
  const std::vector<std::string> libc_scope = {kajps5::hle::kLibcName};
  const std::vector<std::string> libc_internal_ext_scope = {
      kajps5::hle::kLibcInternalExtName};

  HleCallContext null_heap_trace(memory);
  SetArguments(null_heap_trace, {0});
  const auto null_heap_trace_result = registry.Dispatch(
      kajps5::hle::kLibcHeapGetTraceInfoNid, libc_internal_ext_scope,
      null_heap_trace);
  Check(null_heap_trace_result.status == ExportRegistryStatus::kOk &&
            null_heap_trace_result.handler_status ==
                kajps5::hle::HleContextStatus::kInvalidArgument &&
            ReturnValue(null_heap_trace) == 0,
        "heap trace accepted a null info pointer");

  HleCallContext unreadable_heap_trace(memory);
  SetArguments(unreadable_heap_trace, {0x20000});
  const auto unreadable_heap_trace_result = registry.Dispatch(
      kajps5::hle::kLibcHeapGetTraceInfoNid, libc_internal_ext_scope,
      unreadable_heap_trace);
  Check(unreadable_heap_trace_result.handler_status ==
                kajps5::hle::HleContextStatus::kMemoryFault &&
            ReturnValue(unreadable_heap_trace) == 0,
        "heap trace accepted an unreadable info pointer");

  constexpr std::uint64_t kHeapTraceInfoAddress = kOutputAddress + 0x40;
  HleCallContext heap_trace_setup(memory);
  Check(heap_trace_setup.WriteUInt64(kHeapTraceInfoAddress, 24) ==
            kajps5::hle::HleContextStatus::kOk,
        "heap trace wrong-size setup failed");
  HleCallContext wrong_size_heap_trace(memory);
  SetArguments(wrong_size_heap_trace, {kHeapTraceInfoAddress});
  const auto wrong_size_heap_trace_result = registry.Dispatch(
      kajps5::hle::kLibcHeapGetTraceInfoName, libc_internal_ext_scope,
      wrong_size_heap_trace);
  Check(wrong_size_heap_trace_result.handler_status ==
                kajps5::hle::HleContextStatus::kInvalidArgument &&
            ReturnValue(wrong_size_heap_trace) == 0,
        "heap trace accepted the wrong info size");

  constexpr std::uint64_t kTruncatedInfoAddress = kOutputAddress + 0xe8;
  constexpr std::uint64_t kTruncatedMaskAddress = kTruncatedInfoAddress + 16;
  Check(heap_trace_setup.WriteUInt64(kTruncatedInfoAddress, 32) ==
                kajps5::hle::HleContextStatus::kOk &&
            heap_trace_setup.WriteUInt64(kTruncatedMaskAddress,
                                         0xfeedfacefeedface) ==
                kajps5::hle::HleContextStatus::kOk,
        "heap trace truncated-output setup failed");
  HleCallContext truncated_heap_trace(memory);
  SetArguments(truncated_heap_trace, {kTruncatedInfoAddress});
  const auto truncated_heap_trace_result = registry.Dispatch(
      kajps5::hle::kLibcHeapGetTraceInfoNid, libc_internal_ext_scope,
      truncated_heap_trace);
  std::uint64_t truncated_marker = 0;
  Check(truncated_heap_trace_result.handler_status ==
                kajps5::hle::HleContextStatus::kMemoryFault &&
            ReturnValue(truncated_heap_trace) == 0 &&
            heap_trace_setup.ReadUInt64(kTruncatedMaskAddress,
                                         truncated_marker) ==
                kajps5::hle::HleContextStatus::kOk &&
            truncated_marker == 0xfeedfacefeedface,
        "heap trace partially wrote a truncated output range");

  Check(heap_trace_setup.WriteUInt64(kHeapTraceInfoAddress, 32) ==
            kajps5::hle::HleContextStatus::kOk,
        "heap trace valid setup failed");
  HleCallContext heap_trace(memory);
  SetArguments(heap_trace, {kHeapTraceInfoAddress});
  const auto heap_trace_result = registry.Dispatch(
      kajps5::hle::kLibcHeapGetTraceInfoNid, libc_internal_ext_scope,
      heap_trace);
  std::uint64_t heap_trace_mask = 0;
  std::uint64_t heap_trace_mstate = 0;
  std::array<std::byte,
             static_cast<std::size_t>(kajps5::hle::kHleLibcHeapTraceStorageSize)>
      heap_trace_storage{};
  Check(heap_trace_result && ReturnValue(heap_trace) == 0 &&
            heap_trace.ReadUInt64(kHeapTraceInfoAddress + 16,
                                  heap_trace_mask) ==
                kajps5::hle::HleContextStatus::kOk &&
            heap_trace.ReadUInt64(kHeapTraceInfoAddress + 24,
                                  heap_trace_mstate) ==
                kajps5::hle::HleContextStatus::kOk &&
            heap_trace_mask == kHeapTraceStorageAddress &&
            heap_trace_mstate == heap_trace_mask + sizeof(std::uint64_t) &&
            memory.Read(kHeapTraceStorageAddress, heap_trace_storage) &&
            std::all_of(heap_trace_storage.begin(), heap_trace_storage.end(),
                        [](std::byte value) { return value == std::byte{0}; }),
        "heap trace did not return stable zeroed guest storage");
  HleCallContext repeated_heap_trace(memory);
  SetArguments(repeated_heap_trace, {kHeapTraceInfoAddress});
  std::uint64_t repeated_mask = 0;
  std::uint64_t repeated_mstate = 0;
  Check(registry.Dispatch(kajps5::hle::kLibcHeapGetTraceInfoNid,
                          libc_internal_ext_scope, repeated_heap_trace) &&
            repeated_heap_trace.ReadUInt64(kHeapTraceInfoAddress + 16,
                                           repeated_mask) ==
                kajps5::hle::HleContextStatus::kOk &&
            repeated_heap_trace.ReadUInt64(kHeapTraceInfoAddress + 24,
                                           repeated_mstate) ==
                kajps5::hle::HleContextStatus::kOk &&
            repeated_mask == heap_trace_mask &&
            repeated_mstate == heap_trace_mstate,
        "heap trace storage addresses changed between calls");

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

  constexpr std::uint64_t kMspaceNameAddress = 0x10020;
  constexpr std::uint64_t kMspaceBase = 0x18000;
  constexpr std::uint64_t kMspaceSize = 0x4000;
  const std::array mspace_name = {
      std::byte{0x74}, std::byte{0x65}, std::byte{0x73}, std::byte{0x74},
      std::byte{0}};
  Check(memory.Write(kMspaceNameAddress, mspace_name) &&
            memory.Map(kMspaceBase, kMspaceSize,
                       GuestMemoryProtection::kRead |
                           GuestMemoryProtection::kWrite),
        "mspace backing range setup failed");
  HleCallContext create_mspace(memory);
  SetArguments(create_mspace,
               {kMspaceNameAddress, kMspaceBase, kMspaceSize, 0});
  Check(registry.Dispatch(kajps5::hle::kLibcMspaceCreateNid,
                          libc_scope, create_mspace) &&
            ReturnValue(create_mspace) == kMspaceBase &&
            runtime.libc_heap().mspace_count() == 1,
        "mspace creation did not retain its guest range");

  HleCallContext mspace_malloc(memory);
  SetArguments(mspace_malloc, {kMspaceBase, 24});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMspaceMallocNid, libc_scope,
            mspace_malloc)),
        "mspace malloc dispatch failed");
  const auto mspace_first = ReturnValue(mspace_malloc);
  Check(mspace_first >= kMspaceBase +
                            kajps5::kernel::kLibcMspaceMetadataBytes &&
            mspace_first % 16 == 0 &&
            memory.Write(mspace_first, pattern),
        "mspace malloc did not return writable guest memory");

  HleCallContext mspace_realloc(memory);
  SetArguments(mspace_realloc, {kMspaceBase, mspace_first, 64});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMspaceReallocName, libc_scope,
            mspace_realloc)),
        "mspace realloc dispatch failed");
  const auto mspace_resized = ReturnValue(mspace_realloc);
  copied.fill(std::byte{0});
  Check(mspace_resized != 0 && memory.Read(mspace_resized, copied) &&
            copied == pattern,
        "mspace realloc did not preserve allocation contents");

  HleCallContext mspace_reallocalign(memory);
  SetArguments(mspace_reallocalign,
               {kMspaceBase, mspace_resized, 256, 80});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMspaceReallocalignNid, libc_scope,
            mspace_reallocalign)),
        "mspace reallocalign dispatch failed");
  const auto mspace_realigned = ReturnValue(mspace_reallocalign);
  copied.fill(std::byte{0});
  Check(mspace_realigned != 0 && mspace_realigned % 256 == 0 &&
            memory.Read(mspace_realigned, copied) && copied == pattern,
        "mspace reallocalign lost its alignment or contents");

  HleCallContext mspace_calloc(memory);
  SetArguments(mspace_calloc, {kMspaceBase, 4, 8});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMspaceCallocNid, libc_scope,
            mspace_calloc)),
        "mspace calloc dispatch failed");
  const auto mspace_cleared = ReturnValue(mspace_calloc);
  cleared_bytes.fill(std::byte{0xff});
  Check(mspace_cleared != 0 &&
            memory.Read(mspace_cleared, cleared_bytes) &&
            cleared_bytes == zero_bytes,
        "mspace calloc did not clear its allocation");

  HleCallContext mspace_memalign(memory);
  SetArguments(mspace_memalign, {kMspaceBase, 64, 17});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcMspaceMemalignName, libc_scope,
            mspace_memalign)),
        "mspace memalign dispatch failed");
  const auto mspace_aligned = ReturnValue(mspace_memalign);
  Check(mspace_aligned != 0 && mspace_aligned % 64 == 0,
        "mspace memalign returned a misaligned address");

  HleCallContext mspace_posix(memory);
  SetArguments(mspace_posix,
               {kMspaceBase, kOutputAddress, 128, 9});
  Check(registry.Dispatch(kajps5::hle::kLibcMspacePosixMemalignNid,
                          libc_scope, mspace_posix) &&
            ReturnValue(mspace_posix) == 0 &&
            memory.Read(kOutputAddress,
                        std::as_writable_bytes(std::span(&output, 1))) &&
            output != 0 && output % 128 == 0,
        "mspace posix_memalign did not return aligned memory");
  const auto mspace_posix_allocation = output;

  HleCallContext mspace_usable(memory);
  SetArguments(mspace_usable, {mspace_realigned});
  const auto stats =
      runtime.libc_heap().MspaceStats(memory, kMspaceBase);
  Check(registry.Dispatch(
            kajps5::hle::kLibcMspaceMallocUsableSizeNid, libc_scope,
            mspace_usable) &&
            ReturnValue(mspace_usable) == 80 && stats.has_value() &&
            stats->capacity == kMspaceSize &&
            stats->current_in_use == 138 &&
            stats->maximum_in_use >= stats->current_in_use,
        "mspace size accounting is incorrect");

  HleCallContext mspace_not_empty(memory);
  SetArguments(mspace_not_empty, {kMspaceBase});
  HleCallContext busy_destroy(memory);
  SetArguments(busy_destroy, {kMspaceBase});
  Check(registry.Dispatch(kajps5::hle::kLibcMspaceIsHeapEmptyNid,
                          libc_scope, mspace_not_empty) &&
            ReturnValue(mspace_not_empty) == 0 &&
            registry.Dispatch(kajps5::hle::kLibcMspaceDestroyNid,
                              libc_scope, busy_destroy) &&
            ReturnValue(busy_destroy) == 1,
        "live mspace allocations did not block destruction");

  const std::array mspace_allocations = {
      mspace_realigned, mspace_cleared, mspace_aligned,
      mspace_posix_allocation};
  for (const auto allocation : mspace_allocations) {
    HleCallContext mspace_free(memory);
    SetArguments(mspace_free, {kMspaceBase, allocation});
    Check(static_cast<bool>(registry.Dispatch(
              kajps5::hle::kLibcMspaceFreeNid, libc_scope,
              mspace_free)),
          "mspace free dispatch failed");
  }
  HleCallContext mspace_empty(memory);
  SetArguments(mspace_empty, {kMspaceBase});
  HleCallContext destroy_mspace(memory);
  SetArguments(destroy_mspace, {kMspaceBase});
  Check(registry.Dispatch(kajps5::hle::kLibcMspaceIsHeapEmptyName,
                          libc_scope, mspace_empty) &&
            ReturnValue(mspace_empty) == 1 &&
            registry.Dispatch(kajps5::hle::kLibcMspaceDestroyName,
                              libc_scope, destroy_mspace) &&
            ReturnValue(destroy_mspace) == 0 &&
            runtime.libc_heap().mspace_count() == 0 &&
            memory.QueryRegion(kMspaceBase).has_value(),
        "empty mspace destruction changed caller-owned memory");

  const std::vector<std::string> wrong_scope = {"libkernel"};
  Check(registry.Dispatch(kajps5::hle::kLibcMallocNid, wrong_scope,
                          malloc_call)
                .status == ExportRegistryStatus::kNotFound,
        "libc heap export escaped its library scope");
  return failures == 0 ? 0 : 1;
}
