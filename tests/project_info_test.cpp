// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>

#include "core/project_info.h"

int main() {
  if (kajps5::ProjectName() != "KajPS5") {
    return EXIT_FAILURE;
  }
  if (kajps5::ProjectVersion().empty()) {
    return EXIT_FAILURE;
  }
  if (kajps5::ProjectStatus().empty()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
