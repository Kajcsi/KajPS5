// Copyright (C) 2026 KajPS5 contributors
// Architecture adapted from KytyPS5 src/libs/libVideoOut.cpp and
// src/graphics/presentation/videoOut.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203. Behavior adapted from SharpEmu
// src/SharpEmu.Libs/VideoOut/VideoOutExports.cs at
// 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#include "hle/video_out_exports.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "gpu/video_out.h"
#include "hle/call_context.h"

namespace kajps5::hle {
namespace {

constexpr std::size_t kBufferAttributeSize = 0x28;
constexpr std::size_t kBufferAttribute2Size = 0x50;
constexpr std::size_t kBufferEntry2Size = 0x20;
constexpr std::size_t kFlipStatusSize = 0x80;

void SetResult(HleCallContext& context, std::int32_t result) noexcept {
  context.SetReturn(static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
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

std::uint32_t Read32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

template <typename Handler>
void Add(std::vector<HleExportDefinition>& exports, const char* name,
         const char* nid, Handler handler) {
  exports.push_back({kVideoOutLibraryName, name, handler});
  exports.push_back({kVideoOutLibraryName, nid, handler});
}

bool ReadAttribute(HleCallContext& context, std::uint64_t address, bool modern,
                   gpu::VideoOutBufferAttribute& attribute) {
  const auto size = modern ? kBufferAttribute2Size : kBufferAttributeSize;
  if (address == 0 || !context.CanReadMemory(address, size)) {
    return false;
  }
  std::array<std::byte, kBufferAttribute2Size> bytes{};
  if (context.ReadMemory(address, std::span(bytes).first(size)) !=
      HleContextStatus::kOk) {
    return false;
  }
  if (modern) {
    attribute.tiling_mode = Read32(bytes, 0x04);
    attribute.aspect_ratio = Read32(bytes, 0x08);
    attribute.width = Read32(bytes, 0x0c);
    attribute.height = Read32(bytes, 0x10);
    attribute.pitch_in_pixels = Read32(bytes, 0x14);
    attribute.option = Read64(bytes, 0x18);
    attribute.pixel_format = Read64(bytes, 0x20);
    attribute.dcc_clear = Read64(bytes, 0x28);
    attribute.dcc_control = Read32(bytes, 0x30);
  } else {
    attribute.pixel_format = Read32(bytes, 0x00);
    attribute.tiling_mode = Read32(bytes, 0x04);
    attribute.aspect_ratio = Read32(bytes, 0x08);
    attribute.width = Read32(bytes, 0x0c);
    attribute.height = Read32(bytes, 0x10);
    attribute.pitch_in_pixels = Read32(bytes, 0x14);
    attribute.option = Read32(bytes, 0x18);
  }
  return true;
}

HleContextStatus SetBufferAttribute(HleCallContext& context, bool modern) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    SetResult(context, gpu::kVideoOutErrorInvalidAddress);
    return HleContextStatus::kOk;
  }
  const auto size = modern ? kBufferAttribute2Size : kBufferAttributeSize;
  if (!context.CanWriteMemory(address, size)) {
    SetResult(context, gpu::kVideoOutErrorInvalidAddress);
    return HleContextStatus::kOk;
  }
  std::array<std::byte, kBufferAttribute2Size> bytes{};
  if (modern) {
    Write32(bytes, 0x04, static_cast<std::uint32_t>(context.Argument(2).value_or(0)));
    Write32(bytes, 0x0c, static_cast<std::uint32_t>(context.Argument(3).value_or(0)));
    Write32(bytes, 0x10, static_cast<std::uint32_t>(context.Argument(4).value_or(0)));
    Write64(bytes, 0x18, context.Argument(5).value_or(0));
    Write64(bytes, 0x20, context.Argument(1).value_or(0));
    Write32(bytes, 0x30, static_cast<std::uint32_t>(context.Argument(6).value_or(0)));
    Write64(bytes, 0x28, context.Argument(7).value_or(0));
  } else {
    Write32(bytes, 0x00, static_cast<std::uint32_t>(context.Argument(1).value_or(0)));
    Write32(bytes, 0x04, static_cast<std::uint32_t>(context.Argument(2).value_or(0)));
    Write32(bytes, 0x08, static_cast<std::uint32_t>(context.Argument(3).value_or(0)));
    Write32(bytes, 0x0c, static_cast<std::uint32_t>(context.Argument(4).value_or(0)));
    Write32(bytes, 0x10, static_cast<std::uint32_t>(context.Argument(5).value_or(0)));
    Write32(bytes, 0x14, static_cast<std::uint32_t>(context.Argument(6).value_or(0)));
  }
  SetResult(context, context.WriteMemory(address, std::span(bytes).first(size)) ==
                         HleContextStatus::kOk
                     ? 0
                     : gpu::kVideoOutErrorInvalidAddress);
  return HleContextStatus::kOk;
}

HleContextStatus RegisterBuffers(HleCallContext& context,
                                 gpu::VideoOutService& service, bool modern) {
  const auto handle = static_cast<std::int32_t>(context.Argument(0).value_or(0));
  const auto group = modern ? static_cast<std::int32_t>(context.Argument(1).value_or(0)) : 0;
  const auto start = static_cast<std::int32_t>(context.Argument(modern ? 2 : 1).value_or(0));
  const auto entries_address = context.Argument(modern ? 3 : 2).value_or(0);
  const auto count = static_cast<std::int32_t>(context.Argument(modern ? 4 : 3).value_or(0));
  const auto attribute_address = context.Argument(modern ? 5 : 4).value_or(0);
  if (entries_address == 0) {
    SetResult(context, gpu::kVideoOutErrorInvalidAddress);
    return HleContextStatus::kOk;
  }
  if (attribute_address == 0) {
    SetResult(context, gpu::kVideoOutErrorInvalidOption);
    return HleContextStatus::kOk;
  }
  if (count < 1 || count > static_cast<std::int32_t>(gpu::kVideoOutMaximumBuffers)) {
    SetResult(context, gpu::kVideoOutErrorInvalidValue);
    return HleContextStatus::kOk;
  }
  gpu::VideoOutBufferAttribute attribute;
  if (!ReadAttribute(context, attribute_address, modern, attribute)) {
    SetResult(context, gpu::kVideoOutErrorInvalidAddress);
    return HleContextStatus::kOk;
  }
  std::array<gpu::VideoOutBufferEntry, gpu::kVideoOutMaximumBuffers> entries{};
  for (std::int32_t index = 0; index < count; ++index) {
    const auto stride = modern ? kBufferEntry2Size : sizeof(std::uint64_t);
    if (static_cast<std::uint64_t>(index) >
        (std::numeric_limits<std::uint64_t>::max() - entries_address) / stride ||
        context.ReadUInt64(entries_address + static_cast<std::uint64_t>(index) * stride,
                           entries[index].data_address) != HleContextStatus::kOk ||
        modern && context.ReadUInt64(entries_address + static_cast<std::uint64_t>(index) * stride + 8,
                                     entries[index].metadata_address) != HleContextStatus::kOk) {
      SetResult(context, gpu::kVideoOutErrorInvalidAddress);
      return HleContextStatus::kOk;
    }
  }
  std::int32_t result = 0;
  if (modern) {
    result = service.RegisterBuffers2(
        handle, group, start, std::span(entries).first(count), attribute,
        static_cast<std::uint32_t>(context.Argument(6).value_or(0)),
        context.Argument(7).value_or(0));
  } else {
    std::array<std::uint64_t, gpu::kVideoOutMaximumBuffers> addresses{};
    for (std::int32_t index = 0; index < count; ++index) {
      addresses[index] = entries[index].data_address;
    }
    result = service.RegisterBuffers(handle, start,
                                     std::span(addresses).first(count),
                                     attribute);
  }
  SetResult(context, result);
  return HleContextStatus::kOk;
}

}  // namespace

ExportRegistryStatus RegisterVideoOutExports(ExportRegistry& registry,
                                             gpu::VideoOutService& video_out) {
  auto* const service = &video_out;
  std::vector<HleExportDefinition> exports;
  exports.reserve(kRegisteredVideoOutFunctionCount * 2);
  Add(exports, "sceVideoOutOpen", kVideoOutOpenNid,
      [service](HleCallContext& context) {
        SetResult(context, service->Open(
            static_cast<std::int32_t>(context.Argument(0).value_or(0)),
            static_cast<std::int32_t>(context.Argument(1).value_or(0)),
            static_cast<std::int32_t>(context.Argument(2).value_or(0))));
        return HleContextStatus::kOk;
      });
  Add(exports, "sceVideoOutClose", kVideoOutCloseNid,
      [service](HleCallContext& context) { SetResult(context, service->Close(static_cast<std::int32_t>(context.Argument(0).value_or(0)))); return HleContextStatus::kOk; });
  Add(exports, "sceVideoOutSetFlipRate", kVideoOutSetFlipRateNid,
      [service](HleCallContext& context) { SetResult(context, service->SetFlipRate(static_cast<std::int32_t>(context.Argument(0).value_or(0)), static_cast<std::int32_t>(context.Argument(1).value_or(0)))); return HleContextStatus::kOk; });
  Add(exports, "sceVideoOutSetBufferAttribute", kVideoOutSetBufferAttributeNid,
      [](HleCallContext& context) { return SetBufferAttribute(context, false); });
  Add(exports, "sceVideoOutSetBufferAttribute2", kVideoOutSetBufferAttribute2Nid,
      [](HleCallContext& context) { return SetBufferAttribute(context, true); });
  Add(exports, "sceVideoOutRegisterBuffers", kVideoOutRegisterBuffersNid,
      [service](HleCallContext& context) { return RegisterBuffers(context, *service, false); });
  Add(exports, "sceVideoOutRegisterBuffers2", kVideoOutRegisterBuffers2Nid,
      [service](HleCallContext& context) { return RegisterBuffers(context, *service, true); });
  Add(exports, "sceVideoOutUnregisterBuffers", kVideoOutUnregisterBuffersNid,
      [service](HleCallContext& context) { SetResult(context, service->UnregisterBuffers(static_cast<std::int32_t>(context.Argument(0).value_or(0)), static_cast<std::int32_t>(context.Argument(1).value_or(0)))); return HleContextStatus::kOk; });
  Add(exports, "sceVideoOutAddFlipEvent", kVideoOutAddFlipEventNid,
      [service](HleCallContext& context) { SetResult(context, service->AddFlipEvent(context.Argument(0).value_or(0), static_cast<std::int32_t>(context.Argument(1).value_or(0)), context.Argument(2).value_or(0))); return HleContextStatus::kOk; });
  Add(exports, "sceVideoOutDeleteFlipEvent", kVideoOutDeleteFlipEventNid,
      [service](HleCallContext& context) { SetResult(context, service->DeleteFlipEvent(context.Argument(0).value_or(0), static_cast<std::int32_t>(context.Argument(1).value_or(0)))); return HleContextStatus::kOk; });
  Add(exports, "sceVideoOutSubmitFlip", kVideoOutSubmitFlipNid,
      [service](HleCallContext& context) { SetResult(context, service->SubmitFlip(static_cast<std::int32_t>(context.Argument(0).value_or(0)), static_cast<std::int32_t>(context.Argument(1).value_or(0)), static_cast<std::int32_t>(context.Argument(2).value_or(0)), std::bit_cast<std::int64_t>(context.Argument(3).value_or(0)))); return HleContextStatus::kOk; });
  Add(exports, "sceVideoOutGetFlipStatus", kVideoOutGetFlipStatusNid,
      [service](HleCallContext& context) {
        const auto address = context.Argument(1).value_or(0);
        if (address == 0 || !context.CanWriteMemory(address, kFlipStatusSize)) { SetResult(context, gpu::kVideoOutErrorInvalidAddress); return HleContextStatus::kOk; }
        gpu::VideoOutFlipStatus status;
        const auto result = service->GetFlipStatus(static_cast<std::int32_t>(context.Argument(0).value_or(0)), status);
        if (result != 0) { SetResult(context, result); return HleContextStatus::kOk; }
        std::array<std::byte, kFlipStatusSize> bytes{};
        Write64(bytes, 0x00, status.count);
        Write64(bytes, 0x18, std::bit_cast<std::uint64_t>(status.flip_arg));
        Write32(bytes, 0x30, 0);
        Write32(bytes, 0x34, static_cast<std::uint32_t>(status.pending_count));
        Write32(bytes, 0x38, static_cast<std::uint32_t>(status.current_buffer));
        Write64(bytes, 0x40, status.timeline);
        SetResult(context, context.WriteMemory(address, bytes) == HleContextStatus::kOk ? 0 : gpu::kVideoOutErrorInvalidAddress);
        return HleContextStatus::kOk;
      });
  Add(exports, "sceVideoOutIsFlipPending", kVideoOutIsFlipPendingNid,
      [service](HleCallContext& context) { SetResult(context, service->IsFlipPending(static_cast<std::int32_t>(context.Argument(0).value_or(0)))); return HleContextStatus::kOk; });
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
