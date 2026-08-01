// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_event_queue_exports.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

constexpr std::size_t kMaximumWaitEventCount = 4096;
constexpr std::uint16_t kEventError = 0x4000;

template <typename Handler>
void Add(std::vector<HleExportDefinition> &exports, const char *name,
         const char *nid, Handler handler) {
  exports.push_back({kLibKernelName, name, handler});
  exports.push_back({kLibKernelName, nid, handler});
}

void Write16(std::span<std::byte> bytes, std::size_t offset,
             std::uint16_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::uint64_t ReadField(HleCallContext &context, std::size_t offset,
                        std::size_t size) noexcept {
  const auto event_address = context.Argument(0).value_or(0);
  if (event_address == 0 ||
      event_address > std::numeric_limits<std::uint64_t>::max() - offset) {
    return 0;
  }
  std::array<std::byte, sizeof(std::uint64_t)> bytes{};
  if (context.ReadMemory(event_address + offset,
                         std::span(bytes).first(size)) !=
      HleContextStatus::kOk) {
    return 0;
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < size; ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

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

std::int32_t EventQueueWaitStatusResult(
    kernel::KernelStatus status) noexcept {
  if (status == kernel::KernelStatus::kNotFound ||
      status == kernel::KernelStatus::kPermissionDenied) {
    return kKernelHleErrorBadFileDescriptor;
  }
  return EventQueueStatusResult(status);
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

HleContextStatus KernelWaitEqueue(
    HleCallContext &context, kernel::EventQueueService &event_queues) {
  const auto handle = context.Argument(0).value_or(0);
  const auto events_address = context.Argument(1).value_or(0);
  const auto capacity = std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(context.Argument(2).value_or(0)));
  const auto out_count_address = context.Argument(3).value_or(0);
  const auto timeout_address = context.Argument(4).value_or(0);
  if (events_address == 0 || out_count_address == 0 || capacity < 1 ||
      static_cast<std::uint32_t>(capacity) > kMaximumWaitEventCount) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  const auto event_bytes =
      static_cast<std::uint64_t>(capacity) * sizeof(kernel::KernelEvent);
  if (!context.CanWriteMemory(events_address, event_bytes) ||
      !context.CanWriteMemory(out_count_address, sizeof(std::uint32_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  std::optional<std::uint64_t> timeout_microseconds;
  if (timeout_address != 0) {
    std::uint32_t timeout = 0;
    if (context.ReadUInt32(timeout_address, timeout) !=
        HleContextStatus::kOk) {
      SetKernelResult(context, kKernelHleErrorFault);
      return HleContextStatus::kOk;
    }
    timeout_microseconds = timeout;
  }

  std::vector<std::byte> serialized;
  try {
    serialized.resize(static_cast<std::size_t>(event_bytes));
  } catch (...) {
    context.SetReturn(0);
    return HleContextStatus::kResourceLimit;
  }

  auto result = event_queues.Wait(
      handle, static_cast<std::size_t>(capacity), timeout_microseconds);
  if (result.status == kernel::KernelStatus::kWouldBlock) {
    return HleContextStatus::kBlocked;
  }
  if (!result) {
    if (context.WriteUInt32(out_count_address, 0) != HleContextStatus::kOk) {
      SetKernelResult(context, kKernelHleErrorFault);
      return HleContextStatus::kOk;
    }
    SetKernelResult(context, EventQueueWaitStatusResult(result.status));
    return HleContextStatus::kOk;
  }

  for (std::size_t index = 0; index < result.events.size(); ++index) {
    const auto offset = index * sizeof(kernel::KernelEvent);
    const auto &event = result.events[index];
    Write64(serialized, offset, event.ident);
    Write16(serialized, offset + 8,
            static_cast<std::uint16_t>(event.filter));
    Write16(serialized, offset + 10, event.flags);
    Write32(serialized, offset + 12, event.fflags);
    Write64(serialized, offset + 16, event.data);
    Write64(serialized, offset + 24, event.user_data);
  }
  const auto bytes_to_write =
      result.events.size() * sizeof(kernel::KernelEvent);
  if (context.WriteMemory(events_address,
                          std::span(serialized).first(bytes_to_write)) !=
          HleContextStatus::kOk ||
      context.WriteUInt32(out_count_address,
                          static_cast<std::uint32_t>(result.events.size())) !=
          HleContextStatus::kOk) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelGetEventUserData(HleCallContext &context) {
  context.SetReturn(ReadField(context, 24, sizeof(std::uint64_t)));
  return HleContextStatus::kOk;
}

HleContextStatus KernelGetEventId(HleCallContext &context) {
  context.SetReturn(ReadField(context, 0, sizeof(std::uint64_t)));
  return HleContextStatus::kOk;
}

HleContextStatus KernelGetEventFilter(HleCallContext &context) {
  const auto filter = std::bit_cast<std::int16_t>(
      static_cast<std::uint16_t>(ReadField(context, 8, sizeof(std::uint16_t))));
  context.SetReturn(static_cast<std::uint32_t>(filter));
  return HleContextStatus::kOk;
}

HleContextStatus KernelGetEventData(HleCallContext &context) {
  context.SetReturn(ReadField(context, 16, sizeof(std::uint64_t)));
  return HleContextStatus::kOk;
}

HleContextStatus KernelGetEventFflags(HleCallContext &context) {
  context.SetReturn(ReadField(context, 12, sizeof(std::uint32_t)));
  return HleContextStatus::kOk;
}

HleContextStatus KernelGetEventError(HleCallContext &context) {
  const auto flags = static_cast<std::uint16_t>(
      ReadField(context, 10, sizeof(std::uint16_t)));
  context.SetReturn((flags & kEventError) != 0
                        ? static_cast<std::uint32_t>(
                              ReadField(context, 16, sizeof(std::uint32_t)))
                        : 0);
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
  exports.reserve(26);
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
  Add(exports, kKernelWaitEqueueName, kKernelWaitEqueueNid,
      [queue_view](HleCallContext &context) {
        return KernelWaitEqueue(context, *queue_view);
      });
  Add(exports, kKernelGetEventUserDataName, kKernelGetEventUserDataNid,
      [](HleCallContext &context) { return KernelGetEventUserData(context); });
  Add(exports, kKernelGetEventIdName, kKernelGetEventIdNid,
      [](HleCallContext &context) { return KernelGetEventId(context); });
  Add(exports, kKernelGetEventFilterName, kKernelGetEventFilterNid,
      [](HleCallContext &context) { return KernelGetEventFilter(context); });
  Add(exports, kKernelGetEventDataName, kKernelGetEventDataNid,
      [](HleCallContext &context) { return KernelGetEventData(context); });
  Add(exports, kKernelGetEventFflagsName, kKernelGetEventFflagsNid,
      [](HleCallContext &context) { return KernelGetEventFflags(context); });
  Add(exports, kKernelGetEventErrorName, kKernelGetEventErrorNid,
      [](HleCallContext &context) { return KernelGetEventError(context); });
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
