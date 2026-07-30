// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_event_flag_exports.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

void SetKernelResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
}

std::int32_t EventFlagStatusResult(kernel::KernelStatus status) noexcept {
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

HleContextStatus KernelCreateEventFlag(
    HleCallContext& context, kernel::EventFlagService& event_flags) {
  const auto output_address = context.Argument(0).value_or(0);
  const auto name_address = context.Argument(1).value_or(0);
  const auto attributes =
      static_cast<std::uint32_t>(context.Argument(2).value_or(0));
  const auto initial_pattern = context.Argument(3).value_or(0);
  const auto option_address = context.Argument(4).value_or(0);
  if (output_address == 0 || name_address == 0 || option_address != 0) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto name = context.ReadNullTerminatedString(
      name_address, kernel::kMaximumEventFlagNameLength + 1);
  if (!name) {
    SetKernelResult(
        context, name.status == HleContextStatus::kUnterminatedString
                     ? kKernelHleErrorInvalidArgument
                     : kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  const auto created =
      event_flags.Create(name.value, attributes, initial_pattern);
  if (!created) {
    SetKernelResult(context, EventFlagStatusResult(created.status));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output_address, created.handle) !=
      HleContextStatus::kOk) {
    (void)event_flags.Delete(created.handle);
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus KernelDeleteEventFlag(
    HleCallContext& context, kernel::EventFlagService& event_flags) {
  const auto status = event_flags.Delete(context.Argument(0).value_or(0));
  SetKernelResult(context, EventFlagStatusResult(status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelPollEventFlag(
    HleCallContext& context, kernel::EventFlagService& event_flags) {
  const auto result_address = context.Argument(3).value_or(0);
  if (result_address != 0 &&
      !context.CanWriteMemory(result_address, sizeof(std::uint64_t))) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto result = event_flags.Poll(
      context.Argument(0).value_or(0), context.Argument(1).value_or(0),
      static_cast<std::uint32_t>(context.Argument(2).value_or(0)));
  if ((result.status == kernel::KernelStatus::kOk ||
       result.status == kernel::KernelStatus::kBusy) &&
      result_address != 0 &&
      context.WriteUInt64(result_address, result.observed_pattern) !=
          HleContextStatus::kOk) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  SetKernelResult(context, EventFlagStatusResult(result.status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelSetEventFlag(
    HleCallContext& context, kernel::EventFlagService& event_flags) {
  const auto status = event_flags.Set(context.Argument(0).value_or(0),
                                      context.Argument(1).value_or(0));
  SetKernelResult(context, EventFlagStatusResult(status));
  return HleContextStatus::kOk;
}

HleContextStatus KernelClearEventFlag(
    HleCallContext& context, kernel::EventFlagService& event_flags) {
  const auto status = event_flags.Clear(context.Argument(0).value_or(0),
                                        context.Argument(1).value_or(0));
  SetKernelResult(context, EventFlagStatusResult(status));
  return HleContextStatus::kOk;
}

}  // namespace

std::vector<HleExportDefinition> detail::MakeKernelEventFlagExports(
    kernel::EventFlagService& event_flags) {
  auto* const event_flag_view = &event_flags;
  std::vector<HleExportDefinition> exports;
  exports.reserve(10);
  exports.push_back({kLibKernelName, kKernelCreateEventFlagName,
                     [event_flag_view](HleCallContext& context) {
                       return KernelCreateEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelCreateEventFlagNid,
                     [event_flag_view](HleCallContext& context) {
                       return KernelCreateEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteEventFlagName,
                     [event_flag_view](HleCallContext& context) {
                       return KernelDeleteEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelDeleteEventFlagNid,
                     [event_flag_view](HleCallContext& context) {
                       return KernelDeleteEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelPollEventFlagName,
                     [event_flag_view](HleCallContext& context) {
                       return KernelPollEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelPollEventFlagNid,
                     [event_flag_view](HleCallContext& context) {
                       return KernelPollEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelSetEventFlagName,
                     [event_flag_view](HleCallContext& context) {
                       return KernelSetEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelSetEventFlagNid,
                     [event_flag_view](HleCallContext& context) {
                       return KernelSetEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelClearEventFlagName,
                     [event_flag_view](HleCallContext& context) {
                       return KernelClearEventFlag(context, *event_flag_view);
                     }});
  exports.push_back({kLibKernelName, kKernelClearEventFlagNid,
                     [event_flag_view](HleCallContext& context) {
                       return KernelClearEventFlag(context, *event_flag_view);
                     }});
  return exports;
}

ExportRegistryStatus RegisterKernelEventFlagExports(
    ExportRegistry& registry, kernel::EventFlagService& event_flags) {
  return registry.RegisterBatch(detail::MakeKernelEventFlagExports(event_flags));
}

}  // namespace kajps5::hle
