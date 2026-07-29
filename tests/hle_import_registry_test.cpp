// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <string>
#include <vector>

#include "hle/import_registry.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_import_registry_test: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using kajps5::hle::ImportRegistry;
  using kajps5::hle::ImportRegistryStatus;

  ImportRegistry registry;
  Check(registry.Register("libkernel", "open", 0x1000) ==
            ImportRegistryStatus::kOk,
        "kernel import registration failed");
  Check(registry.Register("libc", "memset", 0x2000) ==
            ImportRegistryStatus::kOk,
        "libc import registration failed");
  Check(registry.Register("compat", "open", 0x3000) ==
            ImportRegistryStatus::kOk,
        "compat import registration failed");
  Check(registry.size() == 3, "registry size is incorrect");

  Check(registry.Register("libkernel", "open", 0x4000) ==
            ImportRegistryStatus::kAlreadyExists,
        "duplicate registration was accepted");
  Check(registry.Register("", "open", 1) ==
            ImportRegistryStatus::kInvalidArgument,
        "empty library was accepted");
  Check(registry.Register("libc", "", 1) ==
            ImportRegistryStatus::kInvalidArgument,
        "empty symbol was accepted");
  Check(registry.Register("libc", "malloc", 0) ==
            ImportRegistryStatus::kInvalidArgument,
        "zero target was accepted");
  Check(registry.Register("libc", std::string(256, 'x'), 1) ==
            ImportRegistryStatus::kInvalidArgument,
        "long symbol was accepted");

  const std::vector<std::string> kernel_first = {"libkernel", "compat"};
  const auto ordered = registry.Resolve("open", kernel_first);
  Check(ordered && ordered.target_address == 0x1000 &&
            ordered.library == "libkernel",
        "library order was not preserved");

  const std::vector<std::string> compat_first = {"compat", "libkernel"};
  const auto fallback = registry.Resolve("open", compat_first);
  Check(fallback && fallback.target_address == 0x3000 &&
            fallback.library == "compat",
        "alternate library order was not preserved");
  const std::vector<std::string> libc_only = {"libc"};
  Check(registry.Resolve("open", libc_only).status ==
            ImportRegistryStatus::kNotFound,
        "lookup escaped the needed-library scope");

  Check(registry.Resolve("open").status == ImportRegistryStatus::kAmbiguous,
        "unscoped duplicate symbol was not ambiguous");
  const auto unique = registry.Resolve("memset");
  Check(unique && unique.target_address == 0x2000 && unique.library == "libc",
        "unique unscoped symbol did not resolve");
  Check(registry.Resolve("missing").status == ImportRegistryStatus::kNotFound,
        "missing symbol returned the wrong status");
  Check(registry.Resolve("").status == ImportRegistryStatus::kInvalidArgument,
        "empty lookup was accepted");
  Check(kajps5::hle::ImportRegistryStatusName(
            ImportRegistryStatus::kAmbiguous) == "ambiguous",
        "registry status name is unstable");

  return failures == 0 ? 0 : 1;
}
