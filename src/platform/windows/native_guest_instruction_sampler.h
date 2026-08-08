// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#if defined(_WIN32)

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace kajps5::platform::windows {

struct NativeGuestInstructionSample {
  std::size_t index = 0;
  std::uint64_t instruction_pointer = 0;
};

// Captures a few control-context snapshots of the calling thread after a
// delay. The callback runs only after the target has been resumed.
class NativeGuestInstructionSampler final {
 public:
  using Callback = std::function<void(const NativeGuestInstructionSample&)>;

  [[nodiscard]] static std::unique_ptr<NativeGuestInstructionSampler>
  StartForCallingThread(std::chrono::milliseconds sample_interval,
                        Callback callback) noexcept;

  ~NativeGuestInstructionSampler();

  NativeGuestInstructionSampler(const NativeGuestInstructionSampler&) = delete;
  NativeGuestInstructionSampler& operator=(
      const NativeGuestInstructionSampler&) = delete;

  // Requests a prompt stop and joins the helper. It is also safe to call from
  // the destructor after an earlier Stop().
  void Stop() noexcept;

 private:
  NativeGuestInstructionSampler(void* target_thread, void* stop_event) noexcept;

  void* target_thread_ = nullptr;
  void* stop_event_ = nullptr;
  std::thread worker_;
};

}  // namespace kajps5::platform::windows

#endif
