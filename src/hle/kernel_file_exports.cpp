// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_file_exports.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

void SetKernelResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
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

}  // namespace

ExportRegistryStatus RegisterKernelFileExports(ExportRegistry& registry,
                                               kernel::FileService& files) {
  auto* const file_view = &files;
  std::vector<HleExportDefinition> exports;
  exports.reserve(4);
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
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
