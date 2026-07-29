// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/project_info.h"

namespace kajps5 {

std::string_view ProjectName() noexcept { return "KajPS5"; }

std::string_view ProjectVersion() noexcept { return KAJPS5_VERSION; }

std::string_view ProjectStatus() noexcept {
  return "Foundation build. Emulation is not available.";
}

}  // namespace kajps5
