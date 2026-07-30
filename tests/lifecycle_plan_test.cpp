// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "loader/lifecycle_plan.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    ++failures;
  }
}

void Write64(kajps5::memory::GuestMemory& memory, std::uint64_t address,
             std::uint64_t value) {
  std::array<std::byte, sizeof(value)> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  Check(memory.Initialize(address, bytes), "test array write failed");
}

}  // namespace

int main() {
  using kajps5::loader::BuildLifecycleCallPlan;
  using kajps5::loader::ExecutableFunctionArrayMetadata;
  using kajps5::loader::ExecutableLaunchMetadata;
  using kajps5::loader::FormatLifecyclePlanTrace;
  using kajps5::loader::LifecycleCallKind;
  using kajps5::loader::LifecyclePlanStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(0x1000, 0x2000, GuestMemoryProtection::kNone);
  Check(memory.Map(0x1000, 0x100,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kExecute),
        "executable test range could not be mapped");
  Check(memory.Map(0x2000, 0x100,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "array test range could not be mapped");

  Write64(memory, 0x2000, 0x1030);
  Write64(memory, 0x2008, 0);
  Write64(memory, 0x2010, 0x1030);
  Write64(memory, 0x2018, std::numeric_limits<std::uint64_t>::max());
  Write64(memory, 0x2020, 0x1040);
  Write64(memory, 0x2028, 0x1010);
  Write64(memory, 0x2030, 0x1050);
  Write64(memory, 0x2040, 0x1060);
  Write64(memory, 0x2048, 0x1070);
  Write64(memory, 0x2050, 0x1060);

  ExecutableLaunchMetadata metadata;
  metadata.init_function = 0x1010;
  metadata.fini_function = 0x1020;
  metadata.preinit_array = ExecutableFunctionArrayMetadata{0x2000, 4};
  metadata.init_array = ExecutableFunctionArrayMetadata{0x2020, 3};
  metadata.fini_array = ExecutableFunctionArrayMetadata{0x2040, 3};

  const auto valid = BuildLifecycleCallPlan(metadata, memory);
  Check(valid &&
            valid.plan.preinitializers ==
                std::vector<std::uint64_t>({0x1030}) &&
            valid.plan.initializers ==
                std::vector<std::uint64_t>({0x1010, 0x1040, 0x1050}) &&
            valid.plan.finalizers ==
                std::vector<std::uint64_t>({0x1060, 0x1070, 0x1020}),
        "valid lifecycle call order is incorrect");
  const std::string expected_trace =
      "lifecycle.status=ok\n"
      "lifecycle.preinit_calls=1\n"
      "lifecycle.init_calls=3\n"
      "lifecycle.fini_calls=3\n"
      "lifecycle.failure_kind=none\n"
      "lifecycle.failure_index=0\n";
  Check(FormatLifecyclePlanTrace(valid) == expected_trace,
        "stable lifecycle trace changed");

  ExecutableLaunchMetadata fallback_metadata;
  fallback_metadata.init_function = 0x10;
  fallback_metadata.init_array =
      ExecutableFunctionArrayMetadata{0x2020, 1};
  Write64(memory, 0x2020, 0x40);
  const auto fallback =
      BuildLifecycleCallPlan(fallback_metadata, memory, 0x1000);
  Check(fallback && fallback.plan.initializers ==
                        std::vector<std::uint64_t>({0x1010, 0x1040}),
        "load-bias fallback did not resolve lifecycle functions");
  Write64(memory, 0x2020, 0x1040);

  auto unreadable_metadata = metadata;
  unreadable_metadata.preinit_array =
      ExecutableFunctionArrayMetadata{0x2100, 1};
  const auto unreadable = BuildLifecycleCallPlan(unreadable_metadata, memory);
  Check(unreadable.status == LifecyclePlanStatus::kArrayReadFailed &&
            unreadable.failure_kind == LifecycleCallKind::kPreinitializer &&
            unreadable.plan.preinitializers.empty() &&
            unreadable.plan.initializers.empty() &&
            unreadable.plan.finalizers.empty(),
        "unreadable lifecycle array was not rejected transactionally");

  Write64(memory, 0x2020, 0x2000);
  auto non_executable_metadata = metadata;
  non_executable_metadata.init_array =
      ExecutableFunctionArrayMetadata{0x2020, 1};
  const auto non_executable =
      BuildLifecycleCallPlan(non_executable_metadata, memory);
  Check(non_executable.status ==
            LifecyclePlanStatus::kFunctionNotExecutable &&
            non_executable.failure_kind == LifecycleCallKind::kInitializer &&
            non_executable.failure_index == 0 &&
            non_executable.plan.initializers.empty(),
        "non-executable lifecycle function was accepted");
  Write64(memory, 0x2020, 0x1040);

  auto invalid_direct = metadata;
  invalid_direct.init_function = 0x2000;
  const auto direct_failure = BuildLifecycleCallPlan(invalid_direct, memory);
  Check(direct_failure.status ==
            LifecyclePlanStatus::kFunctionNotExecutable &&
            direct_failure.failure_kind == LifecycleCallKind::kInitializer,
        "non-executable direct initializer was accepted");

  ExecutableLaunchMetadata too_many;
  too_many.init_array = ExecutableFunctionArrayMetadata{
      0x2000, kajps5::loader::kMaximumLifecycleCalls + 1};
  Check(BuildLifecycleCallPlan(too_many, memory).status ==
            LifecyclePlanStatus::kLimitExceeded,
        "oversized lifecycle plan was accepted");

  return failures == 0 ? 0 : 1;
}
