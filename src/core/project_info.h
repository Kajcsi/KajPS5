// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace kajps5 {

[[nodiscard]] std::string_view ProjectName() noexcept;
[[nodiscard]] std::string_view ProjectVersion() noexcept;
[[nodiscard]] std::string_view ProjectStatus() noexcept;

}  // namespace kajps5
