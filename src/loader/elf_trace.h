// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "loader/elf.h"

namespace kajps5::loader {

[[nodiscard]] std::string FormatElfTrace(const ElfMetadata& metadata);

}  // namespace kajps5::loader
