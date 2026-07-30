// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/kernel_pthread_exports.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "kernel/guest_scheduler.h"
#include "kernel/pthread.h"

namespace kajps5::hle {
namespace {

constexpr std::int32_t kPosixErrorNoMemory = 12;
constexpr std::int32_t kPosixErrorFault = 14;
constexpr std::int32_t kPosixErrorInvalidArgument = 22;
constexpr std::int32_t kPosixErrorTryAgain = 35;
constexpr std::int32_t kPosixErrorNoSuchProcess = 3;
constexpr std::int32_t kPosixErrorDeadlock = 11;

void SetSignedResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
}

std::int32_t PthreadStatusResult(kernel::KernelStatus status,
                                 bool posix_errors) noexcept {
  if (status == kernel::KernelStatus::kOk) {
    return 0;
  }
  if (posix_errors) {
    switch (status) {
      case kernel::KernelStatus::kNoResources: return kPosixErrorTryAgain;
      case kernel::KernelStatus::kPermissionDenied: return 1;
      case kernel::KernelStatus::kBusy:
      case kernel::KernelStatus::kWouldBlock: return 16;
      case kernel::KernelStatus::kNotFound:
      case kernel::KernelStatus::kNoSuchEntry:
      case kernel::KernelStatus::kInvalidArgument:
      case kernel::KernelStatus::kOk: return kPosixErrorInvalidArgument;
    }
  }

  switch (status) {
    case kernel::KernelStatus::kNoResources: return kKernelHleErrorTryAgain;
    case kernel::KernelStatus::kPermissionDenied:
      return kKernelHleErrorPermissionDenied;
    case kernel::KernelStatus::kBusy:
    case kernel::KernelStatus::kWouldBlock: return kKernelHleErrorBusy;
    case kernel::KernelStatus::kNotFound:
    case kernel::KernelStatus::kNoSuchEntry:
    case kernel::KernelStatus::kInvalidArgument:
    case kernel::KernelStatus::kOk: return kKernelHleErrorInvalidArgument;
  }
  return kKernelHleErrorInvalidArgument;
}

std::int32_t InvalidArgument(bool posix_errors) noexcept {
  return posix_errors ? kPosixErrorInvalidArgument
                      : kKernelHleErrorInvalidArgument;
}

std::int32_t MemoryFault(bool posix_errors) noexcept {
  return posix_errors ? kPosixErrorFault : kKernelHleErrorFault;
}

HleContextStatus PthreadSelf(HleCallContext& context,
                             kernel::GuestScheduler& scheduler) {
  context.SetReturn(scheduler.current_thread().value_or(0));
  return HleContextStatus::kOk;
}

HleContextStatus PthreadEqual(HleCallContext& context) {
  context.SetReturn(context.Argument(0).value_or(0) ==
                            context.Argument(1).value_or(0)
                        ? 1
                        : 0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadYield(HleCallContext& context,
                              kernel::GuestScheduler& scheduler) {
  (void)scheduler.YieldCurrent();
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadCreate(HleCallContext& context,
                               kernel::PthreadService& pthreads,
                               bool posix_errors,
                               std::uint64_t name_address) {
  const auto output_address = context.Argument(0).value_or(0);
  const auto attribute_address = context.Argument(1).value_or(0);
  const auto entry_address = context.Argument(2).value_or(0);
  const auto argument = context.Argument(3).value_or(0);
  if (output_address == 0 || entry_address == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }

  std::uint64_t attribute_handle = 0;
  if (attribute_address != 0 &&
      context.ReadUInt64(attribute_address, attribute_handle) !=
          HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }

  std::string name;
  if (name_address != 0) {
    const auto read_name = context.ReadNullTerminatedString(
        name_address, kernel::kMaximumGuestThreadNameLength + 1);
    if (!read_name) {
      SetSignedResult(context,
                      read_name.status == HleContextStatus::kUnterminatedString
                          ? (posix_errors ? kPosixErrorInvalidArgument
                                          : kKernelHleErrorNameTooLong)
                          : MemoryFault(posix_errors));
      return HleContextStatus::kOk;
    }
    name = read_name.value;
  }

  const auto created = pthreads.CreateThread(
      std::move(name), attribute_handle, entry_address, argument);
  if (!created) {
    SetSignedResult(context, PthreadStatusResult(created.status, posix_errors));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output_address, created.handle) !=
      HleContextStatus::kOk) {
    (void)pthreads.DiscardReadyThread(created.handle);
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadJoin(HleCallContext& context,
                             kernel::PthreadService& pthreads,
                             bool posix_errors) {
  const auto handle = context.Argument(0).value_or(0);
  const auto output_address = context.Argument(1).value_or(0);
  if (handle == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (output_address != 0 &&
      !context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }

  const auto joined = pthreads.JoinThread(handle);
  if (joined.status != kernel::KernelStatus::kOk) {
    if (joined.status == kernel::KernelStatus::kNotFound) {
      SetSignedResult(context, posix_errors ? kPosixErrorNoSuchProcess
                                            : kKernelHleErrorNoSuchProcess);
    } else if (joined.status == kernel::KernelStatus::kInvalidArgument &&
               posix_errors) {
      SetSignedResult(context, kPosixErrorDeadlock);
    } else {
      SetSignedResult(context,
                      PthreadStatusResult(joined.status, posix_errors));
    }
    return HleContextStatus::kOk;
  }
  if (output_address != 0 &&
      context.WriteUInt64(output_address, joined.exit_value) !=
          HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadExit(HleCallContext& context,
                             kernel::PthreadService& pthreads,
                             bool posix_errors) {
  const auto exit_value = context.Argument(0).value_or(0);
  if (!pthreads.ExitCurrent(exit_value)) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(exit_value);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadAttrInit(HleCallContext& context,
                                 kernel::PthreadService& pthreads,
                                 bool posix_errors) {
  const auto output_address = context.Argument(0).value_or(0);
  if (output_address == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }

  const auto created = pthreads.CreateAttribute();
  if (!created) {
    SetSignedResult(context, posix_errors ? kPosixErrorNoMemory
                                          : kKernelHleErrorNoMemory);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output_address, created.handle) !=
      HleContextStatus::kOk) {
    (void)pthreads.DestroyAttribute(created.handle);
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadAttrDestroy(HleCallContext& context,
                                    kernel::PthreadService& pthreads,
                                    bool posix_errors) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(address, sizeof(std::uint64_t))) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }

  std::uint64_t handle = 0;
  if (context.ReadUInt64(address, handle) != HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  const auto status = pthreads.DestroyAttribute(handle);
  if (status != kernel::KernelStatus::kOk) {
    SetSignedResult(context, PthreadStatusResult(status, posix_errors));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(address, 0) != HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadAttrSetstacksize(HleCallContext& context,
                                         kernel::PthreadService& pthreads,
                                         bool posix_errors) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  std::uint64_t handle = 0;
  if (context.ReadUInt64(address, handle) != HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  SetSignedResult(context,
                  PthreadStatusResult(pthreads.SetAttributeStackSize(
                                          handle,
                                          context.Argument(1).value_or(0)),
                                      posix_errors));
  return HleContextStatus::kOk;
}

HleContextStatus PthreadAttrGetstacksize(HleCallContext& context,
                                         kernel::PthreadService& pthreads,
                                         bool posix_errors) {
  const auto address = context.Argument(0).value_or(0);
  const auto output_address = context.Argument(1).value_or(0);
  if (address == 0 || output_address == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint64_t))) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  std::uint64_t handle = 0;
  if (context.ReadUInt64(address, handle) != HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  const auto attribute = pthreads.GetAttribute(handle);
  if (!attribute) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output_address, attribute->stack_size) !=
      HleContextStatus::kOk) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadKeyCreate(HleCallContext& context,
                                  kernel::PthreadService& pthreads,
                                  bool posix_errors) {
  const auto output_address = context.Argument(0).value_or(0);
  if (output_address == 0) {
    SetSignedResult(context, InvalidArgument(posix_errors));
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(output_address, sizeof(std::uint32_t))) {
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  const auto created =
      pthreads.CreateKey(context.Argument(1).value_or(0));
  if (!created) {
    SetSignedResult(context, PthreadStatusResult(created.status, posix_errors));
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt32(output_address, created.key) !=
      HleContextStatus::kOk) {
    (void)pthreads.DeleteKey(created.key);
    SetSignedResult(context, MemoryFault(posix_errors));
    return HleContextStatus::kOk;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus PthreadKeyDelete(HleCallContext& context,
                                  kernel::PthreadService& pthreads,
                                  bool posix_errors) {
  const auto key =
      static_cast<std::uint32_t>(context.Argument(0).value_or(0));
  SetSignedResult(context,
                  PthreadStatusResult(pthreads.DeleteKey(key), posix_errors));
  return HleContextStatus::kOk;
}

HleContextStatus PthreadSetspecific(HleCallContext& context,
                                    kernel::PthreadService& pthreads,
                                    bool posix_errors) {
  const auto key =
      static_cast<std::uint32_t>(context.Argument(0).value_or(0));
  SetSignedResult(context,
                  PthreadStatusResult(
                      pthreads.SetSpecific(key,
                                           context.Argument(1).value_or(0)),
                      posix_errors));
  return HleContextStatus::kOk;
}

HleContextStatus PthreadGetspecific(HleCallContext& context,
                                    kernel::PthreadService& pthreads) {
  const auto key =
      static_cast<std::uint32_t>(context.Argument(0).value_or(0));
  context.SetReturn(pthreads.GetSpecific(key).value);
  return HleContextStatus::kOk;
}

template <typename Handler>
void AddLibraryAliases(std::vector<HleExportDefinition>& exports,
                       const char* library, const char* name, const char* nid,
                       Handler handler) {
  exports.push_back({library, name, handler});
  exports.push_back({library, nid, std::move(handler)});
}

template <typename Handler>
void AddAliases(std::vector<HleExportDefinition>& exports, const char* name,
                const char* nid, Handler handler) {
  AddLibraryAliases(exports, kLibKernelName, name, nid, std::move(handler));
}

}  // namespace

std::vector<HleExportDefinition> detail::MakeKernelPthreadExports(
    kernel::PthreadService& pthreads, kernel::GuestScheduler& scheduler) {
  auto* const pthread_view = &pthreads;
  auto* const scheduler_view = &scheduler;
  std::vector<HleExportDefinition> exports;
  exports.reserve(60);

  AddAliases(exports, kPosixPthreadSelfName, kPosixPthreadSelfNid,
             [scheduler_view](HleCallContext& context) {
               return PthreadSelf(context, *scheduler_view);
             });
  AddAliases(exports, kKernelPthreadSelfName, kKernelPthreadSelfNid,
             [scheduler_view](HleCallContext& context) {
               return PthreadSelf(context, *scheduler_view);
             });
  AddAliases(exports, kPosixPthreadEqualName, kPosixPthreadEqualNid,
             [](HleCallContext& context) { return PthreadEqual(context); });
  AddAliases(exports, kKernelPthreadEqualName, kKernelPthreadEqualNid,
             [](HleCallContext& context) { return PthreadEqual(context); });
  AddAliases(exports, kPosixPthreadYieldName, kPosixPthreadYieldNid,
             [scheduler_view](HleCallContext& context) {
               return PthreadYield(context, *scheduler_view);
             });
  AddAliases(exports, kKernelPthreadYieldName, kKernelPthreadYieldNid,
             [scheduler_view](HleCallContext& context) {
               return PthreadYield(context, *scheduler_view);
             });
  AddAliases(exports, kPosixPthreadCreateName, kPosixPthreadCreateNid,
             [pthread_view](HleCallContext& context) {
               return PthreadCreate(context, *pthread_view, true, 0);
             });
  AddAliases(exports, kPosixPthreadCreateNameNpName,
             kPosixPthreadCreateNameNpNid,
             [pthread_view](HleCallContext& context) {
               return PthreadCreate(context, *pthread_view, true,
                                    context.Argument(4).value_or(0));
             });
  AddAliases(exports, kKernelPthreadCreateName, kKernelPthreadCreateNid,
             [pthread_view](HleCallContext& context) {
               return PthreadCreate(context, *pthread_view, false,
                                    context.Argument(4).value_or(0));
             });
  AddAliases(exports, kPosixPthreadJoinName, kPosixPthreadJoinNid,
             [pthread_view](HleCallContext& context) {
               return PthreadJoin(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadJoinName, kKernelPthreadJoinNid,
             [pthread_view](HleCallContext& context) {
               return PthreadJoin(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadExitName, kPosixPthreadExitNid,
             [pthread_view](HleCallContext& context) {
               return PthreadExit(context, *pthread_view, true);
             });
  AddLibraryAliases(exports, kLibScePosixName, kPosixPthreadExitName,
                    kPosixPthreadExitNid,
                    [pthread_view](HleCallContext& context) {
                      return PthreadExit(context, *pthread_view, true);
                    });
  AddAliases(exports, kKernelPthreadExitName, kKernelPthreadExitNid,
             [pthread_view](HleCallContext& context) {
               return PthreadExit(context, *pthread_view, false);
             });

  AddAliases(exports, kPosixPthreadAttrInitName, kPosixPthreadAttrInitNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrInit(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadAttrInitName, kKernelPthreadAttrInitNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrInit(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadAttrDestroyName,
             kPosixPthreadAttrDestroyNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrDestroy(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadAttrDestroyName,
             kKernelPthreadAttrDestroyNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrDestroy(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadAttrSetstacksizeName,
             kPosixPthreadAttrSetstacksizeNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrSetstacksize(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadAttrSetstacksizeName,
             kKernelPthreadAttrSetstacksizeNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrSetstacksize(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadAttrGetstacksizeName,
             kPosixPthreadAttrGetstacksizeNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrGetstacksize(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadAttrGetstacksizeName,
             kKernelPthreadAttrGetstacksizeNid,
             [pthread_view](HleCallContext& context) {
               return PthreadAttrGetstacksize(context, *pthread_view, false);
             });

  AddAliases(exports, kPosixPthreadKeyCreateName, kPosixPthreadKeyCreateNid,
             [pthread_view](HleCallContext& context) {
               return PthreadKeyCreate(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadKeyCreateName, kKernelPthreadKeyCreateNid,
             [pthread_view](HleCallContext& context) {
               return PthreadKeyCreate(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadKeyDeleteName, kPosixPthreadKeyDeleteNid,
             [pthread_view](HleCallContext& context) {
               return PthreadKeyDelete(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadKeyDeleteName, kKernelPthreadKeyDeleteNid,
             [pthread_view](HleCallContext& context) {
               return PthreadKeyDelete(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadSetspecificName,
             kPosixPthreadSetspecificNid,
             [pthread_view](HleCallContext& context) {
               return PthreadSetspecific(context, *pthread_view, true);
             });
  AddAliases(exports, kKernelPthreadSetspecificName,
             kKernelPthreadSetspecificNid,
             [pthread_view](HleCallContext& context) {
               return PthreadSetspecific(context, *pthread_view, false);
             });
  AddAliases(exports, kPosixPthreadGetspecificName,
             kPosixPthreadGetspecificNid,
             [pthread_view](HleCallContext& context) {
               return PthreadGetspecific(context, *pthread_view);
             });
  AddAliases(exports, kKernelPthreadGetspecificName,
             kKernelPthreadGetspecificNid,
             [pthread_view](HleCallContext& context) {
               return PthreadGetspecific(context, *pthread_view);
             });
  return exports;
}

ExportRegistryStatus RegisterKernelPthreadExports(
    ExportRegistry& registry, kernel::PthreadService& pthreads,
    kernel::GuestScheduler& scheduler) {
  return registry.RegisterBatch(
      detail::MakeKernelPthreadExports(pthreads, scheduler));
}

}  // namespace kajps5::hle
