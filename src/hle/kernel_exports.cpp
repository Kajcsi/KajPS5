// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_exports.h"

#include <iterator>
#include <utility>
#include <vector>

#include "hle/kernel_clock_exports.h"
#include "hle/kernel_event_queue_exports.h"
#include "hle/kernel_event_flag_exports.h"
#include "hle/kernel_file_exports.h"
#include "hle/kernel_memory_exports.h"
#include "hle/kernel_semaphore_exports.h"
#include "kernel/runtime.h"

namespace kajps5::hle {

ExportRegistryStatus RegisterKernelExports(ExportRegistry& registry,
                                           kernel::KernelRuntime& runtime) {
  auto clock_exports = detail::MakeKernelClockExports(runtime.clock());
  auto event_queue_exports =
      detail::MakeKernelEventQueueExports(runtime.event_queues());
  auto event_flag_exports =
      detail::MakeKernelEventFlagExports(runtime.event_flags());
  auto file_exports = detail::MakeKernelFileExports(runtime.files());
  auto memory_exports = detail::MakeKernelMemoryExports(runtime.direct_memory());
  auto semaphore_exports =
      detail::MakeKernelSemaphoreExports(runtime.semaphores());
  clock_exports.reserve(clock_exports.size() + event_queue_exports.size() +
                        event_flag_exports.size() + file_exports.size() +
                        memory_exports.size() + semaphore_exports.size());
  clock_exports.insert(clock_exports.end(),
                       std::make_move_iterator(event_queue_exports.begin()),
                       std::make_move_iterator(event_queue_exports.end()));
  clock_exports.insert(clock_exports.end(),
                       std::make_move_iterator(event_flag_exports.begin()),
                       std::make_move_iterator(event_flag_exports.end()));
  clock_exports.insert(clock_exports.end(),
                       std::make_move_iterator(file_exports.begin()),
                       std::make_move_iterator(file_exports.end()));
  clock_exports.insert(clock_exports.end(),
                       std::make_move_iterator(memory_exports.begin()),
                       std::make_move_iterator(memory_exports.end()));
  clock_exports.insert(clock_exports.end(),
                       std::make_move_iterator(semaphore_exports.begin()),
                       std::make_move_iterator(semaphore_exports.end()));
  return registry.RegisterBatch(std::move(clock_exports));
}

}  // namespace kajps5::hle
