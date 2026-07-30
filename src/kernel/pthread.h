// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "kernel/guest_scheduler.h"
#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::uint64_t kPthreadDefaultAffinityMask = 0x7f;
inline constexpr std::uint64_t kPthreadDefaultGuardSize = 0x1000;
inline constexpr std::uint64_t kPthreadDefaultStackSize = 0x100000;
inline constexpr std::uint64_t kPthreadMinimumStackSize = 0x4000;
inline constexpr int kPthreadDefaultPriority = 700;
inline constexpr std::size_t kMaximumPthreadKeys = 256;

struct PthreadAttribute {
  std::uint64_t affinity_mask = kPthreadDefaultAffinityMask;
  int detach_state = 0;
  std::uint64_t stack_address = 0;
  std::uint64_t stack_size = kPthreadDefaultStackSize;
  std::uint64_t guard_size = kPthreadDefaultGuardSize;
  int inherit_scheduler = 4;
  int scheduler_policy = 1;
  int priority = kPthreadDefaultPriority;
};

struct PthreadAttributeCreateResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t handle = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct PthreadKeyCreateResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint32_t key = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct PthreadSpecificResult {
  KernelStatus status = KernelStatus::kOk;
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct PthreadThreadCreateResult {
  KernelStatus status = KernelStatus::kOk;
  KernelHandle handle = kInvalidKernelHandle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct PthreadThreadSnapshot {
  KernelHandle handle = kInvalidKernelHandle;
  PthreadAttribute attributes;
};

class PthreadService final {
 public:
  explicit PthreadService(GuestScheduler& scheduler) noexcept;

  PthreadService(const PthreadService&) = delete;
  PthreadService& operator=(const PthreadService&) = delete;

  [[nodiscard]] PthreadAttributeCreateResult CreateAttribute();
  [[nodiscard]] KernelStatus DestroyAttribute(std::uint64_t handle);
  [[nodiscard]] KernelStatus SetAttributeStackSize(std::uint64_t handle,
                                                   std::uint64_t stack_size);
  [[nodiscard]] std::optional<PthreadAttribute> GetAttribute(
      std::uint64_t handle) const;

  [[nodiscard]] PthreadThreadCreateResult CreateThread(
      std::string name, std::uint64_t attribute_handle,
      std::uint64_t entry_address, std::uint64_t argument);
  [[nodiscard]] bool DiscardReadyThread(KernelHandle handle);
  [[nodiscard]] GuestThreadJoinResult JoinThread(KernelHandle handle);
  [[nodiscard]] bool ExitCurrent(std::uint64_t exit_value);
  [[nodiscard]] std::optional<PthreadThreadSnapshot> GetThread(
      KernelHandle handle) const;

  [[nodiscard]] PthreadKeyCreateResult CreateKey(
      std::uint64_t destructor_address);
  [[nodiscard]] KernelStatus DeleteKey(std::uint32_t key);
  [[nodiscard]] KernelStatus SetSpecific(std::uint32_t key,
                                         std::uint64_t value);
  [[nodiscard]] PthreadSpecificResult GetSpecific(std::uint32_t key) const;

  [[nodiscard]] std::size_t attribute_count() const;
  [[nodiscard]] std::size_t key_count() const;

 private:
  struct KeyState {
    std::uint64_t destructor_address = 0;
  };

  static constexpr std::uint64_t kSyntheticAttributeHandleBase =
      0x0000600400000000;

  GuestScheduler& scheduler_;
  mutable std::mutex mutex_;
  std::map<std::uint64_t, PthreadAttribute> attributes_;
  std::map<KernelHandle, PthreadThreadSnapshot> threads_;
  std::array<std::optional<KeyState>, kMaximumPthreadKeys> keys_{};
  std::map<KernelHandle, std::map<std::uint32_t, std::uint64_t>>
      specific_values_;
  std::uint64_t next_attribute_id_ = 1;
};

}  // namespace kajps5::kernel
