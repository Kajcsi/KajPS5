// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "kernel/handle_table.h"
#include "kernel/object.h"
#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::size_t kMaximumEventFlagNameLength = 31;

inline constexpr std::uint32_t kEventFlagThreadFifo = 0x01;
inline constexpr std::uint32_t kEventFlagThreadPriority = 0x02;
inline constexpr std::uint32_t kEventFlagSingle = 0x10;
inline constexpr std::uint32_t kEventFlagMulti = 0x20;

inline constexpr std::uint32_t kEventFlagWaitAll = 0x01;
inline constexpr std::uint32_t kEventFlagWaitAny = 0x02;
inline constexpr std::uint32_t kEventFlagClearAll = 0x10;
inline constexpr std::uint32_t kEventFlagClearPattern = 0x20;

enum class EventFlagWaitCondition {
  kAll,
  kAny,
};

enum class EventFlagClearMode {
  kNone,
  kAll,
  kPattern,
};

struct EventFlagStateResult {
  bool satisfied = false;
  std::uint64_t observed_pattern = 0;
};

class EventFlag final : public KernelObject {
public:
  EventFlag(std::string name, std::uint32_t attributes,
            std::uint64_t initial_pattern);

  [[nodiscard]] const std::string &name() const noexcept;
  [[nodiscard]] std::uint32_t attributes() const noexcept;
  [[nodiscard]] std::uint64_t bits() const;

  void Set(std::uint64_t pattern);
  void Clear(std::uint64_t mask);
  [[nodiscard]] EventFlagStateResult Poll(std::uint64_t pattern,
                                          EventFlagWaitCondition condition,
                                          EventFlagClearMode clear_mode);

private:
  std::string name_;
  std::uint32_t attributes_ = 0;
  mutable std::mutex mutex_;
  std::uint64_t bits_ = 0;
};

struct EventFlagCreateResult {
  KernelStatus status = KernelStatus::kOk;
  KernelHandle handle = kInvalidKernelHandle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct EventFlagPollResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t observed_pattern = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class EventFlagService final {
public:
  explicit EventFlagService(HandleTable &handles) noexcept;

  EventFlagService(const EventFlagService &) = delete;
  EventFlagService &operator=(const EventFlagService &) = delete;

  [[nodiscard]] EventFlagCreateResult Create(std::string name,
                                             std::uint32_t attributes,
                                             std::uint64_t initial_pattern);
  [[nodiscard]] KernelStatus Delete(KernelHandle handle);
  [[nodiscard]] KernelStatus Set(KernelHandle handle, std::uint64_t pattern);
  [[nodiscard]] KernelStatus Clear(KernelHandle handle, std::uint64_t mask);
  [[nodiscard]] EventFlagPollResult
  Poll(KernelHandle handle, std::uint64_t pattern, std::uint32_t wait_mode);

private:
  [[nodiscard]] static bool
  IsValidAttributes(std::uint32_t attributes) noexcept;
  [[nodiscard]] static bool
  DecodeWaitMode(std::uint32_t wait_mode, EventFlagWaitCondition &condition,
                 EventFlagClearMode &clear_mode) noexcept;
  [[nodiscard]] std::shared_ptr<EventFlag> Find(KernelHandle handle) const;

  HandleTable &handles_;
};

} // namespace kajps5::kernel
