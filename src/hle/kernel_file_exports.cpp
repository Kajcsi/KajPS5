// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_file_exports.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

constexpr std::size_t kFileIoChunkBytes = 16 * 1024;
constexpr std::uint16_t kRegularFileMode = 0x81ff;
constexpr std::size_t kStatInodeOffset = 4;
constexpr std::size_t kStatModeOffset = 8;
constexpr std::size_t kStatLinkCountOffset = 10;
constexpr std::size_t kStatSizeOffset = 72;
constexpr std::size_t kStatBlocksOffset = 80;
constexpr std::size_t kStatBlockSizeOffset = 88;

void SetKernelResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
}

void Write16(std::array<std::byte, kKernelStatSize>& bytes,
             std::size_t offset, std::uint16_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::array<std::byte, kKernelStatSize>& bytes,
             std::size_t offset, std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::array<std::byte, kKernelStatSize>& bytes,
             std::size_t offset, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::array<std::byte, kKernelStatSize> RegularFileStat(
    std::uint64_t size, std::uint32_t inode) noexcept {
  std::array<std::byte, kKernelStatSize> bytes{};
  Write32(bytes, kStatInodeOffset, inode);
  Write16(bytes, kStatModeOffset, kRegularFileMode);
  Write16(bytes, kStatLinkCountOffset, 1);
  Write64(bytes, kStatSizeOffset, size);
  const auto blocks = size / 512 + (size % 512 == 0 ? 0 : 1);
  Write64(bytes, kStatBlocksOffset, blocks);
  Write32(bytes, kStatBlockSizeOffset, 512);
  return bytes;
}

std::int32_t FileStatusResult(kernel::KernelStatus status) noexcept {
  switch (status) {
    case kernel::KernelStatus::kOk: return 0;
    case kernel::KernelStatus::kInvalidArgument:
      return kKernelHleErrorInvalidArgument;
    case kernel::KernelStatus::kNotFound: return kKernelHleErrorNotFound;
    case kernel::KernelStatus::kPermissionDenied:
      return kKernelHleErrorPermissionDenied;
    case kernel::KernelStatus::kNoResources:
      return kKernelHleErrorTooManyOpenFiles;
    case kernel::KernelStatus::kBusy:
    case kernel::KernelStatus::kWouldBlock:
      return kKernelHleErrorInvalidArgument;
  }
  return kKernelHleErrorInvalidArgument;
}

HleContextStatus KernelOpen(HleCallContext& context,
                            kernel::FileService& files) {
  const auto path_address = context.Argument(0).value_or(0);
  const auto flags =
      static_cast<std::uint32_t>(context.Argument(1).value_or(0));
  const auto path = context.ReadNullTerminatedString(
      path_address, kernel::kMaximumGuestPathLength + 1);
  if (!path) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto opened = files.Open(path.value, flags);
  if (!opened) {
    SetKernelResult(context, FileStatusResult(opened.status));
    return HleContextStatus::kOk;
  }
  context.SetReturn(opened.handle);
  return HleContextStatus::kOk;
}

HleContextStatus KernelClose(HleCallContext& context,
                             kernel::FileService& files) {
  const auto handle = context.Argument(0).value_or(0);
  const auto status = files.Close(handle);
  SetKernelResult(context, status == kernel::KernelStatus::kOk
                               ? 0
                               : kKernelHleErrorBadFileDescriptor);
  return HleContextStatus::kOk;
}

HleContextStatus KernelRead(HleCallContext& context,
                            kernel::FileService& files) {
  const auto handle = context.Argument(0).value_or(0);
  const auto destination = context.Argument(1).value_or(0);
  const auto requested = context.Argument(2).value_or(0);
  if (requested == 0) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(destination, requested)) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  std::array<std::byte, kFileIoChunkBytes> buffer{};
  std::uint64_t total = 0;
  while (total < requested) {
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(buffer.size(), requested - total));
    const auto result = files.Read(handle, std::span(buffer).first(count));
    if (!result) {
      SetKernelResult(context,
                      result.status == kernel::KernelStatus::kNotFound
                          ? kKernelHleErrorBadFileDescriptor
                          : FileStatusResult(result.status));
      return HleContextStatus::kOk;
    }
    if (result.value == 0) {
      break;
    }
    if (context.WriteMemory(
            destination + total,
            std::span(buffer).first(static_cast<std::size_t>(result.value))) !=
        HleContextStatus::kOk) {
      SetKernelResult(context, kKernelHleErrorFault);
      return HleContextStatus::kOk;
    }
    total += result.value;
    if (result.value < count) {
      break;
    }
  }
  context.SetReturn(total);
  return HleContextStatus::kOk;
}

HleContextStatus KernelPread(HleCallContext& context,
                             kernel::FileService& files) {
  const auto handle = context.Argument(0).value_or(0);
  const auto destination = context.Argument(1).value_or(0);
  const auto requested = context.Argument(2).value_or(0);
  const auto offset =
      std::bit_cast<std::int64_t>(context.Argument(3).value_or(0));
  if (offset < 0 ||
      requested > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max() - offset)) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (requested != 0 && !context.CanWriteMemory(destination, requested)) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  std::array<std::byte, kFileIoChunkBytes> buffer{};
  std::uint64_t total = 0;
  do {
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(buffer.size(), requested - total));
    const auto current_offset = offset + static_cast<std::int64_t>(total);
    const auto result =
        files.Pread(handle, current_offset, std::span(buffer).first(count));
    if (!result) {
      SetKernelResult(context,
                      result.status == kernel::KernelStatus::kNotFound
                          ? kKernelHleErrorBadFileDescriptor
                          : FileStatusResult(result.status));
      return HleContextStatus::kOk;
    }
    if (result.value == 0) {
      break;
    }
    if (context.WriteMemory(
            destination + total,
            std::span(buffer).first(static_cast<std::size_t>(result.value))) !=
        HleContextStatus::kOk) {
      SetKernelResult(context, kKernelHleErrorFault);
      return HleContextStatus::kOk;
    }
    total += result.value;
    if (result.value < count) {
      break;
    }
  } while (total < requested);
  context.SetReturn(total);
  return HleContextStatus::kOk;
}

HleContextStatus KernelLseek(HleCallContext& context,
                             kernel::FileService& files) {
  const auto handle = context.Argument(0).value_or(0);
  const auto offset =
      std::bit_cast<std::int64_t>(context.Argument(1).value_or(0));
  const auto whence = context.Argument(2).value_or(0);

  kernel::FileSeekWhence origin{};
  switch (whence) {
    case 0: origin = kernel::FileSeekWhence::kSet; break;
    case 1: origin = kernel::FileSeekWhence::kCurrent; break;
    case 2: origin = kernel::FileSeekWhence::kEnd; break;
    default:
      SetKernelResult(context, kKernelHleErrorInvalidArgument);
      return HleContextStatus::kOk;
  }

  const auto result = files.Seek(handle, offset, origin);
  if (!result) {
    SetKernelResult(context,
                    result.status == kernel::KernelStatus::kNotFound
                        ? kKernelHleErrorBadFileDescriptor
                        : FileStatusResult(result.status));
    return HleContextStatus::kOk;
  }
  context.SetReturn(result.value);
  return HleContextStatus::kOk;
}

HleContextStatus KernelStat(HleCallContext& context,
                            kernel::FileService& files) {
  const auto path_address = context.Argument(0).value_or(0);
  const auto destination = context.Argument(1).value_or(0);
  if (path_address == 0 || destination == 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(destination, kKernelStatSize)) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto path = context.ReadNullTerminatedString(
      path_address, kernel::kMaximumGuestPathLength + 1);
  if (!path) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  const auto stat = files.Stat(path.value);
  if (!stat) {
    SetKernelResult(context, FileStatusResult(stat.status));
    return HleContextStatus::kOk;
  }

  const auto bytes = RegularFileStat(stat.size, stat.inode);
  if (context.WriteMemory(destination, bytes) != HleContextStatus::kOk) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelFstat(HleCallContext& context,
                             kernel::FileService& files) {
  const auto handle = context.Argument(0).value_or(0);
  const auto destination = context.Argument(1).value_or(0);
  if (!context.CanWriteMemory(destination, kKernelStatSize)) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto stat = files.Fstat(handle);
  if (!stat) {
    SetKernelResult(context,
                    stat.status == kernel::KernelStatus::kNotFound
                        ? kKernelHleErrorBadFileDescriptor
                        : FileStatusResult(stat.status));
    return HleContextStatus::kOk;
  }
  const auto bytes = RegularFileStat(stat.size, stat.inode);
  if (context.WriteMemory(destination, bytes) != HleContextStatus::kOk) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

}  // namespace

ExportRegistryStatus RegisterKernelFileExports(ExportRegistry& registry,
                                               kernel::FileService& files) {
  auto* const file_view = &files;
  std::vector<HleExportDefinition> exports;
  exports.reserve(14);
  exports.push_back({kLibKernelName, kKernelOpenName,
                     [file_view](HleCallContext& context) {
                       return KernelOpen(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelOpenNid,
                     [file_view](HleCallContext& context) {
                       return KernelOpen(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelCloseName,
                     [file_view](HleCallContext& context) {
                       return KernelClose(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelCloseNid,
                     [file_view](HleCallContext& context) {
                       return KernelClose(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelReadName,
                     [file_view](HleCallContext& context) {
                       return KernelRead(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelReadNid,
                     [file_view](HleCallContext& context) {
                       return KernelRead(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelPreadName,
                     [file_view](HleCallContext& context) {
                       return KernelPread(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelPreadNid,
                     [file_view](HleCallContext& context) {
                       return KernelPread(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelLseekName,
                     [file_view](HleCallContext& context) {
                       return KernelLseek(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelLseekNid,
                     [file_view](HleCallContext& context) {
                       return KernelLseek(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelStatName,
                     [file_view](HleCallContext& context) {
                       return KernelStat(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelStatNid,
                     [file_view](HleCallContext& context) {
                       return KernelStat(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelFstatName,
                     [file_view](HleCallContext& context) {
                       return KernelFstat(context, *file_view);
                     }});
  exports.push_back({kLibKernelName, kKernelFstatNid,
                     [file_view](HleCallContext& context) {
                       return KernelFstat(context, *file_view);
                     }});
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
