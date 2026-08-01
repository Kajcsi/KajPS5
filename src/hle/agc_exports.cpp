// Copyright (C) 2026 KajPS5 contributors
// Architecture and behavior reference: KytyPS5
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/agc_exports.h"

#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

#include "gpu/runtime.h"

namespace kajps5::hle {
namespace {

constexpr std::int32_t kGen5ErrorInvalidArgument =
    std::bit_cast<std::int32_t>(0x80020003U);
constexpr std::int32_t kGen5ErrorMemoryFault =
    std::bit_cast<std::int32_t>(0x80020101U);

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

HleContextStatus ReturnPacket(HleCallContext& context,
                              gpu::GpuPacketResult result) noexcept {
  context.SetReturn(result ? result.address : 0);
  return HleContextStatus::kOk;
}

}  // namespace

ExportRegistryStatus RegisterAgcExports(ExportRegistry& registry,
                                        gpu::GpuRuntime& gpu_runtime) {
  auto* const runtime = &gpu_runtime;
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

  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
