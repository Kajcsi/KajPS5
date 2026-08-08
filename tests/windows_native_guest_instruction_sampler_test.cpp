// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include "platform/windows/native_guest_instruction_sampler.h"

int main() {
#if !defined(_WIN32)
  return 77;
#else
  std::atomic<std::size_t> callback_count = 0;
  std::atomic<bool> saw_nonzero_instruction_pointer = false;
  auto sampler =
      kajps5::platform::windows::NativeGuestInstructionSampler::StartForCallingThread(
          std::chrono::milliseconds(2),
          [&](const kajps5::platform::windows::NativeGuestInstructionSample& sample) {
            if (sample.instruction_pointer != 0) {
              saw_nonzero_instruction_pointer.store(true,
                                                     std::memory_order_relaxed);
            }
            callback_count.fetch_add(1, std::memory_order_relaxed);
          });
  if (!sampler) {
    std::cerr << "Could not start native guest instruction sampler\n";
    return 1;
  }

  for (std::size_t wait = 0; wait < 100 &&
                            callback_count.load(std::memory_order_relaxed) == 0;
       ++wait) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  sampler->Stop();
  const auto delivered = callback_count.load(std::memory_order_relaxed);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (delivered == 0 ||
      !saw_nonzero_instruction_pointer.load(std::memory_order_relaxed) ||
      callback_count.load(std::memory_order_relaxed) != delivered) {
    std::cerr << "Native guest instruction sampler did not stop cleanly\n";
    return 1;
  }

  std::atomic<std::size_t> callback_after_lifetime = 0;
  {
    auto short_lived_sampler =
        kajps5::platform::windows::NativeGuestInstructionSampler::StartForCallingThread(
            std::chrono::seconds(5),
            [&](const kajps5::platform::windows::NativeGuestInstructionSample&) {
              callback_after_lifetime.fetch_add(1, std::memory_order_relaxed);
            });
    if (!short_lived_sampler) {
      std::cerr << "Could not create short-lived native guest sampler\n";
      return 1;
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (callback_after_lifetime.load(std::memory_order_relaxed) != 0) {
    std::cerr << "Native guest instruction sampler outlived its owner\n";
    return 1;
  }
  return 0;
#endif
}
