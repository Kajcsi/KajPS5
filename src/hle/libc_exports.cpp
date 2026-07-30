// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/libc_exports.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "kernel/cxa_guard.h"
#include "kernel/libc_heap.h"
#include "kernel/process_lifecycle.h"

namespace kajps5::hle {
namespace {

constexpr std::uint64_t kGuardComplete = 0x0001;
constexpr std::uint64_t kGuardPending = 0x0100;
constexpr std::uint64_t kGuardStateMask = 0xffff;
constexpr std::int32_t kMaximumInitArgumentCount = 2;
constexpr std::uint64_t kPosixEinval = 22;
constexpr std::uint64_t kPosixEnomem = 12;

bool IsPowerOfTwo(std::uint64_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

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

HleContextStatus InitEnvironment(
    HleCallContext& context, kernel::ProcessLifecycleService& lifecycle) {
  const auto params = context.Argument(0).value_or(0);
  if (params == 0) {
    lifecycle.ResetEnvironment();
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  if (params > std::numeric_limits<std::uint64_t>::max() - 32) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }

  std::uint32_t argc_word = 0;
  std::uint64_t ignored = 0;
  if (context.ReadUInt32(params, argc_word) != HleContextStatus::kOk ||
      context.ReadUInt64(params + 8, ignored) != HleContextStatus::kOk ||
      context.ReadUInt64(params + 16, ignored) != HleContextStatus::kOk ||
      context.ReadUInt64(params + 24, ignored) != HleContextStatus::kOk) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  const auto argc = std::bit_cast<std::int32_t>(argc_word);
  if (argc < 0 || argc > kMaximumInitArgumentCount) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }
  const auto argv = params + 8;
  const auto envp = argv + (static_cast<std::uint64_t>(argc) + 1) * 8;
  if (lifecycle.InitializeEnvironment(argc, argv, envp) !=
      kernel::KernelStatus::kOk) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

bool IsExecutableGuestAddress(const HleCallContext& context,
                              std::uint64_t address) {
  const auto region = context.QueryMemoryRegion(address);
  if (!region) {
    return false;
  }
  const auto protection = static_cast<std::uint8_t>(region->protection);
  const auto execute = static_cast<std::uint8_t>(
      memory::GuestMemoryProtection::kExecute);
  return (protection & execute) != 0;
}

HleContextStatus RegisterAtexit(
    HleCallContext& context, kernel::ProcessLifecycleService& lifecycle) {
  const auto function = context.Argument(0).value_or(0);
  if (function != 0 && !IsExecutableGuestAddress(context, function)) {
    context.SetReturn(1);
    return HleContextStatus::kMemoryFault;
  }
  if (lifecycle.RegisterAtexit(function) == kernel::KernelStatus::kNoResources) {
    context.SetReturn(1);
    return HleContextStatus::kResourceLimit;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus RegisterCxaDestructor(
    HleCallContext& context, kernel::ProcessLifecycleService& lifecycle) {
  const kernel::GuestCxaDestructor destructor = {
      context.Argument(0).value_or(0), context.Argument(1).value_or(0),
      context.Argument(2).value_or(0)};
  if (destructor.function != 0 &&
      !IsExecutableGuestAddress(context, destructor.function)) {
    context.SetReturn(1);
    return HleContextStatus::kMemoryFault;
  }
  if (lifecycle.RegisterCxaDestructor(destructor) ==
      kernel::KernelStatus::kNoResources) {
    context.SetReturn(1);
    return HleContextStatus::kResourceLimit;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus RequestExit(HleCallContext& context,
                             kernel::ProcessLifecycleService& lifecycle) {
  const auto status = static_cast<std::uint32_t>(
      context.Argument(0).value_or(0));
  lifecycle.RequestExit(std::bit_cast<std::int32_t>(status));
  context.SetReturn(context.Argument(0).value_or(0));
  return HleContextStatus::kGuestExit;
}

HleContextStatus AllocateHeap(HleCallContext& context,
                              kernel::LibcHeapService& heap,
                              memory::GuestMemory& memory,
                              std::uint64_t size,
                              std::uint64_t alignment,
                              bool zero_fill) {
  const auto allocated = heap.Allocate(memory, size, alignment, zero_fill);
  context.SetReturn(allocated ? allocated.address : 0);
  return HleContextStatus::kOk;
}

HleContextStatus Malloc(HleCallContext& context,
                        kernel::LibcHeapService& heap,
                        memory::GuestMemory& memory) {
  return AllocateHeap(context, heap, memory,
                      context.Argument(0).value_or(0),
                      kernel::kDefaultLibcHeapAlignment, false);
}

HleContextStatus Free(HleCallContext& context,
                      kernel::LibcHeapService& heap,
                      memory::GuestMemory& memory) {
  (void)heap.Release(memory, context.Argument(0).value_or(0));
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus Calloc(HleCallContext& context,
                        kernel::LibcHeapService& heap,
                        memory::GuestMemory& memory) {
  const auto count = context.Argument(0).value_or(0);
  const auto element_size = context.Argument(1).value_or(0);
  if (count != 0 &&
      element_size > std::numeric_limits<std::uint64_t>::max() / count) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  return AllocateHeap(context, heap, memory, count * element_size,
                      kernel::kDefaultLibcHeapAlignment, true);
}

HleContextStatus Realloc(HleCallContext& context,
                         kernel::LibcHeapService& heap,
                         memory::GuestMemory& memory) {
  const auto resized = heap.Reallocate(
      memory, context.Argument(0).value_or(0),
      context.Argument(1).value_or(0));
  context.SetReturn(resized ? resized.address : 0);
  return HleContextStatus::kOk;
}

HleContextStatus Memalign(HleCallContext& context,
                          kernel::LibcHeapService& heap,
                          memory::GuestMemory& memory,
                          bool require_size_multiple) {
  const auto alignment = context.Argument(0).value_or(0);
  const auto size = context.Argument(1).value_or(0);
  if (!IsPowerOfTwo(alignment) ||
      (require_size_multiple && size % alignment != 0)) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  return AllocateHeap(context, heap, memory, size, alignment, false);
}

HleContextStatus PosixMemalign(HleCallContext& context,
                               kernel::LibcHeapService& heap,
                               memory::GuestMemory& memory) {
  const auto output = context.Argument(0).value_or(0);
  const auto alignment = context.Argument(1).value_or(0);
  const auto size = context.Argument(2).value_or(0);
  if (output == 0 || !IsPowerOfTwo(alignment) ||
      alignment % sizeof(std::uint64_t) != 0) {
    if (output != 0 &&
        context.WriteUInt64(output, 0) != HleContextStatus::kOk) {
      context.SetReturn(kPosixEinval);
      return HleContextStatus::kMemoryFault;
    }
    context.SetReturn(kPosixEinval);
    return HleContextStatus::kOk;
  }

  const auto allocated = heap.Allocate(memory, size, alignment, false);
  if (!allocated) {
    (void)context.WriteUInt64(output, 0);
    context.SetReturn(kPosixEnomem);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output, allocated.address) !=
      HleContextStatus::kOk) {
    (void)heap.Release(memory, allocated.address);
    context.SetReturn(kPosixEinval);
    return HleContextStatus::kMemoryFault;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus MallocUsableSize(HleCallContext& context,
                                  kernel::LibcHeapService& heap,
                                  memory::GuestMemory& memory) {
  context.SetReturn(heap.UsableSize(
                             memory, context.Argument(0).value_or(0))
                        .value_or(0));
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
                                         kernel::CxaGuardService& guards,
                                         kernel::ProcessLifecycleService& lifecycle,
                                         kernel::LibcHeapService& heap,
                                         memory::GuestMemory& memory) {
  auto* const guard_view = &guards;
  auto* const lifecycle_view = &lifecycle;
  auto* const heap_view = &heap;
  auto* const memory_view = &memory;
  std::vector<HleExportDefinition> exports;
  exports.reserve(36);
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
  AddAliases(exports, kCxaPureVirtualName, kCxaPureVirtualNid,
             [](HleCallContext&) {
               return HleContextStatus::kFatalGuestError;
             });
  AddAliases(exports, kLibcInitEnvName, kLibcInitEnvNid,
             [lifecycle_view](HleCallContext& context) {
               return InitEnvironment(context, *lifecycle_view);
             });
  AddAliases(exports, kLibcAtexitName, kLibcAtexitNid,
             [lifecycle_view](HleCallContext& context) {
               return RegisterAtexit(context, *lifecycle_view);
             });
  AddAliases(exports, kLibcCxaAtexitName, kLibcCxaAtexitNid,
             [lifecycle_view](HleCallContext& context) {
               return RegisterCxaDestructor(context, *lifecycle_view);
             });
  AddAliases(exports, kLibcCatchReturnFromMainName,
             kLibcCatchReturnFromMainNid,
             [lifecycle_view](HleCallContext& context) {
               return RequestExit(context, *lifecycle_view);
             });
  AddAliases(exports, kLibcExitName, kLibcExitNid,
             [lifecycle_view](HleCallContext& context) {
               return RequestExit(context, *lifecycle_view);
             });
  AddAliases(exports, kLibcAbortName, kLibcAbortNid,
             [](HleCallContext&) {
               return HleContextStatus::kFatalGuestError;
             });
  AddAliases(exports, kLibcMallocName, kLibcMallocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return Malloc(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcFreeName, kLibcFreeNid,
             [heap_view, memory_view](HleCallContext& context) {
               return Free(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcCallocName, kLibcCallocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return Calloc(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcReallocName, kLibcReallocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return Realloc(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMemalignName, kLibcMemalignNid,
             [heap_view, memory_view](HleCallContext& context) {
               return Memalign(context, *heap_view, *memory_view, false);
             });
  AddAliases(exports, kLibcAlignedAllocName, kLibcAlignedAllocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return Memalign(context, *heap_view, *memory_view, true);
             });
  AddAliases(exports, kLibcPosixMemalignName, kLibcPosixMemalignNid,
             [heap_view, memory_view](HleCallContext& context) {
               return PosixMemalign(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMallocUsableSizeName,
             kLibcMallocUsableSizeNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MallocUsableSize(context, *heap_view, *memory_view);
             });
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
