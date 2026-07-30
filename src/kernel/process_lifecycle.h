// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::size_t kMaximumProcessExitCallbacks = 4096;

struct GuestProcessEnvironment {
  bool initialized = false;
  std::int32_t argc = 0;
  std::uint64_t argv_address = 0;
  std::uint64_t envp_address = 0;
};

struct GuestCxaDestructor {
  std::uint64_t function = 0;
  std::uint64_t argument = 0;
  std::uint64_t module = 0;
};

struct GuestProcessExitRequest {
  bool requested = false;
  std::int32_t status = 0;
};

class ProcessLifecycleService final {
 public:
  void ResetEnvironment();
  [[nodiscard]] KernelStatus InitializeEnvironment(
      std::int32_t argc, std::uint64_t argv_address,
      std::uint64_t envp_address);
  [[nodiscard]] GuestProcessEnvironment environment() const;

  [[nodiscard]] KernelStatus RegisterAtexit(std::uint64_t function);
  [[nodiscard]] KernelStatus RegisterCxaDestructor(
      GuestCxaDestructor destructor);
  [[nodiscard]] std::vector<std::uint64_t> PendingAtexitCallbacks() const;
  [[nodiscard]] std::vector<GuestCxaDestructor> PendingCxaDestructors(
      std::optional<std::uint64_t> module = std::nullopt) const;
  [[nodiscard]] std::size_t callback_count() const;

  void RequestExit(std::int32_t status);
  [[nodiscard]] GuestProcessExitRequest exit_request() const;

 private:
  mutable std::mutex mutex_;
  GuestProcessEnvironment environment_;
  std::vector<std::uint64_t> atexit_callbacks_;
  std::vector<GuestCxaDestructor> cxa_destructors_;
  GuestProcessExitRequest exit_request_;
};

}  // namespace kajps5::kernel
