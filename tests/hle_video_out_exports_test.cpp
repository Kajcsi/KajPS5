// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <span>
#include <string_view>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/video_out.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/video_out_exports.h"
#include "kernel/runtime.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "hle_video_out_exports_test: " << message << '\n';
    std::exit(1);
  }
}

void SetArguments(kajps5::hle::HleCallContext& context,
                  std::span<const std::uint64_t> arguments) {
  constexpr std::array registers = {
      kajps5::hle::HleRegister::kRdi, kajps5::hle::HleRegister::kRsi,
      kajps5::hle::HleRegister::kRdx, kajps5::hle::HleRegister::kRcx,
      kajps5::hle::HleRegister::kR8,  kajps5::hle::HleRegister::kR9};
  for (std::size_t index = 0; index < registers.size(); ++index) {
    Check(context.SetRegister(registers[index],
                              index < arguments.size() ? arguments[index] : 0),
          "argument register setup failed");
  }
  Check(context.SetCapturedStackArguments(
            arguments.size() > registers.size()
                ? arguments.subspan(registers.size())
                : std::span<const std::uint64_t>{}),
        "stack argument setup failed");
}

void SetArguments(kajps5::hle::HleCallContext& context,
                  std::initializer_list<std::uint64_t> arguments) {
  SetArguments(context, std::span<const std::uint64_t>(arguments));
}

std::int32_t Result(const kajps5::hle::HleCallContext& context) {
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(
      context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0)));
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::uint32_t Read32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

}  // namespace

int main() {
  using kajps5::gpu::GpuRuntime;
  using kajps5::gpu::VideoOutService;
  using kajps5::gpu::vulkan::VulkanPresentationResult;
  using kajps5::gpu::vulkan::VulkanPresentationStatus;
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x500000;
  constexpr std::uint64_t kAttribute = kBase + 0x100;
  constexpr std::uint64_t kAttributeRaw = kBase + 0x180;
  constexpr std::uint64_t kAttribute10 = kBase + 0x280;
  constexpr std::uint64_t kBuffers = kBase + 0x200;
  constexpr std::uint64_t kStatus = kBase + 0x300;
  constexpr std::uint64_t kLegacyAttribute = kBase + 0x400;
  constexpr std::uint64_t kLegacyAttributeShort = kBase + 0x440;
  GuestMemory memory(kBase, 0x2000,
                     GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  kajps5::kernel::KernelRuntime kernel_runtime;
  GpuRuntime runtime(memory, nullptr, &kernel_runtime.event_queues());
  VideoOutService service(memory, runtime, kernel_runtime.event_queues());
  ExportRegistry registry;
  Check(kajps5::hle::RegisterVideoOutExports(registry, service) ==
                ExportRegistryStatus::kOk &&
            registry.size() == kajps5::hle::kRegisteredVideoOutFunctionCount * 2,
        "VideoOut registration count is incorrect");
  const auto queue = kernel_runtime.event_queues().Create("videoout");
  Check(static_cast<bool>(queue), "event queue setup failed");
  HleCallContext context(memory);

  SetArguments(context, {0, 0, 0, 0});
  Check(registry.Dispatch("sceVideoOutOpen", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == 1,
        "Open by name failed");

  SetArguments(context,
               {kLegacyAttribute, 0x80000000ULL, 1ULL, 2ULL, 640ULL,
                480ULL, 672ULL});
  Check(registry.Dispatch("sceVideoOutSetBufferAttribute", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0,
        "legacy SetBufferAttribute failed");
  std::array<std::byte, 0x28> legacy_attribute{};
  Check(memory.Read(kLegacyAttribute, legacy_attribute) &&
            Read32(legacy_attribute, 0x00) == 0x80000000U &&
            Read32(legacy_attribute, 0x04) == 1 &&
            Read32(legacy_attribute, 0x08) == 2 &&
            Read32(legacy_attribute, 0x0c) == 640 &&
            Read32(legacy_attribute, 0x10) == 480 &&
            Read32(legacy_attribute, 0x14) == 672 &&
            std::all_of(legacy_attribute.begin() + 0x18,
                        legacy_attribute.end(),
                        [](std::byte value) { return value == std::byte{0}; }),
        "legacy SetBufferAttribute did not write the exact 0x28 guest layout");
  std::array<std::byte, 0x28> legacy_attribute_short{};
  std::fill(legacy_attribute_short.begin(), legacy_attribute_short.end(),
            std::byte{0xa5});
  Check(memory.Write(kLegacyAttributeShort, legacy_attribute_short) &&
            memory.Protect(kLegacyAttributeShort + 0x20, 8,
                           GuestMemoryProtection::kRead),
        "legacy short output fixture setup failed");
  SetArguments(context,
               {kLegacyAttributeShort, 0x80000000ULL, 1ULL, 2ULL, 640ULL,
                480ULL, 672ULL});
  Check(registry.Dispatch("sceVideoOutSetBufferAttribute", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorInvalidAddress &&
            memory.Read(kLegacyAttributeShort, legacy_attribute) &&
            legacy_attribute == legacy_attribute_short,
        "legacy SetBufferAttribute accepted 0x20 writable bytes or partially wrote output");

  constexpr std::array<std::uint64_t, 8> set_attribute = {
      kAttribute, 0x8000000022000000ULL, 1ULL, 4ULL, 3ULL, 0ULL, 0ULL, 0ULL};
  SetArguments(context, set_attribute);
  Check(registry.Dispatch(kajps5::hle::kVideoOutSetBufferAttribute2Nid, context)
                    .handler_status == HleContextStatus::kOk &&
            Result(context) == 0,
        "SetBufferAttribute2 by NID failed");
  std::array<std::byte, 0x50> attribute{};
  Check(memory.Read(kAttribute, attribute) && Read32(attribute, 0x04) == 1 &&
            Read32(attribute, 0x0c) == 4 && Read32(attribute, 0x10) == 3 &&
            Read32(attribute, 0x14) == 0 &&
            Read64(attribute, 0x20) == 0x8000000022000000ULL &&
            Read64(attribute, 0x28) == 0 && Read32(attribute, 0x30) == 0 &&
            std::all_of(attribute.begin() + 0x34, attribute.end(),
                        [](std::byte value) { return value == std::byte{0}; }),
        "SetBufferAttribute2 did not write the exact 0x50 guest layout");

  std::array<std::byte, 0x20> buffer{};
  Write64(buffer, 0x00, kBase + 0x800);
  Write64(buffer, 0x08, kBase + 0xc00);
  Check(memory.Write(kBuffers, buffer), "buffer entry setup failed");
  constexpr std::array<std::uint64_t, 8> register_buffers = {
      1ULL, 0ULL, 0ULL, kBuffers, 1ULL, kAttribute, 0ULL, 0ULL};
  SetArguments(context, register_buffers);
  Check(registry.Dispatch("sceVideoOutRegisterBuffers2", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == 0,
        "RegisterBuffers2 failed");
  SetArguments(context, {1, 0, 0, kBuffers, 1, kAttribute, 0, 0});
  Check(registry.Dispatch("sceVideoOutRegisterBuffers2", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorResourceBusy,
        "overlapping group did not return resource busy");
  SetArguments(context, {1, 0, 16, kBuffers, 1, kAttribute, 0, 0});
  Check(registry.Dispatch("sceVideoOutRegisterBuffers2", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorInvalidValue,
        "out-of-range buffer registration did not fail precisely");

  SetArguments(context, {queue.handle, 1, 0x12345678ULL});
  Check(registry.Dispatch("sceVideoOutAddFlipEvent", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0,
        "AddFlipEvent failed");
  Check(memory.Protect(kBase + 0x800, 48,
                       GuestMemoryProtection::kRead |
                           GuestMemoryProtection::kWrite |
                           GuestMemoryProtection::kGpuRead),
        "guest frame protection setup failed");
  SetArguments(context, {1, 0, 1, 0x55});
  Check(registry.Dispatch("sceVideoOutSubmitFlip", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0 &&
            service.last_presentation_result().status ==
                VulkanPresentationStatus::kContextUnavailable,
        "headless SubmitFlip did not retain context-unavailable presentation state");
  auto event = kernel_runtime.event_queues().Poll(queue.handle, 1);
  Check(event && event.events.size() == 1 && event.events[0].ident == 0x6 &&
            event.events[0].filter == kajps5::kernel::kEventFilterVideoOut &&
            event.events[0].data == 0x55 && event.events[0].user_data == 0x12345678ULL,
        "headless flip did not trigger the generation-aware VideoOut event");
  SetArguments(context, {1, kStatus});
  Check(registry.Dispatch("sceVideoOutGetFlipStatus", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0,
        "GetFlipStatus failed");
  std::array<std::byte, 0x80> flip_status{};
  Check(memory.Read(kStatus, flip_status) && Read64(flip_status, 0x00) == 1 &&
            std::bit_cast<std::int64_t>(Read64(flip_status, 0x18)) == 0x55 &&
            Read32(flip_status, 0x34) == 0 && Read32(flip_status, 0x38) == 0 &&
            Read64(flip_status, 0x40) == 0,
        "headless flip status did not report deterministic completion");

  struct FlipState {
    std::uint64_t count;
    std::int64_t flip_arg;
    std::int32_t pending_count;
    std::int32_t current_buffer;
    std::uint64_t timeline;
  };
  const auto SnapshotFlipState = [&] {
    SetArguments(context, {1, kStatus});
    Check(registry.Dispatch("sceVideoOutGetFlipStatus", context).handler_status ==
                  HleContextStatus::kOk && Result(context) == 0 &&
              memory.Read(kStatus, flip_status),
          "GetFlipStatus snapshot failed");
    return FlipState{Read64(flip_status, 0x00),
                     std::bit_cast<std::int64_t>(Read64(flip_status, 0x18)),
                     std::bit_cast<std::int32_t>(Read32(flip_status, 0x34)),
                     std::bit_cast<std::int32_t>(Read32(flip_status, 0x38)),
                     Read64(flip_status, 0x40)};
  };
  const auto accepted_state = SnapshotFlipState();
  const auto accepted_presentation = service.last_presentation_result();
  const auto CheckRejectedFlip = [&](std::string_view message) {
    const auto actual_state = SnapshotFlipState();
    const auto actual_presentation = service.last_presentation_result();
    const auto queue_status = kernel_runtime.event_queues().Poll(queue.handle, 1).status;
    const auto same_presentation =
        actual_presentation.status == accepted_presentation.status &&
        actual_presentation.timeline == accepted_presentation.timeline &&
        actual_presentation.diagnostics.size() ==
            accepted_presentation.diagnostics.size() &&
        std::equal(actual_presentation.diagnostics.begin(),
                   actual_presentation.diagnostics.end(),
                   accepted_presentation.diagnostics.begin(),
                   [](const auto& actual, const auto& expected) {
                     return actual.status == expected.status &&
                            actual.api_result == expected.api_result &&
                            actual.message == expected.message;
                   });
    const auto unchanged = actual_state.count == accepted_state.count &&
                           actual_state.flip_arg == accepted_state.flip_arg &&
                           actual_state.pending_count == accepted_state.pending_count &&
                           actual_state.current_buffer == accepted_state.current_buffer &&
                           actual_state.timeline == accepted_state.timeline &&
                           same_presentation &&
                           queue_status == kajps5::kernel::KernelStatus::kBusy;
    Check(unchanged, message);
  };

  Check(memory.Protect(kBase + 0x800, 48,
                       GuestMemoryProtection::kRead |
                           GuestMemoryProtection::kWrite),
        "guest frame protection removal failed");
  SetArguments(context, {1, 0, 1, 0x44});
  Check(registry.Dispatch("sceVideoOutSubmitFlip", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorInvalidAddress,
        "missing guest GPU-read protection did not reject the flip");
  CheckRejectedFlip("missing guest GPU-read protection changed flip state or fired an event");
  Check(memory.Protect(kBase + 0x800, 48,
                       GuestMemoryProtection::kRead |
                           GuestMemoryProtection::kWrite |
                           GuestMemoryProtection::kGpuRead),
        "guest frame protection restore failed");

  std::array<std::byte, 0x20> invalid_range_buffer{};
  Write64(invalid_range_buffer, 0x00, kBase + 0x1ff0);
  Check(memory.Write(kBuffers + 0x20, invalid_range_buffer),
        "invalid-range buffer entry setup failed");
  SetArguments(context, {1, 1, 1, kBuffers + 0x20, 1, kAttribute, 0, 0});
  Check(registry.Dispatch("sceVideoOutRegisterBuffers2", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == 1,
        "invalid-range fixture registration failed");
  SetArguments(context, {1, 1, 1, 0x57});
  Check(registry.Dispatch("sceVideoOutSubmitFlip", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorInvalidAddress,
        "unmapped complete frame range did not reject the flip");
  CheckRejectedFlip("unmapped complete frame range changed flip state or fired an event");

  SetArguments(context, {kAttributeRaw, 56ULL, 1ULL, 4ULL, 3ULL, 0ULL, 0ULL, 0ULL});
  Check(registry.Dispatch("sceVideoOutSetBufferAttribute2", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0,
        "raw-format attribute setup failed");
  std::array<std::byte, 0x20> raw_buffer{};
  Write64(raw_buffer, 0x00, kBase + 0x900);
  Check(memory.Write(kBuffers + 0x40, raw_buffer) &&
            memory.Protect(kBase + 0x900, 48,
                           GuestMemoryProtection::kRead |
                               GuestMemoryProtection::kWrite |
                               GuestMemoryProtection::kGpuRead),
        "raw-format buffer protection setup failed");
  SetArguments(context, {1, 2, 2, kBuffers + 0x40, 1, kAttributeRaw, 0, 0});
  Check(registry.Dispatch("sceVideoOutRegisterBuffers2", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 2,
        "raw-format fixture registration failed");
  SetArguments(context, {1, 2, 1, 0x58});
  Check(registry.Dispatch("sceVideoOutSubmitFlip", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorInvalidValue,
        "raw internal format alias was accepted");
  CheckRejectedFlip("raw internal format alias changed flip state or fired an event");

  service.SetPresentationCallbacksForTesting({
      [](const kajps5::gpu::GuestImageLayoutInput&) {
        return VulkanPresentationResult{VulkanPresentationStatus::kDeviceFailure, 73, {}};
      },
      {}});
  SetArguments(context, {1, 0, 1, 0x59});
  Check(registry.Dispatch("sceVideoOutSubmitFlip", context).handler_status ==
                HleContextStatus::kOk &&
            Result(context) == kajps5::gpu::kVideoOutErrorResourceBusy,
        "terminal presentation failure did not reject the flip");
  CheckRejectedFlip("terminal presentation failure changed flip state or fired an event");

  kajps5::gpu::GuestImageLayoutInput submitted{};
  std::size_t present_calls = 0;
  std::size_t poll_calls = 0;
  service.SetPresentationCallbacksForTesting({
      [&submitted, &present_calls](const kajps5::gpu::GuestImageLayoutInput& input) {
        submitted = input;
        ++present_calls;
        return VulkanPresentationResult{VulkanPresentationStatus::kRetainedWorkPending,
                                        77, {}};
      },
      [&poll_calls] {
        ++poll_calls;
        return VulkanPresentationResult{VulkanPresentationStatus::kOk, 78, {}};
      }});
  SetArguments(context, {1, 0, 4, 0x66});
  Check(registry.Dispatch("sceVideoOutSubmitFlip", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0 &&
            present_calls == 1 && submitted.guest_address == kBase + 0x800 &&
            submitted.format == 56 && submitted.width == 4 && submitted.height == 3 &&
            submitted.row_pitch_bytes == 16 &&
            submitted.tile_mode == kajps5::gpu::Prospero::TileMode::kLinear,
        "SubmitFlip did not issue exactly one frame with the registered layout");
  SetArguments(context, {1});
  Check(registry.Dispatch("sceVideoOutIsFlipPending", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0 && poll_calls == 1,
        "retained flip did not complete on the next service poll");

  SetArguments(context, {kAttribute10, 0x8100000022000000ULL, 1ULL,
                         4ULL, 3ULL, 0ULL, 0ULL, 0ULL});
  Check(registry.Dispatch("sceVideoOutSetBufferAttribute2", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0,
        "10-bit attribute setup failed");
  std::array<std::byte, 0x20> ten_bit_buffer{};
  Write64(ten_bit_buffer, 0x00, kBase + 0xa00);
  Check(memory.Write(kBuffers + 0x60, ten_bit_buffer) &&
            memory.Protect(kBase + 0xa00, 48,
                           GuestMemoryProtection::kRead |
                               GuestMemoryProtection::kWrite |
                               GuestMemoryProtection::kGpuRead),
        "10-bit buffer protection setup failed");
  SetArguments(context, {1, 3, 3, kBuffers + 0x60, 1, kAttribute10, 0, 0});
  Check(registry.Dispatch("sceVideoOutRegisterBuffers2", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 3,
        "10-bit fixture registration failed");
  service.SetPresentationCallbacksForTesting({
      [&submitted, &present_calls](const kajps5::gpu::GuestImageLayoutInput& input) {
        submitted = input;
        ++present_calls;
        return VulkanPresentationResult{VulkanPresentationStatus::kOk, 79, {}};
      },
      {}});
  SetArguments(context, {1, 3, 1, 0x67});
  const auto ten_bit_dispatch =
      registry.Dispatch("sceVideoOutSubmitFlip", context);
  Check(ten_bit_dispatch.handler_status == HleContextStatus::kOk && Result(context) == 0 &&
            present_calls == 2 && submitted.format == 9 &&
            submitted.tile_mode == kajps5::gpu::Prospero::TileMode::kLinear,
        "exact 64-bit 10-bit VideoOut format did not reach format 9");

  SetArguments(context, {queue.handle, 1});
  Check(registry.Dispatch("sceVideoOutDeleteFlipEvent", context).handler_status ==
                HleContextStatus::kOk && Result(context) == 0,
        "DeleteFlipEvent failed");
  return 0;
}
