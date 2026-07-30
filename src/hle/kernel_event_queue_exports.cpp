// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_event_queue_exports.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

void SetKernelResult(HleCallContext &context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
}

std::int32_t EventQueueStatusResult(kernel::KernelStatus status) noexcept {
  switch (status) {
  case kernel::KernelStatus::kOk:
    return 0;
  case kernel::KernelStatus::kInvalidArgument:
    return kKernelHleErrorInvalidArgument;
  case kernel::KernelStatus::kNotFound:
    return kKernelHleErrorBadFileDescriptor;
  case kernel::KernelStatus::kNoSuchEntry:
    return kKernelHleErrorNotFound;
  case kernel::KernelStatus::kBusy:
  case kernel::KernelStatus::kWouldBlock:
    return kKernelHleErrorBusy;
  case kernel::KernelStatus::kPermissionDenied:
    return kKernelHleErrorPermissionDenied;
  case kernel::KernelStatus::kNoResources:
    return kKernelHleErrorNoMemory;
  case kernel::KernelStatus::kTimedOut:
    return kKernelHleErrorTimedOut;
  }
  return kKernelHleErrorInvalidArgument;
}

HleContextStatus KernelCreateEqueue(
    HleCallContext &context, kernel::EventQueueService &event_queues) {
  const auto output_address = context.Argument(0).value_or(0);
  const auto name_address = context.Argument(1).value_or(0);
  if (output_address == 0 || name_address == 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  const auto name = context.ReadNullTerminatedString(
      name_address, kernel::kMaximumEventQueueNameLength + 1);
  if (!name) {
    SetKernelResult(
        context, name.status == HleContextStatus::kUnterminatedString
                     ? kKernelHleErrorInvalidArgument
                     : kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto created = event_queues.Create(name.value);
  if (!created) {
    SetKernelResult(context, EventQueueStatusResult(created.status));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output_address, created.handle) !=
      HleContextStatus::kOk) {
    (void)event_queues.Delete(created.handle);
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelDeleteEqueue(
    HleCallContext &context, kernel::EventQueueService &event_queues) {
  const auto status =
      event_queues.Delete(context.Argument(0).value_or(0));
  SetKernelResult(context, EventQueueStatusResult(status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelAddUserEvent(
    HleCallContext &context, kernel::EventQueueService &event_queues,
    bool edge) {
  const auto status = event_queues.AddUserEvent(
      context.Argument(0).value_or(0), context.Argument(1).value_or(0), edge);
  SetKernelResult(context, EventQueueStatusResult(status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelTriggerUserEvent(
    HleCallContext &context, kernel::EventQueueService &event_queues) {
  const auto status = event_queues.TriggerUserEvent(
      context.Argument(0).value_or(0), context.Argument(1).value_or(0),
      context.Argument(2).value_or(0));
  SetKernelResult(context, EventQueueStatusResult(status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelDeleteUserEvent(
    HleCallContext &context, kernel::EventQueueService &event_queues) {
  const auto status = event_queues.DeleteUserEvent(
      context.Argument(0).value_or(0), context.Argument(1).value_or(0));
  SetKernelResult(context, EventQueueStatusResult(status));
  return HleContextStatus::kOk;
}

} // namespace

std::vector<HleExportDefinition> detail::MakeKernelEventQueueExports(
    kernel::EventQueueService &event_queues) {
  auto *const queue_view = &event_queues;
  std::vector<HleExportDefinition> exports;
  exports.reserve(12);
  exports.push_back({kLibKernelName, kKernelCreateEqueueName,
                     [queue_view](HleCallContext &context) {
                       return KernelCreateEqueue(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelCreateEqueueNid,
                     [queue_view](HleCallContext &context) {
                       return KernelCreateEqueue(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteEqueueName,
                     [queue_view](HleCallContext &context) {
                       return KernelDeleteEqueue(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteEqueueNid,
                     [queue_view](HleCallContext &context) {
                       return KernelDeleteEqueue(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelAddUserEventName,
                     [queue_view](HleCallContext &context) {
                       return KernelAddUserEvent(context, *queue_view, false);
                     }});
  exports.push_back({kLibKernelName, kKernelAddUserEventNid,
                     [queue_view](HleCallContext &context) {
                       return KernelAddUserEvent(context, *queue_view, false);
                     }});
  exports.push_back({kLibKernelName, kKernelAddUserEventEdgeName,
                     [queue_view](HleCallContext &context) {
                       return KernelAddUserEvent(context, *queue_view, true);
                     }});
  exports.push_back({kLibKernelName, kKernelAddUserEventEdgeNid,
                     [queue_view](HleCallContext &context) {
                       return KernelAddUserEvent(context, *queue_view, true);
                     }});
  exports.push_back({kLibKernelName, kKernelTriggerUserEventName,
                     [queue_view](HleCallContext &context) {
                       return KernelTriggerUserEvent(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelTriggerUserEventNid,
                     [queue_view](HleCallContext &context) {
                       return KernelTriggerUserEvent(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteUserEventName,
                     [queue_view](HleCallContext &context) {
                       return KernelDeleteUserEvent(context, *queue_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteUserEventNid,
                     [queue_view](HleCallContext &context) {
                       return KernelDeleteUserEvent(context, *queue_view);
                     }});
  return exports;
}

ExportRegistryStatus RegisterKernelEventQueueExports(
    ExportRegistry &registry, kernel::EventQueueService &event_queues) {
  return registry.RegisterBatch(
      detail::MakeKernelEventQueueExports(event_queues));
}

} // namespace kajps5::hle
