// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/clock.h"

#include <chrono>
#include <utility>

namespace kajps5::kernel {
namespace {

class HostKernelClockSource final : public KernelClockSource {
public:
  std::int64_t RealtimeNanoseconds() const override {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  std::uint64_t MonotonicNanoseconds() const override {
    const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
  }
};

} // namespace

std::unique_ptr<KernelClockSource> CreateHostKernelClockSource() {
  return std::make_unique<HostKernelClockSource>();
}

KernelClockService::KernelClockService()
    : KernelClockService(CreateHostKernelClockSource()) {}

KernelClockService::KernelClockService(
    std::unique_ptr<KernelClockSource> source)
    : source_(source ? std::move(source) : CreateHostKernelClockSource()),
      process_start_nanoseconds_(source_->MonotonicNanoseconds()) {}

KernelTimespecResult
KernelClockService::ClockGettime(std::int32_t clock_id) const {
  switch (clock_id) {
  case kClockRealtime:
  case kClockRealtimePrecise:
  case kClockRealtimeFast:
    return {KernelStatus::kOk,
            SignedNanosecondsToTimespec(source_->RealtimeNanoseconds())};
  case kClockSecond: {
    auto value = SignedNanosecondsToTimespec(source_->RealtimeNanoseconds());
    value.nanoseconds = 0;
    return {KernelStatus::kOk, value};
  }
  case kClockMonotonic:
  case kClockUptime:
  case kClockUptimePrecise:
  case kClockUptimeFast:
  case kClockMonotonicPrecise:
  case kClockMonotonicFast:
  case kClockExternalNetwork:
  case kClockExternalDebugNetwork:
  case kClockExternalAdNetwork:
  case kClockExternalRawNetwork:
    return {KernelStatus::kOk,
            UnsignedNanosecondsToTimespec(source_->MonotonicNanoseconds())};
  case kClockProcessTime:
    return {KernelStatus::kOk,
            UnsignedNanosecondsToTimespec(ProcessNanoseconds())};
  default:
    return {KernelStatus::kInvalidArgument, {}};
  }
}

KernelTimeval KernelClockService::Gettimeofday() const {
  const auto time = SignedNanosecondsToTimespec(source_->RealtimeNanoseconds());
  return {time.seconds, time.nanoseconds / 1'000};
}

std::uint64_t KernelClockService::GetProcessTimeMicroseconds() const {
  return ProcessNanoseconds() / 1'000;
}

std::uint64_t KernelClockService::GetProcessTimeCounter() const {
  return ProcessNanoseconds();
}

KernelTimespec KernelClockService::SignedNanosecondsToTimespec(
    std::int64_t nanoseconds) noexcept {
  constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
  auto seconds = nanoseconds / kNanosecondsPerSecond;
  auto remainder = nanoseconds % kNanosecondsPerSecond;
  if (remainder < 0) {
    --seconds;
    remainder += kNanosecondsPerSecond;
  }
  return {seconds, remainder};
}

KernelTimespec KernelClockService::UnsignedNanosecondsToTimespec(
    std::uint64_t nanoseconds) noexcept {
  constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;
  return {static_cast<std::int64_t>(nanoseconds / kNanosecondsPerSecond),
          static_cast<std::int64_t>(nanoseconds % kNanosecondsPerSecond)};
}

std::uint64_t KernelClockService::ProcessNanoseconds() const {
  const auto now = source_->MonotonicNanoseconds();
  return now >= process_start_nanoseconds_ ? now - process_start_nanoseconds_
                                           : 0;
}

} // namespace kajps5::kernel
