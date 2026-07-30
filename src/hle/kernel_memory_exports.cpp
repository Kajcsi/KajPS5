// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_memory_exports.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace kajps5::hle {
namespace {

constexpr std::uint32_t kKnownProtection =
    kKernelProtectionCpuRead | kKernelProtectionCpuWrite |
    kKernelProtectionCpuExecute | kKernelProtectionGpuRead |
    kKernelProtectionGpuWrite;
constexpr std::uint32_t kKnownMapFlags =
    0x01 | 0x02 | kKernelMapFixed | kKernelMapNoOverwrite | 0x100 | 0x400 |
    0x800 | 0x1000 | 0x8000 | 0x20000 | 0x400000;
constexpr std::uint64_t kDefaultPs5MapBase = 0x200000000;
constexpr std::size_t kMaximumMapNameBytes = 32;

void SetKernelResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
}

bool NormalizeProtectRange(std::uint64_t address, std::uint64_t length,
                           std::uint64_t& aligned_address,
                           std::uint64_t& aligned_length) noexcept {
  if (address == 0 || length == 0 ||
      length > std::numeric_limits<std::uint64_t>::max() - address) {
    return false;
  }
  const auto end_address = address + length;
  if (end_address > std::numeric_limits<std::uint64_t>::max() -
                        (kKernelMemoryPageSize - 1)) {
    return false;
  }
  aligned_address = address & ~(kKernelMemoryPageSize - 1);
  const auto aligned_end =
      (end_address + kKernelMemoryPageSize - 1) &
      ~(kKernelMemoryPageSize - 1);
  aligned_length = aligned_end - aligned_address;
  return aligned_length != 0;
}

bool DecodeProtection(std::uint32_t value,
                      memory::GuestMemoryProtection& protection) noexcept {
  if ((value & ~kKnownProtection) != 0) {
    return false;
  }
  protection = memory::GuestMemoryProtection::kNone;
  if ((value & kKernelProtectionCpuRead) != 0) {
    protection = protection | memory::GuestMemoryProtection::kRead;
  }
  if ((value & kKernelProtectionCpuWrite) != 0) {
    protection = protection | memory::GuestMemoryProtection::kWrite;
  }
  if ((value & kKernelProtectionCpuExecute) != 0) {
    protection = protection | memory::GuestMemoryProtection::kExecute;
  }
  if ((value & kKernelProtectionGpuRead) != 0) {
    protection = protection | memory::GuestMemoryProtection::kGpuRead;
  }
  if ((value & kKernelProtectionGpuWrite) != 0) {
    protection = protection | memory::GuestMemoryProtection::kGpuWrite;
  }
  return true;
}

std::uint32_t EncodeProtection(
    memory::GuestMemoryProtection protection) noexcept {
  return static_cast<std::uint32_t>(protection);
}

HleContextStatus KernelMprotect(HleCallContext& context) {
  const auto address = context.Argument(0).value_or(0);
  const auto length = context.Argument(1).value_or(0);
  const auto protection_value =
      static_cast<std::uint32_t>(context.Argument(2).value_or(0));
  std::uint64_t aligned_address = 0;
  std::uint64_t aligned_length = 0;
  memory::GuestMemoryProtection protection;
  if (!NormalizeProtectRange(address, length, aligned_address,
                             aligned_length) ||
      !DecodeProtection(protection_value, protection)) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  SetKernelResult(context,
                  context.ProtectMemory(aligned_address, aligned_length,
                                        protection)
                      ? 0
                      : kKernelHleErrorPermissionDenied);
  return HleContextStatus::kOk;
}

HleContextStatus KernelMunmap(HleCallContext& context) {
  const auto address = context.Argument(0).value_or(0);
  const auto length = context.Argument(1).value_or(0);
  if (address == 0 || length == 0 ||
      length > std::numeric_limits<std::uint64_t>::max() - address) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  SetKernelResult(context, context.UnmapMemory(address, length)
                               ? 0
                               : kKernelHleErrorPermissionDenied);
  return HleContextStatus::kOk;
}

HleContextStatus KernelMapFlexibleMemory(HleCallContext& context,
                                         bool named) {
  const auto address_pointer = context.Argument(0).value_or(0);
  const auto length = context.Argument(1).value_or(0);
  const auto protection_value =
      static_cast<std::uint32_t>(context.Argument(2).value_or(0));
  const auto flags =
      static_cast<std::uint32_t>(context.Argument(3).value_or(0));
  if (address_pointer == 0 || length == 0 ||
      (length & (kKernelMemoryPageSize - 1)) != 0 ||
      (flags & ~kKnownMapFlags) != 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  memory::GuestMemoryProtection protection;
  if (!DecodeProtection(protection_value, protection)) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(address_pointer, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  std::uint64_t requested_address = 0;
  if (context.ReadUInt64(address_pointer, requested_address) !=
      HleContextStatus::kOk) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  if (named) {
    const auto name_address = context.Argument(4).value_or(0);
    if (name_address == 0) {
      SetKernelResult(context, kKernelHleErrorFault);
      return HleContextStatus::kOk;
    }
    const auto name = context.ReadNullTerminatedString(
        name_address, kMaximumMapNameBytes);
    if (name.status == HleContextStatus::kUnterminatedString) {
      SetKernelResult(context, kKernelHleErrorNameTooLong);
      return HleContextStatus::kOk;
    }
    if (!name) {
      SetKernelResult(context, kKernelHleErrorFault);
      return HleContextStatus::kOk;
    }
  }

  std::optional<std::uint64_t> mapped_address;
  if ((flags & kKernelMapFixed) != 0) {
    if (requested_address == 0 ||
        (requested_address & (kKernelMemoryPageSize - 1)) != 0) {
      SetKernelResult(context, kKernelHleErrorInvalidArgument);
      return HleContextStatus::kOk;
    }
    mapped_address = requested_address;
  } else {
    const auto search_address =
        requested_address == 0 ? kDefaultPs5MapBase : requested_address;
    mapped_address = context.FindUnmappedMemory(
        search_address, length, kKernelMemoryPageSize);
    if (!mapped_address.has_value() && requested_address == 0) {
      mapped_address = context.FindUnmappedMemory(
          0, length, kKernelMemoryPageSize);
    }
  }
  if (!mapped_address.has_value() ||
      !context.MapMemory(*mapped_address, length, protection)) {
    SetKernelResult(context, kKernelHleErrorNoMemory);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(address_pointer, *mapped_address) !=
      HleContextStatus::kOk) {
    (void)context.UnmapMemory(*mapped_address, length);
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  SetKernelResult(context, 0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelMapNamedFlexibleMemory(HleCallContext& context) {
  return KernelMapFlexibleMemory(context, true);
}

HleContextStatus KernelMapUnnamedFlexibleMemory(HleCallContext& context) {
  return KernelMapFlexibleMemory(context, false);
}

HleContextStatus PosixGetPageSize(HleCallContext& context) {
  context.SetReturn(kKernelMemoryPageSize);
  return HleContextStatus::kOk;
}

HleContextStatus KernelQueryMemoryProtection(HleCallContext& context) {
  const auto address = context.Argument(0).value_or(0);
  const auto start_address = context.Argument(1).value_or(0);
  const auto end_address = context.Argument(2).value_or(0);
  const auto protection_address = context.Argument(3).value_or(0);
  if (address == 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  const auto region = context.QueryMemoryRegion(address);
  if (!region) {
    SetKernelResult(context, kKernelHleErrorPermissionDenied);
    return HleContextStatus::kOk;
  }
  if ((start_address != 0 &&
       !context.CanWriteMemory(start_address, sizeof(std::uint64_t))) ||
      (end_address != 0 &&
       !context.CanWriteMemory(end_address, sizeof(std::uint64_t))) ||
      (protection_address != 0 &&
       !context.CanWriteMemory(protection_address, sizeof(std::uint32_t)))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  const auto write_failed =
      (start_address != 0 &&
       context.WriteUInt64(start_address, region->address) !=
           HleContextStatus::kOk) ||
      (end_address != 0 &&
       context.WriteUInt64(end_address, region->address + region->size) !=
           HleContextStatus::kOk) ||
      (protection_address != 0 &&
       context.WriteUInt32(protection_address,
                           EncodeProtection(region->protection)) !=
           HleContextStatus::kOk);
  SetKernelResult(context, write_failed ? kKernelHleErrorFault : 0);
  return HleContextStatus::kOk;
}

}  // namespace

std::vector<HleExportDefinition> detail::MakeKernelMemoryExports() {
  std::vector<HleExportDefinition> exports;
  exports.reserve(18);
  exports.push_back({kLibKernelName, kKernelMprotectName, KernelMprotect});
  exports.push_back({kLibKernelName, kKernelMprotectNid, KernelMprotect});
  exports.push_back({kLibKernelName, kPosixMprotectName, KernelMprotect});
  exports.push_back({kLibKernelName, kPosixMprotectNid, KernelMprotect});
  exports.push_back({kLibKernelName, kKernelMunmapName, KernelMunmap});
  exports.push_back({kLibKernelName, kKernelMunmapNid, KernelMunmap});
  exports.push_back({kLibKernelName, kPosixMunmapName, KernelMunmap});
  exports.push_back({kLibKernelName, kPosixMunmapNid, KernelMunmap});
  exports.push_back({kLibKernelName, kKernelMapNamedFlexibleMemoryName,
                     KernelMapNamedFlexibleMemory});
  exports.push_back({kLibKernelName, kKernelMapNamedFlexibleMemoryNid,
                     KernelMapNamedFlexibleMemory});
  exports.push_back({kLibKernelName, kKernelMapFlexibleMemoryName,
                     KernelMapUnnamedFlexibleMemory});
  exports.push_back({kLibKernelName, kKernelMapFlexibleMemoryNid,
                     KernelMapUnnamedFlexibleMemory});
  exports.push_back({kLibKernelName, kKernelMapFlexibleMemoryInternalName,
                     KernelMapUnnamedFlexibleMemory});
  exports.push_back({kLibKernelName, kKernelMapFlexibleMemoryInternalNid,
                     KernelMapUnnamedFlexibleMemory});
  exports.push_back(
      {kLibKernelName, kPosixGetPageSizeName, PosixGetPageSize});
  exports.push_back(
      {kLibKernelName, kPosixGetPageSizeNid, PosixGetPageSize});
  exports.push_back({kLibKernelName, kKernelQueryMemoryProtectionName,
                     KernelQueryMemoryProtection});
  exports.push_back({kLibKernelName, kKernelQueryMemoryProtectionNid,
                     KernelQueryMemoryProtection});
  return exports;
}

ExportRegistryStatus RegisterKernelMemoryExports(ExportRegistry& registry) {
  return registry.RegisterBatch(detail::MakeKernelMemoryExports());
}

}  // namespace kajps5::hle
