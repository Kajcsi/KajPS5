// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/libc_exports.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
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
constexpr std::size_t kMemoryCopyChunkBytes = 4096;
constexpr std::size_t kMaximumLibcWideUnits = 1024 * 1024;

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

HleContextStatus Memset(HleCallContext& context) {
  const auto destination = context.Argument(0).value_or(0);
  const auto value = static_cast<std::byte>(
      context.Argument(1).value_or(0) & 0xffU);
  const auto length = context.Argument(2).value_or(0);
  context.SetReturn(destination);
  if (length == 0) {
    return HleContextStatus::kOk;
  }
  if (!context.CanWriteMemory(destination, length)) {
    return HleContextStatus::kMemoryFault;
  }
  return context.FillMemory(destination, length, value);
}

HleContextStatus Memcpy(HleCallContext& context) {
  const auto destination = context.Argument(0).value_or(0);
  const auto source = context.Argument(1).value_or(0);
  const auto length = context.Argument(2).value_or(0);
  context.SetReturn(destination);
  if (length == 0) {
    return HleContextStatus::kOk;
  }
  if (!context.CanReadMemory(source, length) ||
      !context.CanWriteMemory(destination, length)) {
    return HleContextStatus::kMemoryFault;
  }

  std::array<std::byte, kMemoryCopyChunkBytes> bytes{};
  std::uint64_t copied = 0;
  while (copied < length) {
    const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(
        bytes.size(), length - copied));
    const auto view = std::span(bytes).first(chunk);
    if (context.ReadMemory(source + copied, view) != HleContextStatus::kOk ||
        context.WriteMemory(destination + copied, view) !=
            HleContextStatus::kOk) {
      return HleContextStatus::kMemoryFault;
    }
    copied += chunk;
  }
  return HleContextStatus::kOk;
}

HleContextStatus Strcmp(HleCallContext& context) {
  const auto left = context.ReadNullTerminatedString(
      context.Argument(0).value_or(0), kMaximumHleStringBytes);
  if (!left) {
    context.SetReturn(0);
    return left.status;
  }
  const auto right = context.ReadNullTerminatedString(
      context.Argument(1).value_or(0), kMaximumHleStringBytes);
  if (!right) {
    context.SetReturn(0);
    return right.status;
  }

  const auto common = std::min(left.value.size(), right.value.size());
  std::int32_t result = 0;
  for (std::size_t index = 0; index < common; ++index) {
    const auto left_byte =
        static_cast<unsigned char>(left.value[index]);
    const auto right_byte =
        static_cast<unsigned char>(right.value[index]);
    if (left_byte != right_byte) {
      result = static_cast<std::int32_t>(left_byte) -
               static_cast<std::int32_t>(right_byte);
      break;
    }
  }
  if (result == 0 && left.value.size() != right.value.size()) {
    result = left.value.size() < right.value.size() ? -1 : 1;
  }
  context.SetReturn(static_cast<std::uint64_t>(
      static_cast<std::int64_t>(result)));
  return HleContextStatus::kOk;
}

HleContextStatus Strlen(HleCallContext& context) {
  const auto value = context.ReadNullTerminatedString(
      context.Argument(0).value_or(0), kMaximumHleStringBytes);
  context.SetReturn(value ? value.value.size() : 0);
  return value.status;
}

HleContextStatus Wcscmp(HleCallContext& context) {
  const auto left = context.Argument(0).value_or(0);
  const auto right = context.Argument(1).value_or(0);
  if (left == 0 || right == 0) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }

  for (std::size_t index = 0; index < kMaximumLibcWideUnits; ++index) {
    constexpr std::size_t kWideUnitBytes = sizeof(std::uint16_t);
    if (index > (std::numeric_limits<std::uint64_t>::max() - left) /
                    kWideUnitBytes ||
        index > (std::numeric_limits<std::uint64_t>::max() - right) /
                    kWideUnitBytes) {
      context.SetReturn(0);
      return HleContextStatus::kMemoryFault;
    }
    std::array<std::byte, kWideUnitBytes> left_bytes{};
    std::array<std::byte, kWideUnitBytes> right_bytes{};
    const auto offset = static_cast<std::uint64_t>(index * kWideUnitBytes);
    if (context.ReadMemory(left + offset, left_bytes) !=
            HleContextStatus::kOk ||
        context.ReadMemory(right + offset, right_bytes) !=
            HleContextStatus::kOk) {
      context.SetReturn(0);
      return HleContextStatus::kMemoryFault;
    }
    const auto left_unit = static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(left_bytes[0]) |
        (std::to_integer<unsigned char>(left_bytes[1]) << 8U));
    const auto right_unit = static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(right_bytes[0]) |
        (std::to_integer<unsigned char>(right_bytes[1]) << 8U));
    if (left_unit != right_unit || left_unit == 0) {
      const auto result = static_cast<std::int32_t>(left_unit) -
                          static_cast<std::int32_t>(right_unit);
      context.SetReturn(static_cast<std::uint64_t>(
          static_cast<std::int64_t>(result)));
      return HleContextStatus::kOk;
    }
  }
  context.SetReturn(0);
  return HleContextStatus::kUnterminatedString;
}

template <typename T>
std::optional<T> ScalarArgument(const HleCallContext& context,
                                std::size_t index) noexcept {
  const auto vector = context.VectorArgument(index);
  if (!vector) {
    return std::nullopt;
  }
  T value{};
  static_assert(sizeof(value) <= kHleVectorRegisterBytes);
  std::memcpy(&value, vector->data(), sizeof(value));
  return value;
}

template <typename T>
HleContextStatus SetScalarReturn(HleCallContext& context, T value) noexcept {
  HleVectorValue result{};
  static_assert(sizeof(value) <= kHleVectorRegisterBytes);
  std::memcpy(result.data(), &value, sizeof(value));
  return context.SetVectorReturn(0, result)
             ? HleContextStatus::kOk
             : HleContextStatus::kInvalidArgument;
}

template <typename T, typename Operation>
HleContextStatus UnaryMath(HleCallContext& context, Operation operation) {
  const auto input = ScalarArgument<T>(context, 0);
  if (!input) {
    return HleContextStatus::kInvalidArgument;
  }
  return SetScalarReturn(context, static_cast<T>(operation(*input)));
}

HleContextStatus Atan2(HleCallContext& context) {
  const auto left = ScalarArgument<double>(context, 0);
  const auto right = ScalarArgument<double>(context, 1);
  if (!left || !right) {
    return HleContextStatus::kInvalidArgument;
  }
  return SetScalarReturn(context, std::atan2(*left, *right));
}

template <typename T>
HleContextStatus WriteGuestScalar(HleCallContext& context,
                                  std::uint64_t address, T value) {
  std::array<std::byte, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(value));
  return context.WriteMemory(address, bytes);
}

template <typename T>
HleContextStatus SinCos(HleCallContext& context) {
  const auto input = ScalarArgument<T>(context, 0);
  if (!input) {
    return HleContextStatus::kInvalidArgument;
  }
  const auto sine_output = context.Argument(0).value_or(0);
  const auto cosine_output = context.Argument(1).value_or(0);
  if ((sine_output != 0 &&
       !context.CanWriteMemory(sine_output, sizeof(T))) ||
      (cosine_output != 0 &&
       !context.CanWriteMemory(cosine_output, sizeof(T)))) {
    return HleContextStatus::kMemoryFault;
  }
  if (sine_output != 0 &&
      WriteGuestScalar(context, sine_output,
                       static_cast<T>(std::sin(*input))) !=
          HleContextStatus::kOk) {
    return HleContextStatus::kMemoryFault;
  }
  if (cosine_output != 0 &&
      WriteGuestScalar(context, cosine_output,
                       static_cast<T>(std::cos(*input))) !=
          HleContextStatus::kOk) {
    return HleContextStatus::kMemoryFault;
  }
  return HleContextStatus::kOk;
}

HleContextStatus Atof(HleCallContext& context) {
  const auto input = context.ReadNullTerminatedString(
      context.Argument(0).value_or(0), kMaximumHleStringBytes);
  if (!input) {
    return input.status;
  }
  const auto* first = input.value.data();
  const auto* const last = first + input.value.size();
  while (first != last &&
         (*first == ' ' || *first == '\t' || *first == '\n' ||
          *first == '\r' || *first == '\f' || *first == '\v')) {
    ++first;
  }
  bool positive_sign = false;
  if (first != last && *first == '+') {
    positive_sign = true;
    ++first;
  }
  double value = 0;
  const auto parsed =
      std::from_chars(first, last, value, std::chars_format::general);
  if (parsed.ec != std::errc{}) {
    value = 0;
  } else if (positive_sign) {
    value = std::abs(value);
  }
  return SetScalarReturn(context, value);
}

HleContextStatus OperatorNew(HleCallContext& context,
                             kernel::LibcHeapService& heap,
                             memory::GuestMemory& memory) {
  const auto allocated = heap.Allocate(
      memory, context.Argument(0).value_or(0));
  context.SetReturn(allocated ? allocated.address : 0);
  return allocated ? HleContextStatus::kOk
                   : HleContextStatus::kResourceLimit;
}

HleContextStatus OperatorDelete(HleCallContext& context,
                                kernel::LibcHeapService& heap,
                                memory::GuestMemory& memory) {
  (void)heap.Release(memory, context.Argument(0).value_or(0));
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus CreateMspace(HleCallContext& context,
                              kernel::LibcHeapService& heap,
                              memory::GuestMemory& memory) {
  const auto name_address = context.Argument(0).value_or(0);
  const auto name = context.ReadNullTerminatedString(name_address, 64);
  if (name_address == 0 || !name ||
      context.Argument(3).value_or(0) != 0) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  const auto created = heap.CreateMspace(
      memory, context.Argument(1).value_or(0),
      context.Argument(2).value_or(0));
  context.SetReturn(created ? created.address : 0);
  return HleContextStatus::kOk;
}

HleContextStatus DestroyMspace(HleCallContext& context,
                               kernel::LibcHeapService& heap,
                               memory::GuestMemory& memory) {
  const auto status = heap.DestroyMspace(
      memory, context.Argument(0).value_or(0));
  context.SetReturn(status == kernel::KernelStatus::kOk ? 0 : 1);
  return HleContextStatus::kOk;
}

HleContextStatus AllocateMspace(HleCallContext& context,
                                kernel::LibcHeapService& heap,
                                memory::GuestMemory& memory,
                                std::uint64_t size,
                                std::uint64_t alignment,
                                bool zero_fill) {
  const auto allocated = heap.AllocateMspace(
      memory, context.Argument(0).value_or(0), size, alignment, zero_fill);
  context.SetReturn(allocated ? allocated.address : 0);
  return HleContextStatus::kOk;
}

HleContextStatus MspaceMalloc(HleCallContext& context,
                              kernel::LibcHeapService& heap,
                              memory::GuestMemory& memory) {
  return AllocateMspace(context, heap, memory,
                        context.Argument(1).value_or(0),
                        kernel::kDefaultLibcHeapAlignment, false);
}

HleContextStatus MspaceFree(HleCallContext& context,
                            kernel::LibcHeapService& heap,
                            memory::GuestMemory& memory) {
  (void)heap.ReleaseMspace(memory, context.Argument(0).value_or(0),
                           context.Argument(1).value_or(0));
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus MspaceCalloc(HleCallContext& context,
                              kernel::LibcHeapService& heap,
                              memory::GuestMemory& memory) {
  const auto count = context.Argument(1).value_or(0);
  const auto element_size = context.Argument(2).value_or(0);
  if (count != 0 &&
      element_size > std::numeric_limits<std::uint64_t>::max() / count) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  return AllocateMspace(context, heap, memory, count * element_size,
                        kernel::kDefaultLibcHeapAlignment, true);
}

HleContextStatus MspaceRealloc(HleCallContext& context,
                               kernel::LibcHeapService& heap,
                               memory::GuestMemory& memory,
                               bool aligned) {
  const auto alignment =
      aligned ? context.Argument(2).value_or(0)
              : kernel::kDefaultLibcHeapAlignment;
  const auto size_argument = aligned ? 3 : 2;
  const auto resized = heap.ReallocateMspace(
      memory, context.Argument(0).value_or(0),
      context.Argument(1).value_or(0),
      context.Argument(size_argument).value_or(0), alignment);
  context.SetReturn(resized ? resized.address : 0);
  return HleContextStatus::kOk;
}

HleContextStatus MspaceMemalign(HleCallContext& context,
                                kernel::LibcHeapService& heap,
                                memory::GuestMemory& memory) {
  const auto alignment = context.Argument(1).value_or(0);
  if (!IsPowerOfTwo(alignment)) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  return AllocateMspace(context, heap, memory,
                        context.Argument(2).value_or(0), alignment, false);
}

HleContextStatus MspacePosixMemalign(HleCallContext& context,
                                     kernel::LibcHeapService& heap,
                                     memory::GuestMemory& memory) {
  const auto handle = context.Argument(0).value_or(0);
  const auto output = context.Argument(1).value_or(0);
  const auto alignment = context.Argument(2).value_or(0);
  const auto size = context.Argument(3).value_or(0);
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
  const auto allocated =
      heap.AllocateMspace(memory, handle, size, alignment, false);
  if (!allocated) {
    (void)context.WriteUInt64(output, 0);
    context.SetReturn(kPosixEnomem);
    return HleContextStatus::kOk;
  }
  if (context.WriteUInt64(output, allocated.address) !=
      HleContextStatus::kOk) {
    (void)heap.ReleaseMspace(memory, handle, allocated.address);
    context.SetReturn(kPosixEinval);
    return HleContextStatus::kMemoryFault;
  }
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

HleContextStatus MspaceIsHeapEmpty(HleCallContext& context,
                                   kernel::LibcHeapService& heap,
                                   memory::GuestMemory& memory) {
  context.SetReturn(
      heap.MspaceIsEmpty(memory, context.Argument(0).value_or(0))
          .value_or(false)
          ? 1
          : 0);
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
  exports.reserve(96);
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
  AddAliases(exports, kLibcMemsetName, kLibcMemsetNid, Memset);
  AddAliases(exports, kLibcMemcpyName, kLibcMemcpyNid, Memcpy);
  AddAliases(exports, kLibcStrcmpName, kLibcStrcmpNid, Strcmp);
  AddAliases(exports, kLibcStrlenName, kLibcStrlenNid, Strlen);
  AddAliases(exports, kLibcWcscmpName, kLibcWcscmpNid, Wcscmp);
  AddAliases(exports, kLibcAtofName, kLibcAtofNid, Atof);
  AddAliases(exports, kLibcExp2fName, kLibcExp2fNid,
             [](HleCallContext& context) {
               return UnaryMath<float>(context, [](float value) {
                 return std::exp2(value);
               });
             });
  AddAliases(exports, kLibcSinfName, kLibcSinfNid,
             [](HleCallContext& context) {
               return UnaryMath<float>(context, [](float value) {
                 return std::sin(value);
               });
             });
  AddAliases(exports, kLibcCosfName, kLibcCosfNid,
             [](HleCallContext& context) {
               return UnaryMath<float>(context, [](float value) {
                 return std::cos(value);
               });
             });
  AddAliases(exports, kLibcAtanName, kLibcAtanNid,
             [](HleCallContext& context) {
               return UnaryMath<double>(context, [](double value) {
                 return std::atan(value);
               });
             });
  AddAliases(exports, kLibcAtan2Name, kLibcAtan2Nid, Atan2);
  AddAliases(exports, kLibcAcosfName, kLibcAcosfNid,
             [](HleCallContext& context) {
               return UnaryMath<float>(context, [](float value) {
                 return std::acos(value);
               });
             });
  AddAliases(exports, kLibcTanfName, kLibcTanfNid,
             [](HleCallContext& context) {
               return UnaryMath<float>(context, [](float value) {
                 return std::tan(value);
               });
             });
  AddAliases(exports, kLibcSincosName, kLibcSincosNid,
             [](HleCallContext& context) {
               return SinCos<double>(context);
             });
  AddAliases(exports, kLibcSincosfName, kLibcSincosfNid,
             [](HleCallContext& context) {
               return SinCos<float>(context);
             });
  AddAliases(exports, kLibcOperatorNewName, kLibcOperatorNewNid,
             [heap_view, memory_view](HleCallContext& context) {
               return OperatorNew(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcOperatorDeleteName, kLibcOperatorDeleteNid,
             [heap_view, memory_view](HleCallContext& context) {
               return OperatorDelete(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcOperatorNewArrayName, kLibcOperatorNewArrayNid,
             [heap_view, memory_view](HleCallContext& context) {
               return OperatorNew(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcOperatorDeleteArrayName,
             kLibcOperatorDeleteArrayNid,
             [heap_view, memory_view](HleCallContext& context) {
               return OperatorDelete(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceCreateName, kLibcMspaceCreateNid,
             [heap_view, memory_view](HleCallContext& context) {
               return CreateMspace(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceDestroyName, kLibcMspaceDestroyNid,
             [heap_view, memory_view](HleCallContext& context) {
               return DestroyMspace(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceMallocName, kLibcMspaceMallocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceMalloc(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceFreeName, kLibcMspaceFreeNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceFree(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceCallocName, kLibcMspaceCallocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceCalloc(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceReallocName, kLibcMspaceReallocNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceRealloc(context, *heap_view, *memory_view,
                                    false);
             });
  AddAliases(exports, kLibcMspaceMemalignName, kLibcMspaceMemalignNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceMemalign(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspacePosixMemalignName,
             kLibcMspacePosixMemalignNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspacePosixMemalign(context, *heap_view,
                                          *memory_view);
             });
  AddAliases(exports, kLibcMspaceReallocalignName,
             kLibcMspaceReallocalignNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceRealloc(context, *heap_view, *memory_view,
                                    true);
             });
  AddAliases(exports, kLibcMspaceMallocUsableSizeName,
             kLibcMspaceMallocUsableSizeNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MallocUsableSize(context, *heap_view, *memory_view);
             });
  AddAliases(exports, kLibcMspaceIsHeapEmptyName,
             kLibcMspaceIsHeapEmptyNid,
             [heap_view, memory_view](HleCallContext& context) {
               return MspaceIsHeapEmpty(context, *heap_view, *memory_view);
             });
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
