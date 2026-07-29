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

class GuestScheduler;

inline constexpr std::size_t kMaximumSemaphoreNameLength = 128;
inline constexpr std::uint32_t kSemaphoreThreadFifo = 0x01;
inline constexpr std::uint32_t kSemaphoreThreadPriority = 0x02;

class Semaphore final : public KernelObject {
public:
  Semaphore(std::string name, std::uint32_t attributes,
            std::int32_t initial_count, std::int32_t maximum_count);

  [[nodiscard]] const std::string &name() const noexcept;
  [[nodiscard]] std::uint32_t attributes() const noexcept;
  [[nodiscard]] std::int32_t maximum_count() const noexcept;
  [[nodiscard]] std::int32_t count() const;

  [[nodiscard]] KernelStatus Signal(std::int32_t count);
  [[nodiscard]] KernelStatus TryAcquire(std::int32_t count,
                                        std::int32_t &remaining_count);

private:
  std::string name_;
  std::uint32_t attributes_ = 0;
  std::int32_t maximum_count_ = 0;
  mutable std::mutex mutex_;
  std::int32_t count_ = 0;
};

struct SemaphoreCreateResult {
  KernelStatus status = KernelStatus::kOk;
  KernelHandle handle = kInvalidKernelHandle;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

struct SemaphoreCountResult {
  KernelStatus status = KernelStatus::kOk;
  std::int32_t remaining_count = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class SemaphoreService final {
public:
  SemaphoreService(HandleTable &handles, GuestScheduler &scheduler) noexcept;

  SemaphoreService(const SemaphoreService &) = delete;
  SemaphoreService &operator=(const SemaphoreService &) = delete;

  [[nodiscard]] SemaphoreCreateResult Create(std::string name,
                                             std::uint32_t attributes,
                                             std::int32_t initial_count,
                                             std::int32_t maximum_count);
  [[nodiscard]] KernelStatus Delete(KernelHandle handle);
  [[nodiscard]] KernelStatus Signal(KernelHandle handle, std::int32_t count);
  [[nodiscard]] SemaphoreCountResult Wait(KernelHandle handle,
                                          std::int32_t count);
  [[nodiscard]] SemaphoreCountResult Poll(KernelHandle handle,
                                          std::int32_t count);
  [[nodiscard]] SemaphoreCountResult GetCount(KernelHandle handle) const;

private:
  [[nodiscard]] static bool
  IsValidAttributes(std::uint32_t attributes) noexcept;
  [[nodiscard]] std::shared_ptr<Semaphore> Find(KernelHandle handle) const;
  [[nodiscard]] static std::string MakeWaitKey(KernelHandle handle);

  HandleTable &handles_;
  GuestScheduler &scheduler_;
  // Prevent a signal or delete from occurring between a wait check and block.
  std::mutex wait_mutex_;
};

} // namespace kajps5::kernel
