// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <memory>

#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::int32_t kClockRealtime = 0;
inline constexpr std::int32_t kClockMonotonic = 4;
inline constexpr std::int32_t kClockUptime = 5;
inline constexpr std::int32_t kClockUptimePrecise = 7;
inline constexpr std::int32_t kClockUptimeFast = 8;
inline constexpr std::int32_t kClockRealtimePrecise = 9;
inline constexpr std::int32_t kClockRealtimeFast = 10;
inline constexpr std::int32_t kClockMonotonicPrecise = 11;
inline constexpr std::int32_t kClockMonotonicFast = 12;
inline constexpr std::int32_t kClockSecond = 13;
inline constexpr std::int32_t kClockProcessTime = 15;
inline constexpr std::int32_t kClockExternalNetwork = 16;
inline constexpr std::int32_t kClockExternalDebugNetwork = 17;
inline constexpr std::int32_t kClockExternalAdNetwork = 18;
inline constexpr std::int32_t kClockExternalRawNetwork = 19;

inline constexpr std::uint64_t kProcessTimeCounterFrequency = 1'000'000'000;

struct KernelTimespec {
  std::int64_t seconds = 0;
  std::int64_t nanoseconds = 0;
};

struct KernelTimeval {
  std::int64_t seconds = 0;
  std::int64_t microseconds = 0;
};

struct KernelTimespecResult {
  KernelStatus status = KernelStatus::kOk;
  KernelTimespec value;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == KernelStatus::kOk;
  }
};

class KernelClockSource {
public:
  virtual ~KernelClockSource() = default;

  [[nodiscard]] virtual std::int64_t RealtimeNanoseconds() const = 0;
  [[nodiscard]] virtual std::uint64_t MonotonicNanoseconds() const = 0;
};

[[nodiscard]] std::unique_ptr<KernelClockSource> CreateHostKernelClockSource();

class KernelClockService final {
public:
  KernelClockService();
  explicit KernelClockService(std::unique_ptr<KernelClockSource> source);

  KernelClockService(const KernelClockService &) = delete;
  KernelClockService &operator=(const KernelClockService &) = delete;

  [[nodiscard]] KernelTimespecResult ClockGettime(std::int32_t clock_id) const;
  [[nodiscard]] KernelTimeval Gettimeofday() const;
  [[nodiscard]] std::int64_t RealtimeNanoseconds() const;
  [[nodiscard]] std::uint64_t MonotonicNanoseconds() const;
  [[nodiscard]] std::uint64_t GetProcessTimeMicroseconds() const;
  [[nodiscard]] std::uint64_t GetProcessTimeCounter() const;
  [[nodiscard]] constexpr std::uint64_t
  GetProcessTimeCounterFrequency() const noexcept {
    return kProcessTimeCounterFrequency;
  }

private:
  [[nodiscard]] static KernelTimespec
  SignedNanosecondsToTimespec(std::int64_t nanoseconds) noexcept;
  [[nodiscard]] static KernelTimespec
  UnsignedNanosecondsToTimespec(std::uint64_t nanoseconds) noexcept;
  [[nodiscard]] std::uint64_t ProcessNanoseconds() const;

  std::unique_ptr<KernelClockSource> source_;
  std::uint64_t process_start_nanoseconds_ = 0;
};

} // namespace kajps5::kernel
