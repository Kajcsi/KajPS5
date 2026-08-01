// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/kernel/eventQueue.h
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "kernel/handle_table.h"
#include "kernel/object.h"
#include "kernel/status.h"

namespace kajps5::kernel {

class GuestScheduler;

inline constexpr std::size_t kMaximumEventQueueNameLength = 31;
inline constexpr std::int16_t kEventFilterGraphics = -14;
inline constexpr std::int16_t kEventFilterUser = -11;
inline constexpr std::uint16_t kEventAdd = 0x01;
inline constexpr std::uint16_t kEventOneShot = 0x10;
inline constexpr std::uint16_t kEventClear = 0x20;

struct KernelEvent {
  std::uint64_t ident = 0;
  std::int16_t filter = 0;
  std::uint16_t flags = 0;
  std::uint32_t fflags = 0;
  std::uint64_t data = 0;
  std::uint64_t user_data = 0;
};

static_assert(sizeof(KernelEvent) == 32);

struct RegisteredKernelEvent {
  KernelEvent event;
  std::uint64_t registration_generation = 0;
  bool persistent = false;
};

struct EventRegistrationResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t generation = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class EventQueue final : public KernelObject {
public:
  explicit EventQueue(std::string name);

  [[nodiscard]] const std::string &name() const noexcept;
  [[nodiscard]] EventRegistrationResult RegisterEvent(
      std::uint64_t ident, std::int16_t filter, std::uint16_t flags,
      std::uint64_t user_data);
  [[nodiscard]] KernelStatus DeleteEvent(std::uint64_t ident,
                                         std::int16_t filter);
  [[nodiscard]] KernelStatus AddUserEvent(std::uint64_t ident, bool edge);
  [[nodiscard]] KernelStatus DeleteUserEvent(std::uint64_t ident);
  [[nodiscard]] KernelStatus TriggerUserEvent(std::uint64_t ident,
                                              std::uint64_t user_data);
  [[nodiscard]] bool TriggerFirstByFilter(std::int16_t filter,
                                          std::uint64_t data);
  [[nodiscard]] std::vector<RegisteredKernelEvent> Poll(
      std::size_t maximum_count);
  [[nodiscard]] bool IsCurrent(const RegisteredKernelEvent &event) const;

private:
  struct Registration {
    std::uint64_t ident = 0;
    std::int16_t filter = 0;
    std::uint16_t flags = 0;
    std::uint64_t user_data = 0;
    std::uint64_t generation = 0;
  };

  std::string name_;
  mutable std::mutex mutex_;
  std::vector<Registration> registrations_;
  std::deque<RegisteredKernelEvent> pending_events_;
  std::uint64_t next_generation_ = 1;
  bool generation_exhausted_ = false;
};

struct EventQueueCreateResult {
  KernelStatus status = KernelStatus::kOk;
  KernelHandle handle = kInvalidKernelHandle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct EventQueuePollResult {
  KernelStatus status = KernelStatus::kOk;
  std::vector<KernelEvent> events;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class EventQueueService final {
public:
  EventQueueService(HandleTable &handles, GuestScheduler &scheduler) noexcept;

  EventQueueService(const EventQueueService &) = delete;
  EventQueueService &operator=(const EventQueueService &) = delete;

  [[nodiscard]] EventQueueCreateResult Create(std::string name);
  [[nodiscard]] KernelStatus Delete(KernelHandle handle);
  [[nodiscard]] KernelStatus AddUserEvent(KernelHandle handle,
                                          std::uint64_t ident, bool edge);
  [[nodiscard]] KernelStatus DeleteUserEvent(KernelHandle handle,
                                             std::uint64_t ident);
  [[nodiscard]] KernelStatus TriggerUserEvent(KernelHandle handle,
                                              std::uint64_t ident,
                                              std::uint64_t user_data);
  [[nodiscard]] KernelStatus AddGraphicsEvent(KernelHandle handle,
                                              std::uint64_t ident,
                                              std::uint64_t user_data);
  [[nodiscard]] KernelStatus DeleteGraphicsEvent(KernelHandle handle,
                                                 std::uint64_t ident);
  [[nodiscard]] std::size_t TriggerGraphicsEvents(std::uint64_t data);
  [[nodiscard]] EventQueuePollResult Poll(KernelHandle handle,
                                          std::size_t maximum_count);
  [[nodiscard]] EventQueuePollResult Wait(KernelHandle handle,
                                          std::size_t maximum_count);

private:
  enum class WaitCompletion {
    kWaiting,
    kReserved,
    kDeleted,
  };

  struct Waiter {
    KernelHandle queue_handle = kInvalidKernelHandle;
    std::size_t maximum_count = 0;
    WaitCompletion completion = WaitCompletion::kWaiting;
    std::vector<RegisteredKernelEvent> events;
  };

  [[nodiscard]] std::shared_ptr<EventQueue> Find(KernelHandle handle) const;
  [[nodiscard]] EventQueuePollResult PollLocked(
      KernelHandle handle, std::size_t maximum_count);
  [[nodiscard]] static std::vector<KernelEvent> CurrentEvents(
      const std::shared_ptr<EventQueue> &queue,
      std::vector<RegisteredKernelEvent> events);
  void ReserveWaitingThreadsLocked(KernelHandle handle,
                                   const std::shared_ptr<EventQueue> &queue);
  [[nodiscard]] static std::string MakeWaitKey(KernelHandle handle);

  HandleTable &handles_;
  GuestScheduler &scheduler_;
  std::mutex wait_mutex_;
  std::map<KernelHandle, Waiter> waiters_;
  std::map<KernelHandle, std::shared_ptr<EventQueue>> queues_;
};

} // namespace kajps5::kernel
