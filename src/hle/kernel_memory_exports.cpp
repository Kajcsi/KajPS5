// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_memory_exports.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "kernel/direct_memory.h"

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

std::int32_t DirectMemoryReleaseResult(
    kernel::KernelStatus status) noexcept {
  if (status == kernel::KernelStatus::kOk) {
    return 0;
  }
  if (status == kernel::KernelStatus::kInvalidArgument) {
    return kKernelHleErrorInvalidArgument;
  }
  if (status == kernel::KernelStatus::kBusy) {
    return kKernelHleErrorBusy;
  }
  return kKernelHleErrorPermissionDenied;
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

HleContextStatus KernelMunmap(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  const auto address = context.Argument(0).value_or(0);
  const auto length = context.Argument(1).value_or(0);
  if (address == 0 || length == 0 ||
      length > std::numeric_limits<std::uint64_t>::max() - address) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.UnmapMemory(address, length)) {
    SetKernelResult(context, kKernelHleErrorPermissionDenied);
    return HleContextStatus::kOk;
  }
  direct_memory.UnregisterMappings(address, length);
  SetKernelResult(context, 0);
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

HleContextStatus KernelGetDirectMemorySize(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  context.SetReturn(direct_memory.size());
  return HleContextStatus::kOk;
}

HleContextStatus KernelAvailableDirectMemorySize(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  const auto search_start = context.Argument(0).value_or(0);
  const auto search_end = context.Argument(1).value_or(0);
  const auto alignment = context.Argument(2).value_or(0);
  const auto address_output = context.Argument(3).value_or(0);
  const auto size_output = context.Argument(4).value_or(0);
  if (address_output == 0 || size_output == 0 ||
      search_start > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int64_t>::max()) ||
      search_end > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(address_output, sizeof(std::uint64_t)) ||
      !context.CanWriteMemory(size_output, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto available =
      direct_memory.Available(search_start, search_end, alignment);
  if (!available) {
    SetKernelResult(context, kKernelHleErrorNoMemory);
    return HleContextStatus::kOk;
  }
  const auto write_failed =
      context.WriteUInt64(address_output, available.address) !=
          HleContextStatus::kOk ||
      context.WriteUInt64(size_output, available.size) !=
          HleContextStatus::kOk;
  SetKernelResult(context, write_failed ? kKernelHleErrorFault : 0);
  return HleContextStatus::kOk;
}

HleContextStatus AllocateDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory,
    std::uint64_t search_start, std::uint64_t search_end,
    std::uint64_t length, std::uint64_t alignment, std::int32_t memory_type,
    std::uint64_t address_output) {
  if (length == 0 || address_output == 0 || search_start >= search_end ||
      search_start > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int64_t>::max()) ||
      search_end > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(address_output, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto allocated = direct_memory.Allocate(
      search_start, search_end, length, alignment, memory_type);
  if (!allocated) {
    SetKernelResult(
        context, allocated.status == kernel::KernelStatus::kInvalidArgument
                     ? kKernelHleErrorInvalidArgument
                     : kKernelHleErrorTryAgain);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(address_output, allocated.address) !=
      HleContextStatus::kOk) {
    (void)direct_memory.Release(allocated.address, allocated.size);
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  SetKernelResult(context, 0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelAllocateDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return AllocateDirectMemory(
      context, direct_memory, context.Argument(0).value_or(0),
      context.Argument(1).value_or(0), context.Argument(2).value_or(0),
      context.Argument(3).value_or(0),
      static_cast<std::int32_t>(context.Argument(4).value_or(0)),
      context.Argument(5).value_or(0));
}

HleContextStatus KernelAllocateMainDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return AllocateDirectMemory(
      context, direct_memory, 0, direct_memory.size(),
      context.Argument(0).value_or(0), context.Argument(1).value_or(0),
      static_cast<std::int32_t>(context.Argument(2).value_or(0)),
      context.Argument(3).value_or(0));
}

HleContextStatus ReleaseDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory,
    bool checked) {
  const auto start = context.Argument(0).value_or(0);
  const auto length = context.Argument(1).value_or(0);
  if (start > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()) ||
      (checked && ((start & (kKernelMemoryPageSize - 1)) != 0 ||
                   (length & (kKernelMemoryPageSize - 1)) != 0))) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (checked && length == 0) {
    SetKernelResult(context, 0);
    return HleContextStatus::kOk;
  }
  if (length == 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  const auto released = direct_memory.Release(start, length);
  SetKernelResult(context, DirectMemoryReleaseResult(released));
  return HleContextStatus::kOk;
}

HleContextStatus KernelReleaseDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return ReleaseDirectMemory(context, direct_memory, false);
}

HleContextStatus KernelCheckedReleaseDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return ReleaseDirectMemory(context, direct_memory, true);
}

HleContextStatus MapDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory,
    std::size_t protection_index, std::size_t flags_index,
    std::size_t physical_index, std::size_t alignment_index,
    std::optional<std::size_t> name_index) {
  const auto address_pointer = context.Argument(0).value_or(0);
  const auto length = context.Argument(1).value_or(0);
  const auto protection_argument =
      context.Argument(protection_index).value_or(0);
  const auto flags_argument = context.Argument(flags_index).value_or(0);
  const auto protection_value =
      static_cast<std::uint32_t>(protection_argument);
  const auto flags = static_cast<std::uint32_t>(flags_argument);
  const auto physical_address = context.Argument(physical_index).value_or(
      std::numeric_limits<std::uint64_t>::max());
  const auto requested_alignment =
      context.Argument(alignment_index).value_or(0);
  const auto alignment = requested_alignment == 0
                             ? kKernelMemoryPageSize
                             : requested_alignment;
  if (address_pointer == 0 || length == 0 ||
      (length & (kKernelMemoryPageSize - 1)) != 0 ||
      (physical_address & (kKernelMemoryPageSize - 1)) != 0 ||
      alignment < kKernelMemoryPageSize ||
      (alignment & (alignment - 1)) != 0 ||
      protection_argument > std::numeric_limits<std::uint32_t>::max() ||
      flags_argument > std::numeric_limits<std::uint32_t>::max() ||
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
  if (name_index.has_value()) {
    const auto name_address = context.Argument(*name_index).value_or(0);
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
  if (!direct_memory.ContainsAllocatedRange(physical_address, length)) {
    SetKernelResult(context, kKernelHleErrorNoMemory);
    return HleContextStatus::kOk;
  }

  std::optional<std::uint64_t> mapped_address;
  if ((flags & kKernelMapFixed) != 0) {
    if (requested_address == 0 ||
        (requested_address & (alignment - 1)) != 0) {
      SetKernelResult(context, kKernelHleErrorInvalidArgument);
      return HleContextStatus::kOk;
    }
    mapped_address = requested_address;
  } else {
    const auto search_address =
        requested_address == 0 ? kDefaultPs5MapBase : requested_address;
    mapped_address =
        context.FindUnmappedMemory(search_address, length, alignment);
    if (!mapped_address.has_value() && requested_address == 0) {
      mapped_address = context.FindUnmappedMemory(0, length, alignment);
    }
  }
  if (!mapped_address.has_value() ||
      !context.MapMemory(*mapped_address, length, protection)) {
    SetKernelResult(context, kKernelHleErrorNoMemory);
    return HleContextStatus::kOk;
  }

  const auto registered = direct_memory.RegisterMapping(
      *mapped_address, physical_address, length);
  if (registered != kernel::KernelStatus::kOk) {
    (void)context.UnmapMemory(*mapped_address, length);
    SetKernelResult(context, registered == kernel::KernelStatus::kBusy
                                 ? kKernelHleErrorBusy
                                 : kKernelHleErrorNoMemory);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(address_pointer, *mapped_address) !=
      HleContextStatus::kOk) {
    direct_memory.UnregisterMappings(*mapped_address, length);
    (void)context.UnmapMemory(*mapped_address, length);
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  SetKernelResult(context, 0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelMapDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return MapDirectMemory(context, direct_memory, 2, 3, 4, 5, std::nullopt);
}

HleContextStatus KernelMapDirectMemory2(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return MapDirectMemory(context, direct_memory, 3, 4, 5, 6, std::nullopt);
}

HleContextStatus KernelMapNamedDirectMemory(
    HleCallContext& context, kernel::DirectMemoryService& direct_memory) {
  return MapDirectMemory(context, direct_memory, 2, 3, 4, 5, 6);
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

std::vector<HleExportDefinition> detail::MakeKernelMemoryExports(
    kernel::DirectMemoryService& direct_memory) {
  std::vector<HleExportDefinition> exports;
  exports.reserve(36);
  exports.push_back({kLibKernelName, kKernelMprotectName, KernelMprotect});
  exports.push_back({kLibKernelName, kKernelMprotectNid, KernelMprotect});
  exports.push_back({kLibKernelName, kPosixMprotectName, KernelMprotect});
  exports.push_back({kLibKernelName, kPosixMprotectNid, KernelMprotect});
  const auto kernel_munmap = [&direct_memory](HleCallContext& context) {
    return KernelMunmap(context, direct_memory);
  };
  exports.push_back({kLibKernelName, kKernelMunmapName, kernel_munmap});
  exports.push_back({kLibKernelName, kKernelMunmapNid, kernel_munmap});
  exports.push_back({kLibKernelName, kPosixMunmapName, kernel_munmap});
  exports.push_back({kLibKernelName, kPosixMunmapNid, kernel_munmap});
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
      {kLibKernelName, kKernelGetDirectMemorySizeName,
       [&direct_memory](HleCallContext& context) {
         return KernelGetDirectMemorySize(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelGetDirectMemorySizeNid,
       [&direct_memory](HleCallContext& context) {
         return KernelGetDirectMemorySize(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelAvailableDirectMemorySizeName,
       [&direct_memory](HleCallContext& context) {
         return KernelAvailableDirectMemorySize(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelAvailableDirectMemorySizeNid,
       [&direct_memory](HleCallContext& context) {
         return KernelAvailableDirectMemorySize(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelAllocateDirectMemoryName,
       [&direct_memory](HleCallContext& context) {
         return KernelAllocateDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelAllocateDirectMemoryNid,
       [&direct_memory](HleCallContext& context) {
         return KernelAllocateDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelAllocateMainDirectMemoryName,
       [&direct_memory](HleCallContext& context) {
         return KernelAllocateMainDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelAllocateMainDirectMemoryNid,
       [&direct_memory](HleCallContext& context) {
         return KernelAllocateMainDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelReleaseDirectMemoryName,
       [&direct_memory](HleCallContext& context) {
         return KernelReleaseDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelReleaseDirectMemoryNid,
       [&direct_memory](HleCallContext& context) {
         return KernelReleaseDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelCheckedReleaseDirectMemoryName,
       [&direct_memory](HleCallContext& context) {
         return KernelCheckedReleaseDirectMemory(context, direct_memory);
       }});
  exports.push_back(
      {kLibKernelName, kKernelCheckedReleaseDirectMemoryNid,
       [&direct_memory](HleCallContext& context) {
         return KernelCheckedReleaseDirectMemory(context, direct_memory);
       }});
  const auto map_direct = [&direct_memory](HleCallContext& context) {
    return KernelMapDirectMemory(context, direct_memory);
  };
  exports.push_back(
      {kLibKernelName, kKernelMapDirectMemoryName, map_direct});
  exports.push_back(
      {kLibKernelName, kKernelMapDirectMemoryNid, map_direct});
  const auto map_direct2 = [&direct_memory](HleCallContext& context) {
    return KernelMapDirectMemory2(context, direct_memory);
  };
  exports.push_back(
      {kLibKernelName, kKernelMapDirectMemory2Name, map_direct2});
  exports.push_back(
      {kLibKernelName, kKernelMapDirectMemory2Nid, map_direct2});
  const auto map_named_direct = [&direct_memory](HleCallContext& context) {
    return KernelMapNamedDirectMemory(context, direct_memory);
  };
  exports.push_back({kLibKernelName, kKernelMapNamedDirectMemoryName,
                     map_named_direct});
  exports.push_back({kLibKernelName, kKernelMapNamedDirectMemoryNid,
                     map_named_direct});
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

ExportRegistryStatus RegisterKernelMemoryExports(
    ExportRegistry& registry, kernel::DirectMemoryService& direct_memory) {
  return registry.RegisterBatch(
      detail::MakeKernelMemoryExports(direct_memory));
}

}  // namespace kajps5::hle
