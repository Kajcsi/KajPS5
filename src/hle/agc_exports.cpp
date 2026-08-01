// Copyright (C) 2026 KajPS5 contributors
// Architecture and behavior reference: KytyPS5
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/agc_exports.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "gpu/runtime.h"
#include "kernel/event_queue.h"

namespace kajps5::hle {
namespace {

constexpr std::int32_t kGen5ErrorInvalidArgument =
    std::bit_cast<std::int32_t>(0x80020003U);
constexpr std::int32_t kGen5ErrorMemoryFault =
    std::bit_cast<std::int32_t>(0x80020101U);
constexpr std::int32_t kGen5ErrorNotFound =
    std::bit_cast<std::int32_t>(0x80020002U);
constexpr std::uint64_t kMaximumDriverBatchSize = 4096;

void SetSignedResult(HleCallContext& context, std::int32_t value) noexcept {
  context.SetReturn(
      static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

template <typename Handler>
void Add(std::vector<HleExportDefinition>& exports, const char* name,
         const char* nid, Handler handler) {
  exports.push_back({kAgcLibraryName, name, handler});
  exports.push_back({kAgcLibraryName, nid, handler});
}

template <typename Handler>
void AddDriver(std::vector<HleExportDefinition>& exports, const char* name,
               const char* nid, Handler handler) {
  exports.push_back({kAgcDriverLibraryName, name, handler});
  exports.push_back({kAgcDriverLibraryName, nid, handler});
}

HleContextStatus FinishDriverSubmission(
    HleCallContext& context, gpu::GpuRuntime& runtime,
    const gpu::GpuEnqueueResult& queued) {
  if (!queued) {
    if (queued.status == gpu::GpuEnqueueStatus::kMemoryFault) {
      SetSignedResult(context, kGen5ErrorMemoryFault);
      return HleContextStatus::kOk;
    }
    if (queued.status == gpu::GpuEnqueueStatus::kResourceLimit) {
      context.SetReturn(0);
      return HleContextStatus::kResourceLimit;
    }
    SetSignedResult(context, kGen5ErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  const auto drained = runtime.DrainSubmissions();
  context.SetReturn(0);
  if (!drained.first_failure.has_value()) {
    return HleContextStatus::kOk;
  }
  switch (drained.first_failure->status) {
    case gpu::GpuCommandStatus::kMemoryFault:
      return HleContextStatus::kMemoryFault;
    case gpu::GpuCommandStatus::kResourceLimit:
      return HleContextStatus::kResourceLimit;
    case gpu::GpuCommandStatus::kComplete:
    case gpu::GpuCommandStatus::kBlocked:
      return HleContextStatus::kOk;
    case gpu::GpuCommandStatus::kInvalidArgument:
    case gpu::GpuCommandStatus::kMalformedPacket:
    case gpu::GpuCommandStatus::kUnsupportedPacket:
      return HleContextStatus::kFatalGuestError;
  }
  return HleContextStatus::kFatalGuestError;
}

HleContextStatus SubmitDriverCommandBuffer(
    HleCallContext& context, gpu::GpuRuntime& runtime, bool compute) {
  const auto packet_address = context.Argument(compute ? 1 : 0).value_or(0);
  if (packet_address == 0 ||
      packet_address > std::numeric_limits<std::uint64_t>::max() - 12U) {
    SetSignedResult(context, kGen5ErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  std::uint64_t command_address = 0;
  std::uint32_t dword_count = 0;
  std::uint32_t flags = 0;
  if (context.ReadUInt64(packet_address, command_address) !=
          HleContextStatus::kOk ||
      context.ReadUInt32(packet_address + 8U, dword_count) !=
          HleContextStatus::kOk ||
      context.ReadUInt32(packet_address + 12U, flags) !=
          HleContextStatus::kOk) {
    SetSignedResult(context, kGen5ErrorMemoryFault);
    return HleContextStatus::kOk;
  }
  if (command_address == 0 || dword_count == 0 ||
      (!compute && flags != 0)) {
    SetSignedResult(context, kGen5ErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  const auto queued = compute
                          ? runtime.submissions().EnqueueCompute(
                                static_cast<std::uint32_t>(
                                    context.Argument(0).value_or(0)),
                                command_address, dword_count)
                          : runtime.submissions().EnqueueGraphics(
                                command_address, dword_count);
  return FinishDriverSubmission(context, runtime, queued);
}

HleContextStatus SubmitDriverDirectCommandBuffer(
    HleCallContext& context, gpu::GpuRuntime& runtime) {
  const auto command_address = context.Argument(1).value_or(0);
  const auto dword_count_raw = context.Argument(2).value_or(0);
  if (command_address == 0 || dword_count_raw == 0 ||
      dword_count_raw > std::numeric_limits<std::uint32_t>::max()) {
    SetSignedResult(context, kGen5ErrorInvalidArgument);
    return HleContextStatus::kOk;
  }
  const auto queued = runtime.submissions().EnqueueGraphics(
      command_address, static_cast<std::uint32_t>(dword_count_raw));
  return FinishDriverSubmission(context, runtime, queued);
}

HleContextStatus SubmitDriverCommandBufferBatch(
    HleCallContext& context, gpu::GpuRuntime& runtime,
    bool has_owner, bool compute) {
  const std::size_t array_argument = has_owner ? 1 : 0;
  const auto address_array = context.Argument(array_argument).value_or(0);
  const auto size_array = context.Argument(array_argument + 1).value_or(0);
  const auto count_raw = context.Argument(array_argument + 2).value_or(0);
  if (address_array == 0 || size_array == 0 || count_raw == 0 ||
      count_raw > kMaximumDriverBatchSize) {
    SetSignedResult(context, kGen5ErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  const auto count = static_cast<std::size_t>(count_raw);
  const auto last_address_offset = (count - 1U) * sizeof(std::uint64_t);
  const auto last_size_offset = (count - 1U) * sizeof(std::uint32_t);
  if (address_array > std::numeric_limits<std::uint64_t>::max() -
                          last_address_offset ||
      size_array > std::numeric_limits<std::uint64_t>::max() -
                       last_size_offset) {
    SetSignedResult(context, kGen5ErrorInvalidArgument);
    return HleContextStatus::kOk;
  }

  std::vector<gpu::GpuCommandBufferDescriptor> buffers;
  try {
    buffers.reserve(count);
  } catch (...) {
    context.SetReturn(0);
    return HleContextStatus::kResourceLimit;
  }
  for (std::size_t index = 0; index < count; ++index) {
    std::uint64_t command_address = 0;
    std::uint32_t dword_count = 0;
    if (context.ReadUInt64(address_array + index * sizeof(std::uint64_t),
                           command_address) != HleContextStatus::kOk ||
        context.ReadUInt32(size_array + index * sizeof(std::uint32_t),
                           dword_count) != HleContextStatus::kOk) {
      SetSignedResult(context, kGen5ErrorMemoryFault);
      return HleContextStatus::kOk;
    }
    if (command_address == 0 || dword_count == 0) {
      SetSignedResult(context, kGen5ErrorInvalidArgument);
      return HleContextStatus::kOk;
    }
    try {
      buffers.push_back({command_address, dword_count});
    } catch (...) {
      context.SetReturn(0);
      return HleContextStatus::kResourceLimit;
    }
  }

  const auto queued = compute
                          ? runtime.submissions().EnqueueComputeBatch(
                                static_cast<std::uint32_t>(
                                    context.Argument(0).value_or(0)),
                                buffers)
                          : runtime.submissions().EnqueueGraphicsBatch(buffers);
  return FinishDriverSubmission(context, runtime, queued);
}

HleContextStatus ReturnPacket(HleCallContext& context,
                              gpu::GpuPacketResult result) noexcept {
  context.SetReturn(result ? result.address : 0);
  return HleContextStatus::kOk;
}

struct PacketExport {
  const char* name;
  const char* nid;
  gpu::AgcPacketType type;
  std::size_t argument_count;
};

constexpr std::array kPacketExports = {
    PacketExport{"sceAgcDcbSetShRegisterDirect",
                 kAgcDcbSetShRegisterDirectNid,
                 gpu::AgcPacketType::kSetShRegisterDirect, 2},
    PacketExport{"sceAgcDcbSetCxRegisterDirect",
                 kAgcDcbSetCxRegisterDirectNid,
                 gpu::AgcPacketType::kSetCxRegisterDirect, 2},
    PacketExport{"sceAgcDcbSetUcRegisterDirect",
                 kAgcDcbSetUcRegisterDirectNid,
                 gpu::AgcPacketType::kSetUcRegisterDirect, 2},
    PacketExport{"sceAgcDcbSetIndexSize", kAgcDcbSetIndexSizeNid,
                 gpu::AgcPacketType::kSetIndexSize, 3},
    PacketExport{"sceAgcDcbSetIndexBuffer", kAgcDcbSetIndexBufferNid,
                 gpu::AgcPacketType::kSetIndexBuffer, 2},
    PacketExport{"sceAgcDcbSetIndexCount", kAgcDcbSetIndexCountNid,
                 gpu::AgcPacketType::kSetIndexCount, 2},
    PacketExport{"sceAgcDcbSetNumInstances", kAgcDcbSetNumInstancesNid,
                 gpu::AgcPacketType::kSetNumInstances, 2},
    PacketExport{"sceAgcDcbDrawIndex", kAgcDcbDrawIndexNid,
                 gpu::AgcPacketType::kDrawIndex, 4},
    PacketExport{"sceAgcDcbDrawIndexMultiInstanced",
                 kAgcDcbDrawIndexMultiInstancedNid,
                 gpu::AgcPacketType::kDrawIndexMultiInstanced, 6},
    PacketExport{"sceAgcDcbDrawIndexAuto", kAgcDcbDrawIndexAutoNid,
                 gpu::AgcPacketType::kDrawIndexAuto, 3},
    PacketExport{"sceAgcDcbDrawIndexOffset", kAgcDcbDrawIndexOffsetNid,
                 gpu::AgcPacketType::kDrawIndexOffset, 4},
    PacketExport{"sceAgcDcbSetBaseIndirectArgs",
                 kAgcDcbSetBaseIndirectArgsNid,
                 gpu::AgcPacketType::kSetBaseIndirectArgs, 3},
    PacketExport{"sceAgcDcbDispatchIndirect", kAgcDcbDispatchIndirectNid,
                 gpu::AgcPacketType::kDispatchIndirect, 3},
    PacketExport{"sceAgcDcbJump", kAgcDcbJumpNid,
                 gpu::AgcPacketType::kJump, 5},
    PacketExport{"sceAgcDcbRewind", kAgcDcbRewindNid,
                 gpu::AgcPacketType::kRewind, 2},
    PacketExport{"sceAgcDcbSetPredication", kAgcDcbSetPredicationNid,
                 gpu::AgcPacketType::kSetPredication, 6},
    PacketExport{"sceAgcDcbWriteData", kAgcDcbWriteDataNid,
                 gpu::AgcPacketType::kWriteData, 8},
    PacketExport{"sceAgcCbReleaseMem", kAgcCbReleaseMemNid,
                 gpu::AgcPacketType::kReleaseMemory, 12},
    PacketExport{"sceAgcDcbEventWrite", kAgcDcbEventWriteNid,
                 gpu::AgcPacketType::kEventWrite, 3},
    PacketExport{"sceAgcAcbEventWrite", kAgcAcbEventWriteNid,
                 gpu::AgcPacketType::kEventWrite, 3},
    PacketExport{"sceAgcDcbGetLodStats", kAgcDcbGetLodStatsNid,
                 gpu::AgcPacketType::kGetLodStats, 8},
    PacketExport{"sceAgcDcbWaitRegMem", kAgcDcbWaitRegMemNid,
                 gpu::AgcPacketType::kWaitRegMem, 9},
};

struct FixedSizeExport {
  const char* name;
  const char* nid;
  std::uint32_t bytes;
};

constexpr std::array kFixedSizeExports = {
    FixedSizeExport{"sceAgcDcbSetCxRegisterDirectGetSize",
                    kAgcDcbSetCxRegisterDirectGetSizeNid, 12},
    FixedSizeExport{"sceAgcDcbSetNumInstancesGetSize",
                    kAgcDcbSetNumInstancesGetSizeNid, 8},
    FixedSizeExport{"sceAgcDcbDrawIndexGetSize", kAgcDcbDrawIndexGetSizeNid,
                    24},
    FixedSizeExport{"sceAgcDcbDrawIndexMultiInstancedGetSize",
                    kAgcDcbDrawIndexMultiInstancedGetSizeNid, 36},
    FixedSizeExport{"sceAgcDcbDrawIndexAutoGetSize",
                    kAgcDcbDrawIndexAutoGetSizeNid, 12},
    FixedSizeExport{"sceAgcDcbDrawIndexOffsetGetSize",
                    kAgcDcbDrawIndexOffsetGetSizeNid, 20},
    FixedSizeExport{"sceAgcDcbDispatchIndirectGetSize",
                    kAgcDcbDispatchIndirectGetSizeNid, 12},
    FixedSizeExport{"sceAgcDcbJumpGetSize", kAgcDcbJumpGetSizeNid, 16},
    FixedSizeExport{"sceAgcDcbRewindGetSize", kAgcDcbRewindGetSizeNid, 8},
};

}  // namespace

ExportRegistryStatus RegisterAgcExports(ExportRegistry& registry,
                                        gpu::GpuRuntime& gpu_runtime,
                                        kernel::EventQueueService& event_queues) {
  auto* const runtime = &gpu_runtime;
  auto* const queues = &event_queues;
  std::vector<HleExportDefinition> exports;
  exports.reserve(kRegisteredAgcFunctionCount * 2);

  Add(exports, "sceAgcCbNop", kAgcCbNopNid,
      [runtime](HleCallContext& context) {
        return ReturnPacket(
            context, runtime->WriteNop(
                         context.Argument(0).value_or(0),
                         static_cast<std::uint32_t>(
                             context.Argument(1).value_or(0))));
      });
  Add(exports, "sceAgcCbNopGetSize", kAgcCbNopGetSizeNid,
      [](HleCallContext& context) {
        const auto dword_count = static_cast<std::uint32_t>(
            context.Argument(0).value_or(0));
        context.SetReturn(static_cast<std::uint32_t>(dword_count * 4U));
        return HleContextStatus::kOk;
      });
  Add(exports, "sceAgcCbDispatch", kAgcCbDispatchNid,
      [runtime](HleCallContext& context) {
        return ReturnPacket(
            context,
            runtime->WriteDispatch(
                context.Argument(0).value_or(0),
                static_cast<std::uint32_t>(context.Argument(1).value_or(0)),
                static_cast<std::uint32_t>(context.Argument(2).value_or(0)),
                static_cast<std::uint32_t>(context.Argument(3).value_or(0)),
                static_cast<std::uint32_t>(context.Argument(4).value_or(0))));
      });
  Add(exports, "sceAgcCbDispatchGetSize", kAgcCbDispatchGetSizeNid,
      [](HleCallContext& context) {
        context.SetReturn(20);
        return HleContextStatus::kOk;
      });
  Add(exports, "sceAgcGetPacketSize", kAgcGetPacketSizeNid,
      [runtime](HleCallContext& context) {
        const auto result =
            runtime->GetPacketSize(context.Argument(0).value_or(0));
        context.SetReturn(result ? result.dwords : 0);
        return HleContextStatus::kOk;
      });
  Add(exports, "sceAgcSetPacketPredication", kAgcSetPacketPredicationNid,
      [runtime](HleCallContext& context) {
        const auto status = runtime->SetPacketPredication(
            context.Argument(0).value_or(0),
            static_cast<std::uint32_t>(context.Argument(1).value_or(0)));
        if (status == gpu::GpuRuntimeStatus::kOk) {
          context.SetReturn(0);
        } else if (status == gpu::GpuRuntimeStatus::kInvalidArgument) {
          SetSignedResult(context, kGen5ErrorInvalidArgument);
        } else {
          SetSignedResult(context, kGen5ErrorMemoryFault);
        }
        return HleContextStatus::kOk;
      });

  for (const auto& definition : kPacketExports) {
    Add(exports, definition.name, definition.nid,
        [runtime, definition](HleCallContext& context) {
          std::array<std::uint64_t, 12> arguments{};
          for (std::size_t index = 0; index < definition.argument_count;
               ++index) {
            arguments[index] = context.Argument(index).value_or(0);
          }
          return ReturnPacket(
              context,
              runtime->WriteAgcPacket(
                  definition.type,
                  std::span<const std::uint64_t>(arguments.data(),
                                                 definition.argument_count)));
        });
  }
  for (const auto& definition : kFixedSizeExports) {
    Add(exports, definition.name, definition.nid,
        [definition](HleCallContext& context) {
          context.SetReturn(definition.bytes);
          return HleContextStatus::kOk;
        });
  }
  Add(exports, "sceAgcDcbWriteDataGetSize", kAgcDcbWriteDataGetSizeNid,
      [](HleCallContext& context) {
        const auto dword_count = static_cast<std::uint32_t>(
            context.Argument(0).value_or(0));
        context.SetReturn(static_cast<std::uint32_t>(dword_count * 4U + 16U));
        return HleContextStatus::kOk;
      });
  Add(exports, "sceAgcCbQueueEndOfPipeActionGetSize",
      kAgcCbQueueEndOfPipeActionGetSizeNid,
      [](HleCallContext& context) {
        context.SetReturn(32);
        return HleContextStatus::kOk;
      });
  Add(exports, "sceAgcDcbWaitOnAddressGetSize",
      kAgcDcbWaitOnAddressGetSizeNid, [](HleCallContext& context) {
        const auto size = static_cast<std::uint32_t>(
            context.Argument(0).value_or(0));
        context.SetReturn(size == 0 ? 56U : (size == 1 ? 64U : 0U));
        return HleContextStatus::kOk;
      });
  AddDriver(exports, "sceAgcDriverSubmitDcb", kAgcDriverSubmitDcbNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverCommandBuffer(context, *runtime, false);
            });
  AddDriver(exports, "sceAgcDriverSubmitAcb", kAgcDriverSubmitAcbNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverCommandBuffer(context, *runtime, true);
            });
  AddDriver(exports, "sceAgcDriverSubmitMultiDcbs",
            kAgcDriverSubmitMultiDcbsNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverCommandBufferBatch(
                  context, *runtime, false, false);
            });
  AddDriver(exports, "sceAgcDriverAgrSubmitMultiDcbs",
            kAgcDriverAgrSubmitMultiDcbsNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverCommandBufferBatch(
                  context, *runtime, false, false);
            });
  AddDriver(exports, "sceAgcDriverSubmitCommandBuffer",
            kAgcDriverSubmitCommandBufferNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverDirectCommandBuffer(context, *runtime);
            });
  AddDriver(exports, "sceAgcDriverSubmitMultiCommandBuffers",
            kAgcDriverSubmitMultiCommandBuffersNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverCommandBufferBatch(
                  context, *runtime, true, false);
            });
  AddDriver(exports, "sceAgcDriverSubmitMultiAcbs",
            kAgcDriverSubmitMultiAcbsNid,
            [runtime](HleCallContext& context) {
              return SubmitDriverCommandBufferBatch(
                  context, *runtime, true, true);
            });
  AddDriver(exports, "sceAgcDriverAddEqEvent", kAgcDriverAddEqEventNid,
            [queues](HleCallContext& context) {
              const auto status = queues->AddGraphicsEvent(
                  context.Argument(0).value_or(0),
                  context.Argument(1).value_or(0),
                  context.Argument(2).value_or(0));
              SetSignedResult(context, status == kernel::KernelStatus::kOk
                                           ? 0
                                           : kGen5ErrorNotFound);
              return HleContextStatus::kOk;
            });
  AddDriver(exports, "sceAgcDriverDeleteEqEvent",
            kAgcDriverDeleteEqEventNid,
            [queues](HleCallContext& context) {
              const auto status = queues->DeleteGraphicsEvent(
                  context.Argument(0).value_or(0),
                  context.Argument(1).value_or(0));
              SetSignedResult(context, status == kernel::KernelStatus::kOk
                                           ? 0
                                           : kGen5ErrorNotFound);
              return HleContextStatus::kOk;
            });

  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
