// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/call_context.h"

namespace kajps5::hle::detail {

inline constexpr auto kLibcSnprintfName = "snprintf";
inline constexpr auto kLibcSnprintfNid = "eLdDw6l0-bU";
inline constexpr auto kLibcVsnprintfName = "vsnprintf";
inline constexpr auto kLibcVsnprintfNid = "Q2V+iqvjgC0";
inline constexpr auto kLibcSprintfName = "sprintf";
inline constexpr auto kLibcSprintfNid = "tcVi5SivF7Q";
inline constexpr auto kLibcVsprintfName = "vsprintf";
inline constexpr auto kLibcVsprintfNid = "jbz9I9vkqkk";

[[nodiscard]] HleContextStatus LibcSnprintf(HleCallContext& context);
[[nodiscard]] HleContextStatus LibcVsnprintf(HleCallContext& context);
[[nodiscard]] HleContextStatus LibcSprintf(HleCallContext& context);
[[nodiscard]] HleContextStatus LibcVsprintf(HleCallContext& context);

}  // namespace kajps5::hle::detail
