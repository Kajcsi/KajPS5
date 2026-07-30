// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.cpp
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"
#include "kernel/event_queue.h"

namespace kajps5::hle {

inline constexpr auto kKernelCreateEqueueName = "sceKernelCreateEqueue";
inline constexpr auto kKernelCreateEqueueNid = "D0OdFMjp46I";
inline constexpr auto kKernelDeleteEqueueName = "sceKernelDeleteEqueue";
inline constexpr auto kKernelDeleteEqueueNid = "jpFjmgAC5AE";
inline constexpr auto kKernelAddUserEventName = "sceKernelAddUserEvent";
inline constexpr auto kKernelAddUserEventNid = "4R6-OvI2cEA";
inline constexpr auto kKernelAddUserEventEdgeName =
    "sceKernelAddUserEventEdge";
inline constexpr auto kKernelAddUserEventEdgeNid = "WDszmSbWuDk";
inline constexpr auto kKernelTriggerUserEventName =
    "sceKernelTriggerUserEvent";
inline constexpr auto kKernelTriggerUserEventNid = "F6e0kwo4cnk";
inline constexpr auto kKernelDeleteUserEventName =
    "sceKernelDeleteUserEvent";
inline constexpr auto kKernelDeleteUserEventNid = "LJDwdSNTnDg";

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelEventQueueExports(
    kernel::EventQueueService &event_queues);
}

// The event-queue service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelEventQueueExports(
    ExportRegistry &registry, kernel::EventQueueService &event_queues);

} // namespace kajps5::hle
