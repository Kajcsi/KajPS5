// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <array>
#include <bit>
#include <cstddef>
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

std::int64_t ReadSigned64(const std::array<std::byte, 16>& bytes,
                          std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return std::bit_cast<std::int64_t>(value);
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
  source_view->realtime_nanoseconds = 1'725'000'123'456'789'000;
  source_view->monotonic_nanoseconds = 10'000'000'000;
  KernelClockService clock(std::move(source));
  source_view->monotonic_nanoseconds = 12'500'123'456;

  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelClockExports(registry, clock) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 10,
        "kernel clock exports did not register atomically");

  GuestMemory memory(0x1000, 32);
  HleCallContext context(memory);
  Check(DispatchReturn(registry, kajps5::hle::kKernelGetProcessTimeNid,
                       context) == 2'500'123,
        "process-time NID returned the wrong microseconds");
  Check(DispatchReturn(
            registry, kajps5::hle::kKernelGetProcessTimeCounterNid,
            context) == 2'500'123'456,
        "process-time counter NID returned the wrong value");
  Check(DispatchReturn(
            registry,
            kajps5::hle::kKernelGetProcessTimeCounterFrequencyNid,
            context) == kajps5::kernel::kProcessTimeCounterFrequency,
        "process-time frequency NID returned the wrong value");

  Check(context.SetRegister(kajps5::hle::HleRegister::kRdi,
                            kajps5::kernel::kClockRealtime) &&
            context.SetRegister(kajps5::hle::HleRegister::kRsi, 0x1000),
        "clock-gettime argument setup failed");
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto clock_result = registry.Dispatch(
      kajps5::hle::kKernelClockGettimeNid, libraries, context);
  std::array<std::byte, 16> timespec{};
  Check(clock_result && memory.Read(0x1000, timespec) &&
            ReadSigned64(timespec, 0) == 1'725'000'123 &&
            ReadSigned64(timespec, 8) == 456'789'000,
        "realtime clock-gettime export wrote the wrong value");

  HleCallContext timeval_context(memory);
  Check(timeval_context.SetRegister(kajps5::hle::HleRegister::kRdi, 0x1010),
        "gettimeofday argument setup failed");
  const auto timeval_result = registry.Dispatch(
      kajps5::hle::kKernelGettimeofdayNid, libraries, timeval_context);
  std::array<std::byte, 16> timeval{};
  Check(timeval_result && memory.Read(0x1010, timeval) &&
            ReadSigned64(timeval, 0) == 1'725'000'123 &&
            ReadSigned64(timeval, 8) == 456'789,
        "gettimeofday export wrote the wrong value");

  HleCallContext null_timeval_context(memory);
  const auto null_timeval = registry.Dispatch(
      kajps5::hle::kKernelGettimeofdayName, libraries,
      null_timeval_context);
  Check(null_timeval &&
            null_timeval_context
                    .GetRegister(kajps5::hle::HleRegister::kRax)
                    .value_or(0) ==
                static_cast<std::uint64_t>(static_cast<std::int64_t>(
                    kajps5::hle::kKernelHleErrorFault)),
        "null timeval returned the wrong kernel result");

  HleCallContext invalid_clock_context(memory);
  Check(invalid_clock_context.SetRegister(kajps5::hle::HleRegister::kRdi, 99) &&
            invalid_clock_context.SetRegister(kajps5::hle::HleRegister::kRsi,
                                              0x1000),
        "invalid clock argument setup failed");
  const auto invalid_clock = registry.Dispatch(
      kajps5::hle::kKernelClockGettimeName, libraries,
      invalid_clock_context);
  Check(invalid_clock &&
            invalid_clock_context
                    .GetRegister(kajps5::hle::HleRegister::kRax)
                    .value_or(0) ==
                static_cast<std::uint64_t>(static_cast<std::int64_t>(
                    kajps5::hle::kKernelHleErrorInvalidArgument)),
        "invalid clock ID returned the wrong kernel result");

  HleCallContext null_context(memory);
  Check(null_context.SetRegister(kajps5::hle::HleRegister::kRdi,
                                 kajps5::kernel::kClockRealtime) &&
            null_context.SetRegister(kajps5::hle::HleRegister::kRsi, 0),
        "null clock pointer setup failed");
  const auto null_result = registry.Dispatch(
      kajps5::hle::kKernelClockGettimeName, libraries, null_context);
  Check(null_result &&
            null_context.GetRegister(kajps5::hle::HleRegister::kRax)
                    .value_or(0) ==
                static_cast<std::uint64_t>(static_cast<std::int64_t>(
                    kajps5::hle::kKernelHleErrorFault)),
        "null timespec returned the wrong kernel result");

  GuestMemory partial_memory(0x2000, 16,
                             kajps5::memory::GuestMemoryProtection::kNone);
  Check(partial_memory.Map(
            0x2000, 8,
            kajps5::memory::GuestMemoryProtection::kRead |
                kajps5::memory::GuestMemoryProtection::kWrite),
        "partial timespec mapping failed");
  const std::array sentinel = {
      std::byte{0xcc}, std::byte{0xcc}, std::byte{0xcc}, std::byte{0xcc},
      std::byte{0xcc}, std::byte{0xcc}, std::byte{0xcc}, std::byte{0xcc}};
  Check(partial_memory.Initialize(0x2000, sentinel),
        "partial timespec setup failed");
  HleCallContext partial_context(partial_memory);
  Check(partial_context.SetRegister(kajps5::hle::HleRegister::kRdi,
                                    kajps5::kernel::kClockRealtime) &&
            partial_context.SetRegister(kajps5::hle::HleRegister::kRsi,
                                        0x2000),
        "partial timespec argument setup failed");
  const auto partial_result = registry.Dispatch(
      kajps5::hle::kKernelClockGettimeName, libraries, partial_context);
  std::array<std::byte, 8> preserved{};
  Check(partial_result &&
            partial_context.GetRegister(kajps5::hle::HleRegister::kRax)
                    .value_or(0) ==
                static_cast<std::uint64_t>(static_cast<std::int64_t>(
                    kajps5::hle::kKernelHleErrorFault)) &&
            partial_memory.Read(0x2000, preserved) && preserved == sentinel,
        "failed timespec write changed guest memory");

  Check(kajps5::hle::RegisterKernelClockExports(registry, clock) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 10,
        "duplicate clock export batch changed the registry");
  const std::vector<std::string> wrong_library = {"libKernel"};
  Check(registry
            .Dispatch(kajps5::hle::kKernelGetProcessTimeName,
                      wrong_library, context)
            .status == ExportRegistryStatus::kNotFound,
        "clock export lookup ignored library case");
  return failures == 0 ? 0 : 1;
}
