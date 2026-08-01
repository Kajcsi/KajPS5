// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/ampr_exports.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "core/memory/guest_memory.h"
#include "kernel/ampr_command_buffer.h"

namespace kajps5::hle {
namespace {

constexpr std::int32_t kAmprErrorInvalidArgument =
    std::bit_cast<std::int32_t>(0x80020003U);
constexpr std::int32_t kAmprErrorTryAgain =
    std::bit_cast<std::int32_t>(0x80020023U);
constexpr std::int32_t kAmprErrorMemoryFault =
    std::bit_cast<std::int32_t>(0x80020101U);
constexpr std::int16_t kAmprEventFilter = -16;
constexpr std::uint32_t kEventQueueRecordType = 2;
constexpr std::uint32_t kWriteAddressRecordType = 3;

void SetSignedResult(HleCallContext& context, std::int32_t value) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::int32_t ErrorFor(kernel::AmprCommandBufferStatus status) noexcept {
  switch (status) {
    case kernel::AmprCommandBufferStatus::kOk:
      return 0;
    case kernel::AmprCommandBufferStatus::kNoResources:
      return kAmprErrorTryAgain;
    case kernel::AmprCommandBufferStatus::kMemoryFault:
      return kAmprErrorMemoryFault;
    case kernel::AmprCommandBufferStatus::kInvalidArgument:
    case kernel::AmprCommandBufferStatus::kBufferTooSmall:
    case kernel::AmprCommandBufferStatus::kUnsupportedRecord:
      return kAmprErrorInvalidArgument;
  }
  return kAmprErrorInvalidArgument;
}

void Write16(std::span<std::byte> bytes, std::size_t offset,
             std::uint16_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

HleContextStatus CommandBufferConstructor(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  const auto command_buffer = context.Argument(0).value_or(0);
  if (command_buffer == 0) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  const auto status =
      buffers.Construct(memory, command_buffer, context.Argument(1).value_or(0),
                        context.Argument(2).value_or(0));
  if (status != kernel::AmprCommandBufferStatus::kOk) {
    SetSignedResult(context, ErrorFor(status));
  } else {
    context.SetReturn(command_buffer);
  }
  return HleContextStatus::kOk;
}

HleContextStatus AprCommandBufferConstructor(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  const auto command_buffer = context.Argument(0).value_or(0);
  if (command_buffer == 0) {
    context.SetReturn(0);
    return HleContextStatus::kOk;
  }
  const auto status = buffers.Construct(memory, command_buffer, 0, 0,
                                        context.Argument(1).value_or(0),
                                        context.Argument(2).value_or(0), true);
  if (status != kernel::AmprCommandBufferStatus::kOk) {
    SetSignedResult(context, ErrorFor(status));
  } else {
    context.SetReturn(command_buffer);
  }
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferDestructor(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory, bool apr) {
  const auto command_buffer = context.Argument(0).value_or(0);
  const auto status = apr ? buffers.DestroyApr(memory, command_buffer)
                          : buffers.Destroy(memory, command_buffer);
  SetSignedResult(context, ErrorFor(status));
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferSetBuffer(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  const auto status = buffers.SetBuffer(memory, context.Argument(0).value_or(0),
                                        context.Argument(1).value_or(0),
                                        context.Argument(2).value_or(0));
  SetSignedResult(context, ErrorFor(status));
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferReset(HleCallContext& context,
                                    kernel::AmprCommandBufferService& buffers,
                                    memory::GuestMemory& memory) {
  const auto status = buffers.Reset(memory, context.Argument(0).value_or(0));
  SetSignedResult(context, ErrorFor(status));
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferClearBuffer(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  const auto result = buffers.Clear(memory, context.Argument(0).value_or(0));
  if (!result) {
    SetSignedResult(context, ErrorFor(result.status));
  } else {
    context.SetReturn(result.value);
  }
  return HleContextStatus::kOk;
}

HleContextStatus Measure(HleCallContext& context, std::size_t size) noexcept {
  context.SetReturn(size);
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferGet(HleCallContext& context,
                                  kernel::AmprCommandBufferService& buffers,
                                  memory::GuestMemory& memory,
                                  std::size_t field) {
  const auto snapshot =
      buffers.Snapshot(memory, context.Argument(0).value_or(0));
  if (!snapshot) {
    SetSignedResult(context, ErrorFor(snapshot.status));
  } else if (field == 0) {
    context.SetReturn(snapshot.size);
  } else if (field == 1) {
    context.SetReturn(snapshot.write_offset);
  } else {
    context.SetReturn(snapshot.command_count);
  }
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferWriteEventQueue(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  std::array<std::byte, kernel::kAmprEventQueueRecordSize> record{};
  Write32(record, 0x00, kEventQueueRecordType);
  Write16(record, 0x04, std::bit_cast<std::uint16_t>(kAmprEventFilter));
  Write64(record, 0x08, context.Argument(1).value_or(0));
  Write64(record, 0x10, context.Argument(2).value_or(0));
  Write64(record, 0x18, context.Argument(4).value_or(0));
  Write64(record, 0x20, context.Argument(3).value_or(0));
  const auto status =
      buffers.Append(memory, context.Argument(0).value_or(0), record);
  SetSignedResult(context, ErrorFor(status));
  return HleContextStatus::kOk;
}

HleContextStatus CommandBufferWriteAddress(
    HleCallContext& context, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  const auto address = context.Argument(1).value_or(0);
  if (context.Argument(0).value_or(0) == 0 || address == 0) {
    SetSignedResult(context, kAmprErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  std::array<std::byte, kernel::kAmprWriteAddressRecordSize> record{};
  Write32(record, 0x00, kWriteAddressRecordType);
  Write64(record, 0x08, address);
  Write64(record, 0x10, context.Argument(2).value_or(0));
  const auto status =
      buffers.Append(memory, context.Argument(0).value_or(0), record);
  SetSignedResult(context, ErrorFor(status));
  return HleContextStatus::kOk;
}

template <typename Handler>
void Add(std::vector<HleExportDefinition>& exports, const char* name,
         const char* nid, Handler handler) {
  exports.push_back({kAmprLibraryName, name, handler});
  exports.push_back({kAmprLibraryName, nid, handler});
}

}  // namespace

ExportRegistryStatus RegisterAmprExports(
    ExportRegistry& registry, kernel::AmprCommandBufferService& buffers,
    memory::GuestMemory& memory) {
  auto* const buffer_view = &buffers;
  auto* const memory_view = &memory;
  std::vector<HleExportDefinition> exports;
  exports.reserve(kRegisteredAmprFunctionCount * 2);

  Add(exports, "sceAmprCommandBufferConstructor",
      kAmprCommandBufferConstructorNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferConstructor(context, *buffer_view, *memory_view);
      });
  Add(exports, "sceAmprAprCommandBufferConstructor",
      kAmprAprCommandBufferConstructorNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return AprCommandBufferConstructor(context, *buffer_view, *memory_view);
      });
  Add(exports, "sceAmprAprCommandBufferDestructor",
      kAmprAprCommandBufferDestructorNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferDestructor(context, *buffer_view, *memory_view,
                                       true);
      });
  Add(exports, "sceAmprCommandBufferDestructor",
      kAmprCommandBufferDestructorNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferDestructor(context, *buffer_view, *memory_view,
                                       false);
      });
  Add(exports, "sceAmprCommandBufferSetBuffer", kAmprCommandBufferSetBufferNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferSetBuffer(context, *buffer_view, *memory_view);
      });
  Add(exports, "sceAmprCommandBufferReset", kAmprCommandBufferResetNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferReset(context, *buffer_view, *memory_view);
      });
  Add(exports, "sceAmprCommandBufferClearBuffer",
      kAmprCommandBufferClearBufferNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferClearBuffer(context, *buffer_view, *memory_view);
      });
  Add(exports, "sceAmprMeasureCommandSizeReadFile",
      kAmprMeasureCommandSizeReadFileNid, [](HleCallContext& context) {
        return Measure(context, kernel::kAmprReadFileRecordSize);
      });
  Add(exports, "sceAmprMeasureCommandSizeWriteKernelEventQueue_04_00",
      kAmprMeasureCommandSizeWriteEventQueue0400Nid,
      [](HleCallContext& context) {
        return Measure(context, kernel::kAmprEventQueueRecordSize);
      });
  Add(exports, "sceAmprMeasureCommandSizeWriteKernelEventQueueOnCompletion",
      kAmprMeasureCommandSizeWriteEventQueueCompletionNid,
      [](HleCallContext& context) {
        return Measure(context, kernel::kAmprEventQueueRecordSize);
      });
  Add(exports, "sceAmprMeasureCommandSizeWriteAddressOnCompletion",
      kAmprMeasureCommandSizeWriteAddressCompletionNid,
      [](HleCallContext& context) {
        return Measure(context, kernel::kAmprWriteAddressRecordSize);
      });
  Add(exports, "sceAmprMeasureCommandSizeWriteAddress_04_00",
      kAmprMeasureCommandSizeWriteAddress0400Nid, [](HleCallContext& context) {
        return Measure(context, kernel::kAmprWriteAddressRecordSize);
      });
  Add(exports, "sceAmprCommandBufferGetSize", kAmprCommandBufferGetSizeNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferGet(context, *buffer_view, *memory_view, 0);
      });
  Add(exports, "sceAmprCommandBufferGetCurrentOffset",
      kAmprCommandBufferGetCurrentOffsetNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferGet(context, *buffer_view, *memory_view, 1);
      });
  Add(exports, "sceAmprCommandBufferGetNumCommands",
      kAmprCommandBufferGetNumCommandsNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferGet(context, *buffer_view, *memory_view, 2);
      });
  Add(exports, "sceAmprCommandBufferWriteKernelEventQueue_04_00",
      kAmprCommandBufferWriteEventQueue0400Nid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferWriteEventQueue(context, *buffer_view,
                                            *memory_view);
      });
  Add(exports, "sceAmprCommandBufferWriteKernelEventQueueOnCompletion",
      kAmprCommandBufferWriteEventQueueCompletionNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferWriteEventQueue(context, *buffer_view,
                                            *memory_view);
      });
  Add(exports, "sceAmprCommandBufferWriteAddressOnCompletion",
      kAmprCommandBufferWriteAddressCompletionNid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferWriteAddress(context, *buffer_view, *memory_view);
      });
  Add(exports, "sceAmprCommandBufferWriteAddress_04_00",
      kAmprCommandBufferWriteAddress0400Nid,
      [buffer_view, memory_view](HleCallContext& context) {
        return CommandBufferWriteAddress(context, *buffer_view, *memory_view);
      });

  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
