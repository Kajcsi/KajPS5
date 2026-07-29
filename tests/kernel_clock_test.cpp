// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "kernel/clock.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

class TestClockSource final : public kajps5::kernel::KernelClockSource {
public:
  [[nodiscard]] std::int64_t RealtimeNanoseconds() const override {
    return realtime_nanoseconds;
  }

  [[nodiscard]] std::uint64_t MonotonicNanoseconds() const override {
    return monotonic_nanoseconds;
  }

  std::int64_t realtime_nanoseconds = 0;
  std::uint64_t monotonic_nanoseconds = 0;
};

} // namespace

int main() {
  using namespace kajps5::kernel;

  auto source = std::make_unique<TestClockSource>();
  auto *source_view = source.get();
  source_view->realtime_nanoseconds = 1'725'000'123'456'789'000;
  source_view->monotonic_nanoseconds = 9'000'000'000;
  KernelRuntime runtime(std::move(source));
  auto &clock = runtime.clock();

  constexpr std::array realtime_clock_ids = {
      kClockRealtime, kClockRealtimePrecise, kClockRealtimeFast};
  KernelTimespecResult time;
  for (const auto clock_id : realtime_clock_ids) {
    time = clock.ClockGettime(clock_id);
    Check(time && time.value.seconds == 1'725'000'123 &&
              time.value.nanoseconds == 456'789'000,
          "realtime clock conversion failed");
  }

  time = clock.ClockGettime(kClockSecond);
  Check(time && time.value.seconds == 1'725'000'123 &&
            time.value.nanoseconds == 0,
        "second clock was not truncated");

  constexpr std::array monotonic_clock_ids = {
      kClockMonotonic,         kClockUptime,
      kClockUptimePrecise,     kClockUptimeFast,
      kClockMonotonicPrecise,  kClockMonotonicFast,
      kClockExternalNetwork,   kClockExternalDebugNetwork,
      kClockExternalAdNetwork, kClockExternalRawNetwork,
  };
  for (const auto clock_id : monotonic_clock_ids) {
    time = clock.ClockGettime(clock_id);
    Check(time && time.value.seconds == 9 && time.value.nanoseconds == 0,
          "monotonic clock conversion failed");
  }

  const auto timeval = clock.Gettimeofday();
  Check(timeval.seconds == 1'725'000'123 && timeval.microseconds == 456'789,
        "time-of-day conversion failed");

  source_view->monotonic_nanoseconds = 11'500'123'456;
  time = clock.ClockGettime(kClockProcessTime);
  Check(time && time.value.seconds == 2 &&
            time.value.nanoseconds == 500'123'456,
        "process clock did not use the runtime start point");
  Check(clock.GetProcessTimeMicroseconds() == 2'500'123,
        "process microsecond conversion failed");
  Check(clock.GetProcessTimeCounter() == 2'500'123'456,
        "process counter is inconsistent");
  Check(clock.GetProcessTimeCounterFrequency() == 1'000'000'000,
        "process counter frequency is inconsistent");

  source_view->monotonic_nanoseconds = 8'000'000'000;
  Check(clock.GetProcessTimeCounter() == 0,
        "backward monotonic source produced a negative process time");

  source_view->realtime_nanoseconds = -1;
  time = clock.ClockGettime(kClockRealtime);
  Check(time && time.value.seconds == -1 &&
            time.value.nanoseconds == 999'999'999,
        "negative realtime value was not normalized");

  Check(clock.ClockGettime(99).status == KernelStatus::kInvalidArgument,
        "unknown clock ID was accepted");

  std::cout << "kernel clock tests passed\n";
  return 0;
}
