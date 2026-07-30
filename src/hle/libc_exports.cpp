// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/libc_exports.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "kernel/cxa_guard.h"

namespace kajps5::hle {
namespace {

constexpr std::uint64_t kGuardComplete = 0x0001;
constexpr std::uint64_t kGuardPending = 0x0100;
constexpr std::uint64_t kGuardStateMask = 0xffff;

HleContextStatus CxaGuardAcquire(HleCallContext& context,
                                 kernel::CxaGuardService& guards) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }

  std::uint64_t word = 0;
  if (context.ReadUInt64(address, word) != HleContextStatus::kOk) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  const auto acquired = guards.Acquire(address, (word & kGuardComplete) != 0);
  if (acquired.status == kernel::KernelStatus::kWouldBlock) {
    return HleContextStatus::kBlocked;
  }
  if (!acquired) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }
  if (!acquired.should_initialize) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }

  const auto pending = (word & ~kGuardStateMask) | kGuardPending;
  if (context.WriteUInt64(address, pending) != HleContextStatus::kOk) {
    (void)guards.Abort(address);
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  context.SetReturn(1);
  return HleContextStatus::kOk;
}

HleContextStatus CompleteGuard(HleCallContext& context,
                               kernel::CxaGuardService& guards,
                               std::uint64_t state) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }

  std::uint64_t word = 0;
  if (context.ReadUInt64(address, word) != HleContextStatus::kOk ||
      !context.CanWriteMemory(address, sizeof(word))) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  const auto completed = state == kGuardComplete ? guards.Release(address)
                                                  : guards.Abort(address);
  if (completed != kernel::KernelStatus::kOk) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }
  const auto updated = (word & ~kGuardStateMask) | state;
  if (context.WriteUInt64(address, updated) != HleContextStatus::kOk) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

template <typename Handler>
void AddAliases(std::vector<HleExportDefinition>& exports, const char* name,
                const char* nid, Handler handler) {
  exports.push_back({kLibcName, name, handler});
  exports.push_back({kLibcName, nid, std::move(handler)});
}

}  // namespace

ExportRegistryStatus RegisterLibcExports(ExportRegistry& registry,
                                         kernel::CxaGuardService& guards) {
  auto* const guard_view = &guards;
  std::vector<HleExportDefinition> exports;
  exports.reserve(6);
  AddAliases(exports, kCxaGuardAcquireName, kCxaGuardAcquireNid,
             [guard_view](HleCallContext& context) {
               return CxaGuardAcquire(context, *guard_view);
             });
  AddAliases(exports, kCxaGuardReleaseName, kCxaGuardReleaseNid,
             [guard_view](HleCallContext& context) {
               return CompleteGuard(context, *guard_view, kGuardComplete);
             });
  AddAliases(exports, kCxaGuardAbortName, kCxaGuardAbortNid,
             [guard_view](HleCallContext& context) {
               return CompleteGuard(context, *guard_view, 0);
             });
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
