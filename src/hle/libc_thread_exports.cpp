// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/libc_thread_exports.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "hle/libc_exports.h"
#include "kernel/pthread.h"

namespace kajps5::hle {
namespace {

constexpr std::uint64_t kThreadSuccess = 0;
constexpr std::uint64_t kThreadBusy = 3;
constexpr std::uint64_t kThreadError = 4;
constexpr std::uint64_t kRecursiveMutexFlag = 0x100;

HleContextStatus ReadMutexHandle(HleCallContext& context,
                                 std::uint64_t& handle) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kInvalidArgument;
  }
  if (context.ReadUInt64(address, handle) != HleContextStatus::kOk) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kMemoryFault;
  }
  return HleContextStatus::kOk;
}

HleContextStatus MtxInit(HleCallContext& context,
                         kernel::PthreadService& pthreads) {
  const auto address = context.Argument(0).value_or(0);
  const auto type = context.Argument(1).value_or(0);
  if (address == 0 || !context.CanWriteMemory(address, sizeof(std::uint64_t))) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kMemoryFault;
  }

  const auto mutex_type = (type & kRecursiveMutexFlag) != 0
                              ? kernel::kPthreadMutexRecursive
                              : kernel::kPthreadMutexErrorCheck;
  const auto created = pthreads.CreateMutex(0, mutex_type);
  if (!created) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(address, created.handle) != HleContextStatus::kOk) {
    (void)pthreads.DestroyMutex(created.handle);
    context.SetReturn(kThreadError);
    return HleContextStatus::kMemoryFault;
  }
  context.SetReturn(kThreadSuccess);
  return HleContextStatus::kOk;
}

HleContextStatus MtxDestroy(HleCallContext& context,
                            kernel::PthreadService& pthreads) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    context.SetReturn(kThreadSuccess);
    return HleContextStatus::kOk;
  }
  std::uint64_t handle = 0;
  const auto read_status = ReadMutexHandle(context, handle);
  if (read_status != HleContextStatus::kOk) {
    return read_status;
  }
  if (handle != 0 &&
      pthreads.DestroyMutex(handle) != kernel::KernelStatus::kOk) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(address, 0) != HleContextStatus::kOk) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kMemoryFault;
  }
  context.SetReturn(kThreadSuccess);
  return HleContextStatus::kOk;
}

HleContextStatus MtxLock(HleCallContext& context,
                         kernel::PthreadService& pthreads, bool try_only) {
  std::uint64_t handle = 0;
  const auto read_status = ReadMutexHandle(context, handle);
  if (read_status != HleContextStatus::kOk) {
    return read_status;
  }
  if (handle == 0) {
    context.SetReturn(try_only ? kThreadBusy : kThreadError);
    return HleContextStatus::kInvalidArgument;
  }
  const auto status = pthreads.LockMutex(handle, try_only);
  if (status == kernel::KernelStatus::kWouldBlock && !try_only) {
    return HleContextStatus::kBlocked;
  }
  context.SetReturn(status == kernel::KernelStatus::kOk
                        ? kThreadSuccess
                        : (try_only ? kThreadBusy : kThreadError));
  return HleContextStatus::kOk;
}

HleContextStatus MtxUnlock(HleCallContext& context,
                           kernel::PthreadService& pthreads) {
  std::uint64_t handle = 0;
  const auto read_status = ReadMutexHandle(context, handle);
  if (read_status != HleContextStatus::kOk) {
    return read_status;
  }
  if (handle == 0) {
    context.SetReturn(kThreadError);
    return HleContextStatus::kInvalidArgument;
  }
  context.SetReturn(pthreads.UnlockMutex(handle) == kernel::KernelStatus::kOk
                        ? kThreadSuccess
                        : kThreadError);
  return HleContextStatus::kOk;
}

HleContextStatus MtxCurrentOwns(HleCallContext& context,
                                kernel::PthreadService& pthreads) {
  std::uint64_t handle = 0;
  const auto read_status = ReadMutexHandle(context, handle);
  if (read_status != HleContextStatus::kOk) {
    return read_status;
  }
  if (handle == 0) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  context.SetReturn(
      pthreads.CurrentThreadOwnsMutex(handle).value_or(false) ? 1 : 0);
  return HleContextStatus::kOk;
}

void AddAliases(std::vector<HleExportDefinition>& exports,
                std::string_view name, std::string_view nid,
                const HleHandler& handler) {
  exports.push_back({kLibcName, std::string(name), handler});
  exports.push_back({kLibcName, std::string(nid), handler});
}

}  // namespace

ExportRegistryStatus RegisterLibcThreadExports(
    ExportRegistry& registry, kernel::PthreadService& pthreads) {
  std::vector<HleExportDefinition> exports;
  exports.reserve(18);
  auto* pthread_view = &pthreads;
  const HleHandler init = [pthread_view](HleCallContext& context) {
    return MtxInit(context, *pthread_view);
  };
  AddAliases(exports, kLibcMtxInitName, kLibcMtxInitNid, init);
  AddAliases(exports, kLibcMtxInitWithNameName, kLibcMtxInitWithNameNid, init);
  AddAliases(exports, kLibcMtxInitWithDefaultNameOverrideName,
             kLibcMtxInitWithDefaultNameOverrideNid, init);
  AddAliases(exports, kLibcMtxDestroyName, kLibcMtxDestroyNid,
             [pthread_view](HleCallContext& context) {
               return MtxDestroy(context, *pthread_view);
             });
  AddAliases(exports, kLibcMtxLockName, kLibcMtxLockNid,
             [pthread_view](HleCallContext& context) {
               return MtxLock(context, *pthread_view, false);
             });
  AddAliases(exports, kLibcMtxTrylockName, kLibcMtxTrylockNid,
             [pthread_view](HleCallContext& context) {
               return MtxLock(context, *pthread_view, true);
             });
  AddAliases(exports, kLibcMtxTimedlockName, kLibcMtxTimedlockNid,
             [pthread_view](HleCallContext& context) {
               return MtxLock(context, *pthread_view, false);
             });
  AddAliases(exports, kLibcMtxUnlockName, kLibcMtxUnlockNid,
             [pthread_view](HleCallContext& context) {
               return MtxUnlock(context, *pthread_view);
             });
  AddAliases(exports, kLibcMtxCurrentOwnsName, kLibcMtxCurrentOwnsNid,
             [pthread_view](HleCallContext& context) {
               return MtxCurrentOwns(context, *pthread_view);
             });
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
