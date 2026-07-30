// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/kernel_memory_exports.h"
#include "kernel/direct_memory.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_memory_exports_test: " << message << '\n';
    ++failures;
  }
}

std::uint64_t KernelResult(std::int32_t result) {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
}

std::uint64_t Dispatch(kajps5::hle::ExportRegistry& registry,
                       std::string_view symbol,
                       kajps5::hle::HleCallContext& context) {
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(static_cast<bool>(result), "memory export dispatch failed");
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleRegister;
  using kajps5::kernel::DirectMemoryService;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(0x4000, 0xc000);
  DirectMemoryService direct_memory;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelMemoryExports(registry, direct_memory) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 30,
        "memory exports did not register atomically");

  HleCallContext page_size_context(memory);
  Check(Dispatch(registry, kajps5::hle::kPosixGetPageSizeNid,
                 page_size_context) == kajps5::hle::kKernelMemoryPageSize,
        "getpagesize returned the wrong guest page size");

  HleCallContext direct_size_context(memory);
  Check(Dispatch(registry, kajps5::hle::kKernelGetDirectMemorySizeNid,
                 direct_size_context) == direct_memory.size(),
        "direct-memory size export returned the wrong value");

  HleCallContext available_context(memory);
  Check(available_context.SetRegister(HleRegister::kRdi, 0) &&
            available_context.SetRegister(HleRegister::kRsi,
                                          direct_memory.size()) &&
            available_context.SetRegister(HleRegister::kRdx, 0x4000) &&
            available_context.SetRegister(HleRegister::kRcx, 0x4100) &&
            available_context.SetRegister(HleRegister::kR8, 0x4108),
        "available direct-memory setup failed");
  std::uint64_t available_address = 1;
  std::uint64_t available_size = 0;
  Check(Dispatch(registry,
                 kajps5::hle::kKernelAvailableDirectMemorySizeNid,
                 available_context) == 0 &&
            available_context.ReadUInt64(0x4100, available_address) ==
                kajps5::hle::HleContextStatus::kOk &&
            available_context.ReadUInt64(0x4108, available_size) ==
                kajps5::hle::HleContextStatus::kOk &&
            available_address == 0 &&
            available_size == direct_memory.size(),
        "available direct-memory export returned the wrong range");

  HleCallContext allocate_context(memory);
  Check(allocate_context.SetRegister(HleRegister::kRdi, 0) &&
            allocate_context.SetRegister(HleRegister::kRsi,
                                         direct_memory.size()) &&
            allocate_context.SetRegister(HleRegister::kRdx, 0x8000) &&
            allocate_context.SetRegister(HleRegister::kRcx, 0x4000) &&
            allocate_context.SetRegister(HleRegister::kR8, 42) &&
            allocate_context.SetRegister(HleRegister::kR9, 0x4120),
        "direct-memory allocation setup failed");
  std::uint64_t direct_address = 1;
  Check(Dispatch(registry, kajps5::hle::kKernelAllocateDirectMemoryNid,
                 allocate_context) == 0 &&
            allocate_context.ReadUInt64(0x4120, direct_address) ==
                kajps5::hle::HleContextStatus::kOk &&
            direct_address == 0 &&
            direct_memory.ContainsAllocatedRange(0, 0x8000),
        "direct-memory allocation returned the wrong range");

  HleCallContext main_allocate_context(memory);
  Check(main_allocate_context.SetRegister(HleRegister::kRdi, 0x4000) &&
            main_allocate_context.SetRegister(HleRegister::kRsi, 0x8000) &&
            main_allocate_context.SetRegister(HleRegister::kRdx, 7) &&
            main_allocate_context.SetRegister(HleRegister::kRcx, 0x4128),
        "main direct-memory allocation setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelAllocateMainDirectMemoryName,
                 main_allocate_context) == 0 &&
            main_allocate_context.ReadUInt64(0x4128, direct_address) ==
                kajps5::hle::HleContextStatus::kOk &&
            direct_address == 0x8000 &&
            direct_memory.ContainsAllocatedRange(0x8000, 0x4000),
        "main direct-memory allocation did not use first fit");

  const auto allocations_before_fault = direct_memory.allocation_count();
  HleCallContext allocation_fault_context(memory);
  Check(allocation_fault_context.SetRegister(HleRegister::kRsi,
                                             direct_memory.size()) &&
            allocation_fault_context.SetRegister(HleRegister::kRdx, 0x4000) &&
            allocation_fault_context.SetRegister(HleRegister::kR9, 0x1000),
        "direct-memory output fault setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelAllocateDirectMemoryName,
                 allocation_fault_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault) &&
            direct_memory.allocation_count() == allocations_before_fault,
        "invalid direct-memory output changed allocations");

  HleCallContext exhausted_context(memory);
  Check(exhausted_context.SetRegister(HleRegister::kRsi,
                                      direct_memory.size()) &&
            exhausted_context.SetRegister(HleRegister::kRdx,
                                          direct_memory.size()) &&
            exhausted_context.SetRegister(HleRegister::kR9, 0x4130),
        "direct-memory exhaustion setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelAllocateDirectMemoryNid,
                 exhausted_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorTryAgain),
        "direct-memory exhaustion returned the wrong error");

  HleCallContext checked_release_context(memory);
  Check(checked_release_context.SetRegister(HleRegister::kRdi, 0x4000) &&
            checked_release_context.SetRegister(HleRegister::kRsi, 0x4000),
        "checked direct-memory release setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelCheckedReleaseDirectMemoryNid,
                 checked_release_context) == 0 &&
            !direct_memory.ContainsAllocatedRange(0x4000, 0x4000) &&
            direct_memory.ContainsAllocatedRange(0, 0x4000),
        "checked direct-memory release did not split its allocation");

  HleCallContext invalid_release_context(memory);
  Check(invalid_release_context.SetRegister(HleRegister::kRdi, 1) &&
            invalid_release_context.SetRegister(HleRegister::kRsi, 0x4000),
        "invalid direct-memory release setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelCheckedReleaseDirectMemoryName,
                 invalid_release_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument) &&
            direct_memory.ContainsAllocatedRange(0, 0x4000),
        "unaligned checked release changed direct memory");

  HleCallContext missing_release_context(memory);
  Check(missing_release_context.SetRegister(HleRegister::kRdi, 0x10000) &&
            missing_release_context.SetRegister(HleRegister::kRsi, 0x4000),
        "missing direct-memory release setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReleaseDirectMemoryNid,
                 missing_release_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorPermissionDenied),
        "unknown direct-memory release returned the wrong error");

  GuestMemory flexible_memory(0x10000, 0x20000,
                              GuestMemoryProtection::kNone);
  Check(flexible_memory.Map(
            0x10000, 0x4000,
            GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite),
        "flexible-memory control page setup failed");
  HleCallContext flexible_context(flexible_memory);
  Check(flexible_context.WriteUInt64(0x10000, 0x18001) ==
            kajps5::hle::HleContextStatus::kOk &&
            flexible_context.SetRegister(HleRegister::kRdi, 0x10000) &&
            flexible_context.SetRegister(HleRegister::kRsi, 0x4000) &&
            flexible_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuRead |
                    kajps5::hle::kKernelProtectionCpuWrite),
        "flexible-memory map setup failed");
  std::uint64_t mapped_address = 0;
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapFlexibleMemoryNid,
                 flexible_context) == 0 &&
            flexible_context.ReadUInt64(0x10000, mapped_address) ==
                kajps5::hle::HleContextStatus::kOk &&
            mapped_address == 0x1c000 &&
            flexible_memory.CanAccess(
                mapped_address, 0x4000,
                GuestMemoryProtection::kRead |
                    GuestMemoryProtection::kWrite),
        "hinted flexible-memory map returned the wrong range");

  HleCallContext fixed_collision(flexible_memory);
  Check(fixed_collision.WriteUInt64(0x10008, mapped_address) ==
            kajps5::hle::HleContextStatus::kOk &&
            fixed_collision.SetRegister(HleRegister::kRdi, 0x10008) &&
            fixed_collision.SetRegister(HleRegister::kRsi, 0x4000) &&
            fixed_collision.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuRead) &&
            fixed_collision.SetRegister(
                HleRegister::kRcx,
                kajps5::hle::kKernelMapFixed |
                    kajps5::hle::kKernelMapNoOverwrite),
        "fixed collision setup failed");
  std::uint64_t unchanged_address = 0;
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapFlexibleMemoryName,
                 fixed_collision) ==
                KernelResult(kajps5::hle::kKernelHleErrorNoMemory) &&
            fixed_collision.ReadUInt64(0x10008, unchanged_address) ==
                kajps5::hle::HleContextStatus::kOk &&
            unchanged_address == mapped_address,
        "fixed no-overwrite map replaced an existing range");

  const std::array map_name = {
      std::byte{'h'}, std::byte{'e'}, std::byte{'a'}, std::byte{'p'},
      std::byte{0}};
  Check(flexible_memory.Write(0x10080, map_name),
        "named flexible-memory string setup failed");
  HleCallContext named_context(flexible_memory);
  Check(named_context.WriteUInt64(0x10010, 0) ==
            kajps5::hle::HleContextStatus::kOk &&
            named_context.SetRegister(HleRegister::kRdi, 0x10010) &&
            named_context.SetRegister(HleRegister::kRsi, 0x4000) &&
            named_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuExecute) &&
            named_context.SetRegister(HleRegister::kR8, 0x10080),
        "named flexible-memory map setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapNamedFlexibleMemoryNid,
                 named_context) == 0 &&
            named_context.ReadUInt64(0x10010, mapped_address) ==
                kajps5::hle::HleContextStatus::kOk &&
            mapped_address == 0x14000 &&
            flexible_memory.CanExecute(mapped_address, 0x4000),
        "named flexible-memory map did not use the checked fallback range");

  HleCallContext fixed_context(flexible_memory);
  Check(fixed_context.WriteUInt64(0x10018, 0x24000) ==
            kajps5::hle::HleContextStatus::kOk &&
            fixed_context.SetRegister(HleRegister::kRdi, 0x10018) &&
            fixed_context.SetRegister(HleRegister::kRsi, 0x4000) &&
            fixed_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionGpuRead) &&
            fixed_context.SetRegister(HleRegister::kRcx,
                                      kajps5::hle::kKernelMapFixed),
        "fixed flexible-memory map setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapFlexibleMemoryInternalNid,
                 fixed_context) == 0 &&
            flexible_memory.IsMapped(0x24000, 0x4000) &&
            !flexible_memory.CanAccess(0x24000, 1,
                                       GuestMemoryProtection::kRead),
        "fixed flexible-memory map returned the wrong protection");

  std::array<std::byte, 32> long_name{};
  long_name.fill(std::byte{'x'});
  Check(flexible_memory.Write(0x10100, long_name),
        "long map name setup failed");
  const auto flexible_region_count = flexible_memory.regions().size();
  HleCallContext long_name_context(flexible_memory);
  Check(long_name_context.WriteUInt64(0x10020, 0x28000) ==
            kajps5::hle::HleContextStatus::kOk &&
            long_name_context.SetRegister(HleRegister::kRdi, 0x10020) &&
            long_name_context.SetRegister(HleRegister::kRsi, 0x4000) &&
            long_name_context.SetRegister(HleRegister::kRdx, 0) &&
            long_name_context.SetRegister(HleRegister::kR8, 0x10100),
        "long map name dispatch setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapNamedFlexibleMemoryName,
                 long_name_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorNameTooLong) &&
            flexible_memory.regions().size() == flexible_region_count,
        "overlong map name changed guest memory");

  HleCallContext invalid_map_context(flexible_memory);
  Check(invalid_map_context.SetRegister(HleRegister::kRdi, 0x18000) &&
            invalid_map_context.SetRegister(HleRegister::kRsi, 0x4000),
        "invalid flexible-memory map setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapFlexibleMemoryNid,
                 invalid_map_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault),
        "unmapped flexible-memory output pointer was accepted");
  HleCallContext invalid_flags_context(flexible_memory);
  Check(invalid_flags_context.WriteUInt64(0x10028, 0x28000) ==
            kajps5::hle::HleContextStatus::kOk &&
            invalid_flags_context.SetRegister(HleRegister::kRdi, 0x10028) &&
            invalid_flags_context.SetRegister(HleRegister::kRsi, 0x4000) &&
            invalid_flags_context.SetRegister(HleRegister::kRcx, 0x08),
        "invalid flexible-memory flags setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelMapFlexibleMemoryNid,
                 invalid_flags_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument) &&
            !flexible_memory.IsMapped(0x28000, 1),
        "unknown flexible-memory flags changed guest memory");

  HleCallContext read_context(memory);
  Check(read_context.SetRegister(HleRegister::kRdi, 0x5000) &&
            read_context.SetRegister(HleRegister::kRsi, 0x100) &&
            read_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuRead),
        "read protection setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelMprotectNid,
                 read_context) == 0 &&
            memory.CanAccess(0x4000, 0x4000,
                             GuestMemoryProtection::kRead) &&
            !memory.CanAccess(0x4000, 1,
                              GuestMemoryProtection::kWrite),
        "NID mprotect did not normalize to a 16 KiB page");

  const std::array released_data = {
      std::byte{0x41}, std::byte{0x42}, std::byte{0x43}, std::byte{0x44}};
  Check(memory.Initialize(0x8000, released_data),
        "unmap data setup failed");
  HleCallContext gpu_context(memory);
  Check(gpu_context.SetRegister(HleRegister::kRdi, 0x8000) &&
            gpu_context.SetRegister(HleRegister::kRsi, 0x4000) &&
            gpu_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionGpuRead),
        "GPU-only protection setup failed");
  Check(Dispatch(registry, kajps5::hle::kPosixMprotectName,
                 gpu_context) == 0 &&
            memory.IsMapped(0x8000, 0x4000) &&
            !memory.CanAccess(0x8000, 1, GuestMemoryProtection::kRead) &&
            !memory.CanAccess(0x8000, 1, GuestMemoryProtection::kWrite) &&
            !memory.CanExecute(0x8000, 1),
        "GPU-only protection granted guest CPU access");

  HleCallContext query_context(memory);
  Check(query_context.SetRegister(HleRegister::kRdi, 0x8001) &&
            query_context.SetRegister(HleRegister::kRsi, 0xf000) &&
            query_context.SetRegister(HleRegister::kRdx, 0xf008) &&
            query_context.SetRegister(HleRegister::kRcx, 0xf010),
        "memory protection query setup failed");
  std::uint64_t query_start = 0;
  std::uint64_t query_end = 0;
  std::uint32_t query_protection = 0;
  Check(Dispatch(registry,
                 kajps5::hle::kKernelQueryMemoryProtectionNid,
                 query_context) == 0 &&
            query_context.ReadUInt64(0xf000, query_start) ==
                kajps5::hle::HleContextStatus::kOk &&
            query_context.ReadUInt64(0xf008, query_end) ==
                kajps5::hle::HleContextStatus::kOk &&
            query_context.ReadUInt32(0xf010, query_protection) ==
                kajps5::hle::HleContextStatus::kOk &&
            query_start == 0x8000 && query_end == 0xc000 &&
            query_protection == kajps5::hle::kKernelProtectionGpuRead,
        "memory protection query did not round-trip GPU metadata");

  HleCallContext fault_query_context(memory);
  constexpr std::uint64_t kQuerySentinel = 0x1122334455667788;
  Check(fault_query_context.WriteUInt64(0xf020, kQuerySentinel) ==
            kajps5::hle::HleContextStatus::kOk &&
            fault_query_context.SetRegister(HleRegister::kRdi, 0x8000) &&
            fault_query_context.SetRegister(HleRegister::kRsi, 0xf020) &&
            fault_query_context.SetRegister(HleRegister::kRdx, 0x20000),
        "faulting memory query setup failed");
  query_start = 0;
  Check(Dispatch(registry,
                 kajps5::hle::kKernelQueryMemoryProtectionName,
                 fault_query_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault) &&
            fault_query_context.ReadUInt64(0xf020, query_start) ==
                kajps5::hle::HleContextStatus::kOk &&
            query_start == kQuerySentinel,
        "faulting memory query changed an earlier output");

  HleCallContext execute_context(memory);
  Check(execute_context.SetRegister(HleRegister::kRdi, 0xc001) &&
            execute_context.SetRegister(HleRegister::kRsi, 1) &&
            execute_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuExecute),
        "execute protection setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelMprotectName,
                 execute_context) == 0 &&
            memory.CanExecute(0xc000, 0x4000) &&
            !memory.CanAccess(0xc000, 1, GuestMemoryProtection::kRead),
        "named mprotect returned the wrong execute permissions");

  const auto region_count = memory.regions().size();
  HleCallContext invalid_protection_context(memory);
  Check(invalid_protection_context.SetRegister(HleRegister::kRdi, 0x4000) &&
            invalid_protection_context.SetRegister(HleRegister::kRsi,
                                                   0x4000) &&
            invalid_protection_context.SetRegister(HleRegister::kRdx, 0x80),
        "invalid protection setup failed");
  Check(Dispatch(registry, kajps5::hle::kPosixMprotectNid,
                 invalid_protection_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument) &&
            memory.regions().size() == region_count &&
            memory.CanAccess(0x4000, 1, GuestMemoryProtection::kRead),
        "invalid protection changed guest memory");

  HleCallContext missing_protection_context(memory);
  Check(missing_protection_context.SetRegister(HleRegister::kRdi, 0xc000) &&
            missing_protection_context.SetRegister(HleRegister::kRsi,
                                                   0x8000) &&
            missing_protection_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuRead),
        "missing protection range setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelMprotectName,
                 missing_protection_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorPermissionDenied) &&
            memory.CanExecute(0xc000, 1),
        "failed protection changed a mapped page");

  HleCallContext zero_protection_context(memory);
  Check(zero_protection_context.SetRegister(HleRegister::kRdi, 0x4000),
        "zero protection range setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelMprotectName,
                 zero_protection_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "zero-length mprotect was accepted");
  HleCallContext overflow_context(memory);
  Check(overflow_context.SetRegister(HleRegister::kRdi,
                                     UINT64_MAX - 0x100) &&
            overflow_context.SetRegister(HleRegister::kRsi, 0x200) &&
            overflow_context.SetRegister(
                HleRegister::kRdx,
                kajps5::hle::kKernelProtectionCpuRead),
        "overflow protection setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelMprotectNid,
                 overflow_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "overflowing mprotect range was accepted");

  HleCallContext unmap_context(memory);
  Check(unmap_context.SetRegister(HleRegister::kRdi, 0x8000) &&
            unmap_context.SetRegister(HleRegister::kRsi, 0x4000),
        "unmap setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelMunmapNid,
                 unmap_context) == 0 &&
            !memory.IsMapped(0x8000, 1),
        "NID munmap did not release the page");
  HleCallContext missing_query_context(memory);
  Check(missing_query_context.SetRegister(HleRegister::kRdi, 0x8000),
        "missing memory query setup failed");
  Check(Dispatch(registry,
                 kajps5::hle::kKernelQueryMemoryProtectionName,
                 missing_query_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorPermissionDenied),
        "unmapped memory query returned the wrong result");
  HleCallContext gap_context(memory);
  Check(gap_context.SetRegister(HleRegister::kRdi, 0x7000) &&
            gap_context.SetRegister(HleRegister::kRsi, 0x2000),
        "gap unmap setup failed");
  Check(Dispatch(registry, kajps5::hle::kPosixMunmapName,
                 gap_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorPermissionDenied) &&
            memory.IsMapped(0x7000, 0x1000),
        "failed unmap changed the mapped prefix");

  Check(memory.Map(0x8000, 0x4000,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "released page could not be remapped");
  std::array<std::byte, 4> remapped_data{};
  Check(memory.Read(0x8000, remapped_data) &&
            remapped_data == std::array<std::byte, 4>{},
        "HLE remap exposed released bytes");

  HleCallContext invalid_unmap_context(memory);
  Check(Dispatch(registry, kajps5::hle::kPosixMunmapNid,
                 invalid_unmap_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "zero-address munmap was accepted");

  Check(kajps5::hle::RegisterKernelMemoryExports(registry, direct_memory) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 30,
        "duplicate memory export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
