// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/lifecycle_plan.h"

#include <array>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace kajps5::loader {
namespace {

using AddressSet = std::unordered_set<std::uint64_t>;

bool AddCount(std::uint64_t count, std::size_t& total) noexcept {
  if (count > kMaximumLifecycleCalls - total) {
    return false;
  }
  total += static_cast<std::size_t>(count);
  return true;
}

bool ReadAddress(const memory::GuestMemory& memory, std::uint64_t base,
                 std::uint64_t index, std::uint64_t& value) noexcept {
  if (index > (std::numeric_limits<std::uint64_t>::max() - base) /
                  sizeof(std::uint64_t)) {
    return false;
  }
  std::array<std::byte, sizeof(std::uint64_t)> bytes{};
  if (!memory.Read(base + index * sizeof(std::uint64_t), bytes)) {
    return false;
  }
  value = 0;
  for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(
                 bytes[byte]))
             << (byte * 8U);
  }
  return true;
}

bool AppendFunction(std::uint64_t value, std::uint64_t load_bias,
                    const memory::GuestMemory& memory,
                    std::vector<std::uint64_t>& destination,
                    AddressSet& seen) {
  if (value == 0 || value == std::numeric_limits<std::uint64_t>::max()) {
    return true;
  }

  auto resolved = value;
  if (!memory.CanExecute(resolved, 1)) {
    if (load_bias == 0 ||
        value > std::numeric_limits<std::uint64_t>::max() - load_bias) {
      return false;
    }
    resolved = value + load_bias;
    if (!memory.CanExecute(resolved, 1)) {
      return false;
    }
  }
  if (seen.insert(resolved).second) {
    destination.push_back(resolved);
  }
  return true;
}

bool AppendArray(const ExecutableFunctionArrayMetadata& array,
                 bool reverse, LifecycleCallKind kind,
                 std::uint64_t load_bias, const memory::GuestMemory& memory,
                 std::vector<std::uint64_t>& destination, AddressSet& seen,
                 LifecyclePlanResult& result) {
  for (std::uint64_t cursor = 0; cursor < array.entry_count; ++cursor) {
    const auto index = reverse ? array.entry_count - cursor - 1 : cursor;
    std::uint64_t value = 0;
    if (!ReadAddress(memory, array.address, index, value)) {
      result.status = LifecyclePlanStatus::kArrayReadFailed;
      result.failure_kind = kind;
      result.failure_index = static_cast<std::size_t>(index);
      return false;
    }
    if (!AppendFunction(value, load_bias, memory, destination, seen)) {
      result.status = LifecyclePlanStatus::kFunctionNotExecutable;
      result.failure_kind = kind;
      result.failure_index = static_cast<std::size_t>(index);
      return false;
    }
  }
  return true;
}

}  // namespace

LifecyclePlanResult BuildLifecycleCallPlan(
    const ExecutableLaunchMetadata& metadata,
    const memory::GuestMemory& memory, std::uint64_t load_bias) {
  LifecyclePlanResult result;
  std::size_t call_limit = 0;
  const auto count_array = [&call_limit](
                               const std::optional<
                                   ExecutableFunctionArrayMetadata>& array) {
    return !array.has_value() || AddCount(array->entry_count, call_limit);
  };
  if (!count_array(metadata.preinit_array) ||
      !count_array(metadata.init_array) ||
      !count_array(metadata.fini_array) ||
      !AddCount(metadata.init_function.has_value() ? 1 : 0, call_limit) ||
      !AddCount(metadata.fini_function.has_value() ? 1 : 0, call_limit)) {
    result.status = LifecyclePlanStatus::kLimitExceeded;
    return result;
  }

  ExecutableLifecyclePlan plan;
  plan.preinitializers.reserve(
      metadata.preinit_array.has_value()
          ? static_cast<std::size_t>(metadata.preinit_array->entry_count)
          : 0);
  plan.initializers.reserve(
      (metadata.init_function.has_value() ? 1U : 0U) +
      (metadata.init_array.has_value()
           ? static_cast<std::size_t>(metadata.init_array->entry_count)
           : 0U));
  plan.finalizers.reserve(
      (metadata.fini_function.has_value() ? 1U : 0U) +
      (metadata.fini_array.has_value()
           ? static_cast<std::size_t>(metadata.fini_array->entry_count)
           : 0U));

  AddressSet preinitializer_set;
  AddressSet initializer_set;
  AddressSet finalizer_set;
  if (metadata.preinit_array.has_value() &&
      !AppendArray(*metadata.preinit_array, false,
                   LifecycleCallKind::kPreinitializer, load_bias, memory,
                   plan.preinitializers, preinitializer_set, result)) {
    return result;
  }
  if (metadata.init_function.has_value() &&
      !AppendFunction(*metadata.init_function, load_bias, memory,
                      plan.initializers, initializer_set)) {
    result.status = LifecyclePlanStatus::kFunctionNotExecutable;
    result.failure_kind = LifecycleCallKind::kInitializer;
    return result;
  }
  if (metadata.init_array.has_value() &&
      !AppendArray(*metadata.init_array, false,
                   LifecycleCallKind::kInitializer, load_bias, memory,
                   plan.initializers, initializer_set, result)) {
    return result;
  }
  if (metadata.fini_array.has_value() &&
      !AppendArray(*metadata.fini_array, true,
                   LifecycleCallKind::kFinalizer, load_bias, memory,
                   plan.finalizers, finalizer_set, result)) {
    return result;
  }
  if (metadata.fini_function.has_value() &&
      !AppendFunction(*metadata.fini_function, load_bias, memory,
                      plan.finalizers, finalizer_set)) {
    result.status = LifecyclePlanStatus::kFunctionNotExecutable;
    result.failure_kind = LifecycleCallKind::kFinalizer;
    return result;
  }

  result.plan = std::move(plan);
  return result;
}

std::string_view LifecyclePlanStatusName(
    LifecyclePlanStatus status) noexcept {
  switch (status) {
    case LifecyclePlanStatus::kOk: return "ok";
    case LifecyclePlanStatus::kLimitExceeded: return "limit-exceeded";
    case LifecyclePlanStatus::kArrayReadFailed: return "array-read-failed";
    case LifecyclePlanStatus::kFunctionNotExecutable:
      return "function-not-executable";
  }
  return "unknown";
}

std::string_view LifecycleCallKindName(LifecycleCallKind kind) noexcept {
  switch (kind) {
    case LifecycleCallKind::kNone: return "none";
    case LifecycleCallKind::kPreinitializer: return "preinitializer";
    case LifecycleCallKind::kInitializer: return "initializer";
    case LifecycleCallKind::kFinalizer: return "finalizer";
  }
  return "unknown";
}

std::string FormatLifecyclePlanTrace(const LifecyclePlanResult& result) {
  std::ostringstream trace;
  trace.imbue(std::locale::classic());
  trace << "lifecycle.status=" << LifecyclePlanStatusName(result.status)
        << '\n'
        << "lifecycle.preinit_calls=" << result.plan.preinitializers.size()
        << '\n'
        << "lifecycle.init_calls=" << result.plan.initializers.size() << '\n'
        << "lifecycle.fini_calls=" << result.plan.finalizers.size() << '\n'
        << "lifecycle.failure_kind="
        << LifecycleCallKindName(result.failure_kind) << '\n'
        << "lifecycle.failure_index=" << result.failure_index << '\n';
  return trace.str();
}

}  // namespace kajps5::loader
