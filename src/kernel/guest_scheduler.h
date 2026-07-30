// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kernel/clock.h"
#include "kernel/handle_table.h"
#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::size_t kMaximumGuestThreadNameLength = 31;

enum class GuestThreadState {
  kReady,
  kRunning,
  kBlocked,
  kExited,
};

struct GuestThreadCreateResult {
  KernelStatus status = KernelStatus::kOk;
  KernelHandle handle = kInvalidKernelHandle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct GuestThreadJoinResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t exit_value = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct GuestThreadSnapshot {
  KernelHandle handle = kInvalidKernelHandle;
  std::string name;
  int priority = 0;
  GuestThreadState state = GuestThreadState::kReady;
  std::string wait_key;
  std::uint64_t exit_value = 0;
  std::uint64_t entry_address = 0;
  std::uint64_t argument = 0;
};

class GuestScheduler final {
public:
  GuestScheduler(HandleTable &handles, KernelClockService &clock) noexcept;
  ~GuestScheduler();

  GuestScheduler(const GuestScheduler &) = delete;
  GuestScheduler &operator=(const GuestScheduler &) = delete;

  [[nodiscard]] GuestThreadCreateResult CreateThread(std::string name,
                                                     int priority,
                                                     std::uint64_t entry_address = 0,
                                                     std::uint64_t argument = 0);
  [[nodiscard]] bool DiscardReadyThread(KernelHandle handle);
  [[nodiscard]] std::optional<KernelHandle> SelectNext();
  [[nodiscard]] bool YieldCurrent();
  [[nodiscard]] bool BlockCurrent(std::string wait_key);
  [[nodiscard]] bool BlockCurrentUntil(std::string wait_key,
                                       std::uint64_t deadline_nanoseconds);
  [[nodiscard]] std::size_t WakeBlockedThreads(
      std::string_view wait_key,
      std::size_t maximum_count = std::numeric_limits<std::size_t>::max());
  [[nodiscard]] GuestThreadJoinResult JoinThread(KernelHandle handle);
  [[nodiscard]] bool ExitCurrent(std::uint64_t exit_value);
  [[nodiscard]] bool CurrentThreadTimedOut(
      std::string_view wait_key) const;

  [[nodiscard]] std::optional<KernelHandle> current_thread() const;
  [[nodiscard]] std::optional<GuestThreadSnapshot>
  Snapshot(KernelHandle handle) const;
  [[nodiscard]] std::vector<GuestThreadSnapshot> SnapshotAll() const;

private:
  struct GuestThread;

  [[nodiscard]] static GuestThreadSnapshot
  MakeSnapshot(KernelHandle handle, const GuestThread &thread);
  [[nodiscard]] bool BlockCurrentLocked(
      std::string wait_key,
      std::optional<std::uint64_t> deadline_nanoseconds);
  void WakeExpiredThreadsLocked(std::uint64_t now_nanoseconds);

  HandleTable &handles_;
  KernelClockService &clock_;
  mutable std::mutex mutex_;
  std::map<KernelHandle, std::shared_ptr<GuestThread>> threads_;
  std::deque<KernelHandle> ready_threads_;
  std::optional<KernelHandle> current_thread_;
};

} // namespace kajps5::kernel
