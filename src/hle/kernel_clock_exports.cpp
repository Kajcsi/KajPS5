// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_clock_exports.h"

#include <utility>
#include <vector>

namespace kajps5::hle {

ExportRegistryStatus RegisterKernelClockExports(
    ExportRegistry& registry, kernel::KernelClockService& clock) {
  auto* const clock_view = &clock;
  std::vector<HleExportDefinition> exports;
  exports.reserve(3);
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
