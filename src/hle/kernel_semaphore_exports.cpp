// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_semaphore_exports.h"

#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

void SetKernelResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
}

std::int32_t SignedArgument(const HleCallContext& context,
                            std::size_t index) noexcept {
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(context.Argument(index).value_or(0)));
}

std::int32_t SemaphoreStatusResult(kernel::KernelStatus status) noexcept {
  switch (status) {
    case kernel::KernelStatus::kOk: return 0;
    case kernel::KernelStatus::kInvalidArgument:
      return kKernelHleErrorInvalidArgument;
    case kernel::KernelStatus::kNotFound:
      return kKernelHleErrorNoSuchProcess;
    case kernel::KernelStatus::kNoSuchEntry:
      return kKernelHleErrorNotFound;
    case kernel::KernelStatus::kBusy: return kKernelHleErrorBusy;
    case kernel::KernelStatus::kNoResources: return kKernelHleErrorNoMemory;
    case kernel::KernelStatus::kPermissionDenied:
      return kKernelHleErrorPermissionDenied;
    case kernel::KernelStatus::kWouldBlock: return kKernelHleErrorBusy;
  }
  return kKernelHleErrorInvalidArgument;
}

HleContextStatus KernelCreateSema(HleCallContext& context,
                                  kernel::SemaphoreService& semaphores) {
  const auto output_address = context.Argument(0).value_or(0);
  const auto name_address = context.Argument(1).value_or(0);
  const auto attributes =
      static_cast<std::uint32_t>(context.Argument(2).value_or(0));
  const auto initial_count = SignedArgument(context, 3);
  const auto maximum_count = SignedArgument(context, 4);
  const auto option_address = context.Argument(5).value_or(0);
  if (output_address == 0 || name_address == 0 || option_address != 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto name = context.ReadNullTerminatedString(
      name_address, kernel::kMaximumSemaphoreNameLength + 1);
  if (!name) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  const auto created = semaphores.Create(name.value, attributes, initial_count,
                                         maximum_count);
  if (!created) {
    SetKernelResult(context, SemaphoreStatusResult(created.status));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output_address, created.handle) !=
      HleContextStatus::kOk) {
    (void)semaphores.Delete(created.handle);
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelDeleteSema(HleCallContext& context,
                                  kernel::SemaphoreService& semaphores) {
  const auto status = semaphores.Delete(context.Argument(0).value_or(0));
  SetKernelResult(context, SemaphoreStatusResult(status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelPollSema(HleCallContext& context,
                                kernel::SemaphoreService& semaphores) {
  const auto result = semaphores.Poll(context.Argument(0).value_or(0),
                                      SignedArgument(context, 1));
  SetKernelResult(context, SemaphoreStatusResult(result.status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelSignalSema(HleCallContext& context,
                                  kernel::SemaphoreService& semaphores) {
  const auto status = semaphores.Signal(context.Argument(0).value_or(0),
                                        SignedArgument(context, 1));
  SetKernelResult(context, SemaphoreStatusResult(status));
  return HleContextStatus::kOk;
}

}  // namespace

std::vector<HleExportDefinition> detail::MakeKernelSemaphoreExports(
    kernel::SemaphoreService& semaphores) {
  auto* const semaphore_view = &semaphores;
  std::vector<HleExportDefinition> exports;
  exports.reserve(8);
  exports.push_back({kLibKernelName, kKernelCreateSemaName,
                     [semaphore_view](HleCallContext& context) {
                       return KernelCreateSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelCreateSemaNid,
                     [semaphore_view](HleCallContext& context) {
                       return KernelCreateSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteSemaName,
                     [semaphore_view](HleCallContext& context) {
                       return KernelDeleteSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteSemaNid,
                     [semaphore_view](HleCallContext& context) {
                       return KernelDeleteSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelPollSemaName,
                     [semaphore_view](HleCallContext& context) {
                       return KernelPollSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelPollSemaNid,
                     [semaphore_view](HleCallContext& context) {
                       return KernelPollSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelSignalSemaName,
                     [semaphore_view](HleCallContext& context) {
                       return KernelSignalSema(context, *semaphore_view);
                     }});
  exports.push_back({kLibKernelName, kKernelSignalSemaNid,
                     [semaphore_view](HleCallContext& context) {
                       return KernelSignalSema(context, *semaphore_view);
                     }});
  return exports;
}

ExportRegistryStatus RegisterKernelSemaphoreExports(
    ExportRegistry& registry, kernel::SemaphoreService& semaphores) {
  return registry.RegisterBatch(
      detail::MakeKernelSemaphoreExports(semaphores));
}

}  // namespace kajps5::hle
