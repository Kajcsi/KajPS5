// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "loader/launch_metadata.h"

namespace kajps5::loader {

inline constexpr std::size_t kMaximumLifecycleCalls = 65536;

enum class LifecyclePlanStatus {
  kOk,
  kLimitExceeded,
  kArrayReadFailed,
  kFunctionNotExecutable,
};

enum class LifecycleCallKind {
  kNone,
  kPreinitializer,
  kInitializer,
  kFinalizer,
};

struct ExecutableLifecyclePlan {
  std::vector<std::uint64_t> preinitializers;
  std::vector<std::uint64_t> initializers;
  std::vector<std::uint64_t> finalizers;
};

struct LifecyclePlanResult {
  LifecyclePlanStatus status = LifecyclePlanStatus::kOk;
  LifecycleCallKind failure_kind = LifecycleCallKind::kNone;
  std::size_t failure_index = 0;
  ExecutableLifecyclePlan plan;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == LifecyclePlanStatus::kOk;
  }
};

[[nodiscard]] LifecyclePlanResult BuildLifecycleCallPlan(
    const ExecutableLaunchMetadata& metadata,
    const memory::GuestMemory& memory, std::uint64_t load_bias = 0);
[[nodiscard]] std::string_view LifecyclePlanStatusName(
    LifecyclePlanStatus status) noexcept;
[[nodiscard]] std::string_view LifecycleCallKindName(
    LifecycleCallKind kind) noexcept;
[[nodiscard]] std::string FormatLifecyclePlanTrace(
    const LifecyclePlanResult& result);

}  // namespace kajps5::loader
