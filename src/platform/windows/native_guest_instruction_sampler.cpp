// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/windows/native_guest_instruction_sampler.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <limits>
#include <utility>

namespace kajps5::platform::windows {
namespace {

constexpr std::size_t kMaximumSampleCount = 3;

[[nodiscard]] DWORD ToWaitMilliseconds(
    const std::chrono::milliseconds interval) noexcept {
  return static_cast<DWORD>(interval.count());
}

[[nodiscard]] bool CaptureInstructionPointer(
    HANDLE target_thread, std::uint64_t& instruction_pointer) noexcept {
  if (SuspendThread(target_thread) == static_cast<DWORD>(-1)) {
    return false;
  }

  CONTEXT context{};
  context.ContextFlags = CONTEXT_CONTROL;
  const bool captured = GetThreadContext(target_thread, &context) != 0;
  const bool resumed = ResumeThread(target_thread) != static_cast<DWORD>(-1);
  if (!captured || !resumed) {
    return false;
  }

#if defined(_M_X64) || defined(__x86_64__)
  instruction_pointer = context.Rip;
#elif defined(_M_ARM64) || defined(__aarch64__)
  instruction_pointer = context.Pc;
#else
#error "Native guest instruction sampling requires a supported Windows CPU context"
#endif
  return true;
}

void RunSampler(HANDLE target_thread, HANDLE stop_event,
                const DWORD sample_interval,
                NativeGuestInstructionSampler::Callback callback) noexcept {
  for (std::size_t index = 0; index < kMaximumSampleCount; ++index) {
    if (WaitForSingleObject(stop_event, sample_interval) != WAIT_TIMEOUT) {
      return;
    }

    NativeGuestInstructionSample sample{.index = index};
    if (!CaptureInstructionPointer(target_thread, sample.instruction_pointer)) {
      continue;
    }

    // The target is resumed before application code can allocate or log.
    try {
      callback(sample);
    } catch (...) {
      return;
    }
  }
}

}  // namespace

NativeGuestInstructionSampler::NativeGuestInstructionSampler(
    void* target_thread, void* stop_event) noexcept
    : target_thread_(target_thread), stop_event_(stop_event) {}

std::unique_ptr<NativeGuestInstructionSampler>
NativeGuestInstructionSampler::StartForCallingThread(
    const std::chrono::milliseconds sample_interval, Callback callback) noexcept {
  constexpr auto kMaximumWaitMilliseconds =
      static_cast<std::chrono::milliseconds::rep>(INFINITE - 1U);
  if (sample_interval.count() <= 0 ||
      sample_interval.count() > kMaximumWaitMilliseconds || !callback) {
    return nullptr;
  }

  HANDLE target_thread = nullptr;
  if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                      GetCurrentProcess(), &target_thread,
                      THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0) ==
      0) {
    return nullptr;
  }
  HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event == nullptr) {
    CloseHandle(target_thread);
    return nullptr;
  }

  std::unique_ptr<NativeGuestInstructionSampler> sampler;
  try {
    sampler = std::unique_ptr<NativeGuestInstructionSampler>(
        new NativeGuestInstructionSampler(target_thread, stop_event));
    sampler->worker_ = std::thread(RunSampler, target_thread, stop_event,
                                   ToWaitMilliseconds(sample_interval),
                                   std::move(callback));
    return sampler;
  } catch (...) {
    if (!sampler) {
      CloseHandle(stop_event);
      CloseHandle(target_thread);
    }
    return nullptr;
  }
}

NativeGuestInstructionSampler::~NativeGuestInstructionSampler() {
  Stop();
  if (stop_event_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(stop_event_));
  }
  if (target_thread_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(target_thread_));
  }
}

void NativeGuestInstructionSampler::Stop() noexcept {
  if (stop_event_ != nullptr) {
    (void)SetEvent(static_cast<HANDLE>(stop_event_));
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

}  // namespace kajps5::platform::windows

#endif
