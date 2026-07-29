// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_exports.h"

#include <iterator>
#include <utility>
#include <vector>

#include "hle/kernel_clock_exports.h"
#include "hle/kernel_file_exports.h"
#include "kernel/runtime.h"

namespace kajps5::hle {

ExportRegistryStatus RegisterKernelExports(ExportRegistry& registry,
                                           kernel::KernelRuntime& runtime) {
  auto clock_exports = detail::MakeKernelClockExports(runtime.clock());
  auto file_exports = detail::MakeKernelFileExports(runtime.files());
  clock_exports.reserve(clock_exports.size() + file_exports.size());
  clock_exports.insert(clock_exports.end(),
                       std::make_move_iterator(file_exports.begin()),
                       std::make_move_iterator(file_exports.end()));
  return registry.RegisterBatch(std::move(clock_exports));
}

}  // namespace kajps5::hle
