// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

#include "hle/export_registry.h"

namespace kajps5::kernel {
class KernelRuntime;
}

namespace kajps5::hle {

inline constexpr auto kLibKernelName = "libKernel";

inline constexpr std::int32_t kKernelHleErrorNotFound = -2147352574;
inline constexpr std::int32_t kKernelHleErrorNoSuchProcess = -2147352573;
inline constexpr std::int32_t kKernelHleErrorBadFileDescriptor = -2147352567;
inline constexpr std::int32_t kKernelHleErrorNoMemory = -2147352564;
inline constexpr std::int32_t kKernelHleErrorPermissionDenied = -2147352563;
inline constexpr std::int32_t kKernelHleErrorFault = -2147352562;
inline constexpr std::int32_t kKernelHleErrorBusy = -2147352560;
inline constexpr std::int32_t kKernelHleErrorInvalidArgument = -2147352554;
inline constexpr std::int32_t kKernelHleErrorTooManyOpenFiles = -2147352552;
inline constexpr std::int32_t kKernelHleErrorTryAgain = -2147352541;
inline constexpr std::int32_t kKernelHleErrorNameTooLong = -2147352513;

// The kernel runtime must outlive all dispatches through the registry.
[[nodiscard]] ExportRegistryStatus RegisterKernelExports(
    ExportRegistry& registry, kernel::KernelRuntime& runtime);

}  // namespace kajps5::hle
