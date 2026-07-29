// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include "loader/relocator.h"

namespace kajps5::loader {

inline constexpr std::size_t kMaximumRelocationTraceImports = 32;
inline constexpr std::size_t kMaximumRelocationTraceSymbolBytes = 128;

[[nodiscard]] std::string FormatRelocationTrace(
    const RelocationResult& result);

}  // namespace kajps5::loader
