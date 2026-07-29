// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/kernel_clock_exports.h"
#include "kernel/clock.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_clock_exports_test: " << message << '\n';
    ++failures;
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

std::uint64_t DispatchReturn(kajps5::hle::ExportRegistry& registry,
                             std::string_view symbol,
                             kajps5::hle::HleCallContext& context) {
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(static_cast<bool>(result), "kernel clock export dispatch failed");
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::kernel::KernelClockService;
  using kajps5::memory::GuestMemory;

  auto source = std::make_unique<TestClockSource>();
  auto* const source_view = source.get();
  source_view->monotonic_nanoseconds = 10'000'000'000;
  KernelClockService clock(std::move(source));
  source_view->monotonic_nanoseconds = 12'500'123'456;

  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelClockExports(registry, clock) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 3,
        "kernel clock exports did not register atomically");

  GuestMemory memory(0x1000, 8);
  HleCallContext context(memory);
  Check(DispatchReturn(registry, kajps5::hle::kKernelGetProcessTimeName,
                       context) == 2'500'123,
        "process-time export returned the wrong microseconds");
  Check(DispatchReturn(
            registry, kajps5::hle::kKernelGetProcessTimeCounterName,
            context) == 2'500'123'456,
        "process-time counter export returned the wrong value");
  Check(DispatchReturn(
            registry,
            kajps5::hle::kKernelGetProcessTimeCounterFrequencyName,
            context) == kajps5::kernel::kProcessTimeCounterFrequency,
        "process-time frequency export returned the wrong value");

  Check(kajps5::hle::RegisterKernelClockExports(registry, clock) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 3,
        "duplicate clock export batch changed the registry");
  const std::vector<std::string> wrong_library = {"libkernel"};
  Check(registry
            .Dispatch(kajps5::hle::kKernelGetProcessTimeName,
                      wrong_library, context)
            .status == ExportRegistryStatus::kNotFound,
        "clock export lookup ignored library case");
  return failures == 0 ? 0 : 1;
}
