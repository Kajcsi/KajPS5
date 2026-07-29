// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_clock_exports.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kajps5::hle {
namespace {

void SetKernelResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(static_cast<std::uint64_t>(
      static_cast<std::int64_t>(result)));
}

void Write64(std::array<std::byte, 16>& bytes, std::size_t offset,
             std::int64_t value) noexcept {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((bits >> (index * 8U)) & 0xffU);
  }
}

HleContextStatus ClockGettime(HleCallContext& context,
                              kernel::KernelClockService& clock) {
  const auto clock_id = static_cast<std::int32_t>(
      context.Argument(0).value_or(0));
  const auto destination = context.Argument(1).value_or(0);
  if (destination == 0) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }

  const auto time = clock.ClockGettime(clock_id);
  if (!time) {
    SetKernelResult(context, kKernelHleErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  std::array<std::byte, 16> bytes{};
  Write64(bytes, 0, time.value.seconds);
  Write64(bytes, 8, time.value.nanoseconds);
  if (context.WriteMemory(destination, bytes) != HleContextStatus::kOk) {
    SetKernelResult(context, kKernelHleErrorFault);
    return HleContextStatus::kOk;
  }
  SetKernelResult(context, 0);
  return HleContextStatus::kOk;
}

}  // namespace

ExportRegistryStatus RegisterKernelClockExports(
    ExportRegistry& registry, kernel::KernelClockService& clock) {
  auto* const clock_view = &clock;
  std::vector<HleExportDefinition> exports;
  exports.reserve(4);
  exports.push_back(
      {kLibKernelName, kKernelClockGettimeName,
       [clock_view](HleCallContext& context) {
         return ClockGettime(context, *clock_view);
       }});
  exports.push_back(
      {kLibKernelName, kKernelGetProcessTimeName,
       [clock_view](HleCallContext& context) {
         context.SetReturn(clock_view->GetProcessTimeMicroseconds());
         return HleContextStatus::kOk;
       }});
  exports.push_back(
      {kLibKernelName, kKernelGetProcessTimeCounterName,
       [clock_view](HleCallContext& context) {
         context.SetReturn(clock_view->GetProcessTimeCounter());
         return HleContextStatus::kOk;
       }});
  exports.push_back(
      {kLibKernelName, kKernelGetProcessTimeCounterFrequencyName,
       [clock_view](HleCallContext& context) {
         context.SetReturn(clock_view->GetProcessTimeCounterFrequency());
         return HleContextStatus::kOk;
       }});
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
