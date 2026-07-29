// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <string_view>

#include "core/project_info.h"

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << kajps5::ProjectName() << ' ' << kajps5::ProjectVersion()
              << '\n';
    return 0;
  }

  std::cout << kajps5::ProjectName() << ' ' << kajps5::ProjectVersion()
            << '\n'
            << kajps5::ProjectStatus() << '\n';
  return 0;
}
