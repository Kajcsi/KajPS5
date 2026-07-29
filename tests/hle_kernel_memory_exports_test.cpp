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
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(0x4000, 0xc000);
  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelMemoryExports(registry) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 10,
        "memory exports did not register atomically");

  HleCallContext page_size_context(memory);
  Check(Dispatch(registry, kajps5::hle::kPosixGetPageSizeNid,
                 page_size_context) == kajps5::hle::kKernelMemoryPageSize,
        "getpagesize returned the wrong guest page size");

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

  Check(kajps5::hle::RegisterKernelMemoryExports(registry) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 10,
        "duplicate memory export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
