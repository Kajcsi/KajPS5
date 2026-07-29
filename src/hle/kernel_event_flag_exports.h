// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "hle/export_registry.h"
#include "hle/kernel_exports.h"
#include "kernel/event_flag.h"

namespace kajps5::hle {

inline constexpr auto kKernelCreateEventFlagName = "sceKernelCreateEventFlag";
inline constexpr auto kKernelCreateEventFlagNid = "BpFoboUJoZU";
inline constexpr auto kKernelDeleteEventFlagName = "sceKernelDeleteEventFlag";
inline constexpr auto kKernelDeleteEventFlagNid = "8mql9OcQnd4";
inline constexpr auto kKernelPollEventFlagName = "sceKernelPollEventFlag";
inline constexpr auto kKernelPollEventFlagNid = "9lvj5DjHZiA";
inline constexpr auto kKernelSetEventFlagName = "sceKernelSetEventFlag";
inline constexpr auto kKernelSetEventFlagNid = "IOnSvHzqu6A";
inline constexpr auto kKernelClearEventFlagName = "sceKernelClearEventFlag";
inline constexpr auto kKernelClearEventFlagNid = "7uhBFWRAS60";

namespace detail {
[[nodiscard]] std::vector<HleExportDefinition> MakeKernelEventFlagExports(
    kernel::EventFlagService& event_flags);
}

// The event-flag service must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelEventFlagExports(
    ExportRegistry& registry, kernel::EventFlagService& event_flags);

}  // namespace kajps5::hle
