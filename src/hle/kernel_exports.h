// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace kajps5::hle {

inline constexpr auto kLibKernelName = "libKernel";

inline constexpr std::int32_t kKernelHleErrorNotFound = -2147352574;
inline constexpr std::int32_t kKernelHleErrorBadFileDescriptor = -2147352567;
inline constexpr std::int32_t kKernelHleErrorPermissionDenied = -2147352563;
inline constexpr std::int32_t kKernelHleErrorFault = -2147352562;
inline constexpr std::int32_t kKernelHleErrorInvalidArgument = -2147352554;
inline constexpr std::int32_t kKernelHleErrorTooManyOpenFiles = -2147352552;

}  // namespace kajps5::hle
