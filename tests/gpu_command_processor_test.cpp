// Copyright (C) 2026 KajPS5 contributors
// Architecture and packet reference: KytyPS5
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/command_processor.h"
#include "gpu/runtime.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_command_processor_test: " << message << '\n';
    std::exit(1);
  }
}

std::uint32_t Pm4(std::uint32_t dwords, std::uint32_t opcode,
                  std::uint32_t packet_register = 0) {
  return 0xc0000000U | (((dwords - 2U) & 0x3fffU) << 16U) |
         ((opcode & 0xffU) << 8U) | ((packet_register & 0x3fU) << 2U);
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::uint32_t Read32(std::span<const std::byte> bytes,
                     std::size_t offset = 0) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes,
                     std::size_t offset = 0) {
  return static_cast<std::uint64_t>(Read32(bytes, offset)) |
         (static_cast<std::uint64_t>(Read32(bytes, offset + 4U)) << 32U);
}

void WriteDwords(kajps5::memory::GuestMemory& memory, std::uint64_t address,
                 std::span<const std::uint32_t> words) {
  std::vector<std::byte> bytes(words.size() * 4U);
  for (std::size_t index = 0; index < words.size(); ++index) {
    Write32(bytes, index * 4U, words[index]);
  }
  Check(memory.Write(address, bytes), "guest command write failed");
}

void WriteValue32(kajps5::memory::GuestMemory& memory,
                  std::uint64_t address, std::uint32_t value) {
  std::array<std::byte, 4> bytes{};
  Write32(bytes, 0, value);
  Check(memory.Write(address, bytes), "guest value write failed");
}

}  // namespace

int main() {
  using kajps5::gpu::GpuActionTrace;
  using kajps5::gpu::GpuActionRing;
  using kajps5::gpu::GpuActionType;
  using kajps5::gpu::GpuCommandStatus;
  using kajps5::gpu::GpuMemorySubmissionSink;
  using kajps5::gpu::GpuRegisterSpace;
  using kajps5::gpu::GpuRuntime;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x600000;
  constexpr std::uint64_t kMain = kBase + 0x1000;
  constexpr std::uint64_t kNested = kBase + 0x2000;
  constexpr std::uint64_t kRegisterTable = kBase + 0x3000;
  constexpr std::uint64_t kWaitLabel = kBase + 0x4000;
  constexpr std::uint64_t kIndexBuffer = kBase + 0x5000;
  constexpr std::uint64_t kWriteDestination = kBase + 0x6000;
  GuestMemory memory(
      kBase, 0x8000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  GpuRuntime runtime(memory);

  const std::array register_table = {0x8eU, 0x0000000fU};
  WriteDwords(memory, kRegisterTable, register_table);
  WriteValue32(memory, kWaitLabel, 0x55U);

  const std::array nested = {
      Pm4(5, 0x15), 2U, 3U, 4U, 0x41U,
      0x80000000U,
  };
  WriteDwords(memory, kNested, nested);

  const std::array main_commands = {
      Pm4(3, 0x69), 0x8eU, 0x00000007U,
      Pm4(3, 0x76), 0xc8U, 0x00448582U,
      Pm4(4, 0x10, 0x12), 1U,
      static_cast<std::uint32_t>(kRegisterTable),
      static_cast<std::uint32_t>(kRegisterTable >> 32U),
      Pm4(3, 0x26), static_cast<std::uint32_t>(kIndexBuffer),
      static_cast<std::uint32_t>(kIndexBuffer >> 32U),
      Pm4(2, 0x13), 9U,
      Pm4(2, 0x2f), 2U,
      Pm4(6, 0x27), 9U, static_cast<std::uint32_t>(kIndexBuffer),
      static_cast<std::uint32_t>(kIndexBuffer >> 32U), 9U, 0U,
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kWaitLabel),
      static_cast<std::uint32_t>(kWaitLabel >> 32U), 0xffU, 0x55U, 0x13U,
      1U,
      Pm4(4, 0x3f), static_cast<std::uint32_t>(kNested),
      static_cast<std::uint32_t>(kNested >> 32U),
      0x0f200000U | static_cast<std::uint32_t>(nested.size()),
      Pm4(6, 0x37), 0xa5U | (5U << 8U) | (1U << 20U) | (2U << 25U),
      static_cast<std::uint32_t>(kWriteDestination),
      static_cast<std::uint32_t>(kWriteDestination >> 32U), 0x11223344U,
      0xaabbccddU,
  };
  static_assert(main_commands.size() == 40);
  WriteDwords(memory, kMain, main_commands);

  GpuActionTrace trace(64);
  GpuMemorySubmissionSink memory_sink(memory, trace);
  const auto result = runtime.ProcessCommandBuffer(
      kMain, static_cast<std::uint32_t>(main_commands.size()), memory_sink);
  Check(result.status == GpuCommandStatus::kComplete,
        "valid command stream did not complete");
  Check(result.processed_dwords == main_commands.size() + nested.size(),
        "processed dword count did not include the indirect buffer");
  Check(result.submitted_actions == 12 && trace.actions().size() == 12,
        "action trace count is incorrect");
  Check(trace.actions()[0].type == GpuActionType::kRegisterWrite &&
            trace.actions()[0].values[0] ==
                static_cast<std::uint64_t>(GpuRegisterSpace::kContext) &&
            trace.actions()[0].values[1] == 0x8eU &&
            trace.actions()[0].values[2] == 7U,
        "direct context register was decoded incorrectly");
  Check(trace.actions()[2].type == GpuActionType::kRegisterWrite &&
            trace.actions()[2].values[1] == 0x8eU &&
            trace.actions()[2].values[2] == 0xfU &&
            trace.actions()[2].values[3] == kRegisterTable,
        "indirect context register did not use the direct register key");
  Check(trace.actions()[7].type == GpuActionType::kWaitMemory &&
            trace.actions()[7].values[0] == kWaitLabel &&
            trace.actions()[7].values[3] == 0x55U,
        "satisfied wait was not recorded exactly");
  Check(trace.actions()[8].type == GpuActionType::kIndirectBuffer &&
            trace.actions()[8].values[0] == kNested &&
            trace.actions()[8].values[1] == nested.size(),
        "indirect buffer transition is incorrect");
  Check(trace.actions()[9].type == GpuActionType::kDispatch &&
            trace.actions()[9].values[0] == 2U &&
            trace.actions()[9].values[1] == 3U &&
            trace.actions()[9].values[2] == 4U,
        "nested dispatch is incorrect");
  Check(trace.actions()[11].type == GpuActionType::kWriteData &&
            trace.actions()[11].values[0] == kWriteDestination &&
            trace.actions()[11].values[2] == 2U &&
            trace.actions()[11].values[3] == 0x11223344U,
        "write-data action is incorrect");
  std::array<std::byte, 8> written_data{};
  Check(memory.Read(kWriteDestination, written_data) &&
            Read32(written_data, 0) == 0x11223344U &&
            Read32(written_data, 4) == 0xaabbccddU,
        "standard write-data control did not update guest memory");
  Check(runtime.ReadRegister(GpuRegisterSpace::kContext, 0x8e) == 0xfU &&
            runtime.ReadRegister(GpuRegisterSpace::kShader, 0xc8) ==
                0x00448582U,
        "parsed register state is incorrect");

  constexpr std::uint64_t kReleaseCommands = kBase + 0xa00;
  constexpr std::uint64_t kReleaseDestination = kWriteDestination + 0x20;
  constexpr std::uint64_t kTimestampOne = kReleaseDestination + 0x10;
  constexpr std::uint64_t kTimestampTwo = kTimestampOne + 8;
  const std::array release_commands = {
      Pm4(8, 0x49), 0x28U,
      0xa5U | (1U << 16U) | (2U << 29U),
      static_cast<std::uint32_t>(kReleaseDestination),
      static_cast<std::uint32_t>(kReleaseDestination >> 32U),
      0x55667788U, 0x11223344U, 0U,
      Pm4(8, 0x49), 0x28U, 3U << 29U,
      static_cast<std::uint32_t>(kTimestampOne),
      static_cast<std::uint32_t>(kTimestampOne >> 32U), 0U, 0U, 0U,
      Pm4(8, 0x49), 0x28U, 3U << 29U,
      static_cast<std::uint32_t>(kTimestampTwo),
      static_cast<std::uint32_t>(kTimestampTwo >> 32U), 0U, 0U, 0U,
  };
  WriteDwords(memory, kReleaseCommands, release_commands);
  GpuActionTrace release_trace(3);
  GpuMemorySubmissionSink release_sink(memory, release_trace);
  Check(runtime.ProcessCommandBuffer(
            kReleaseCommands,
            static_cast<std::uint32_t>(release_commands.size()),
            release_sink) &&
            release_trace.actions().size() == 3 &&
            release_trace.actions()[0].type ==
                GpuActionType::kReleaseMemory,
        "standard release-memory packets did not decode");
  std::array<std::byte, 8> released_data{};
  std::array<std::byte, 8> first_timestamp{};
  std::array<std::byte, 8> second_timestamp{};
  Check(memory.Read(kReleaseDestination, released_data) &&
            Read64(released_data) == 0x1122334455667788ULL &&
            memory.Read(kTimestampOne, first_timestamp) &&
            memory.Read(kTimestampTwo, second_timestamp) &&
            Read64(first_timestamp) != 0 &&
            Read64(second_timestamp) > Read64(first_timestamp),
        "ordered release-memory values are incorrect");

  constexpr std::uint64_t kUnsupportedRelease = kBase + 0xb00;
  const std::array unsupported_release = {
      Pm4(8, 0x49), 0x28U, 5U << 29U,
      static_cast<std::uint32_t>(kReleaseDestination),
      static_cast<std::uint32_t>(kReleaseDestination >> 32U),
      0U, 0U, 0U,
  };
  WriteDwords(memory, kUnsupportedRelease, unsupported_release);
  GpuActionTrace unsupported_trace(1);
  GpuMemorySubmissionSink unsupported_sink(memory, unsupported_trace);
  const auto unsupported_result = runtime.ProcessCommandBuffer(
      kUnsupportedRelease,
      static_cast<std::uint32_t>(unsupported_release.size()),
      unsupported_sink);
  Check(unsupported_result.status == GpuCommandStatus::kUnsupportedPacket,
        "unsupported GDS release-memory source was accepted");

  constexpr std::uint64_t kMalformedRelease = kBase + 0xc00;
  const std::array malformed_release = {
      Pm4(8, 0x10, 0x18), 0x62fU, 4U << 29U,
      static_cast<std::uint32_t>(kReleaseDestination),
      static_cast<std::uint32_t>(kReleaseDestination >> 32U),
      0U, 0U, 0U,
  };
  WriteDwords(memory, kMalformedRelease, malformed_release);
  GpuActionTrace malformed_release_trace(1);
  GpuMemorySubmissionSink malformed_release_sink(memory,
                                                 malformed_release_trace);
  const auto malformed_release_result = runtime.ProcessCommandBuffer(
      kMalformedRelease,
      static_cast<std::uint32_t>(malformed_release.size()),
      malformed_release_sink);
  Check(malformed_release_result.status == GpuCommandStatus::kMalformedPacket,
        "invalid AGC release-memory data selector was accepted");

  constexpr std::uint64_t kEventCommands = kBase + 0xd00;
  constexpr std::uint64_t kEventAddress = kBase + 0x6500;
  const std::array event_commands = {
      Pm4(2, 0x46), 0x407U,
      Pm4(4, 0x46), 0x138U,
      static_cast<std::uint32_t>(kEventAddress),
      static_cast<std::uint32_t>(kEventAddress >> 32U),
  };
  WriteDwords(memory, kEventCommands, event_commands);
  GpuActionTrace event_trace(2);
  const auto event_result = runtime.ProcessCommandBuffer(
      kEventCommands, static_cast<std::uint32_t>(event_commands.size()),
      event_trace);
  Check(event_result && event_trace.actions().size() == 2 &&
            event_trace.actions()[0].type == GpuActionType::kEventWrite &&
            event_trace.actions()[0].values[0] == 7 &&
            event_trace.actions()[1].type == GpuActionType::kEventWrite &&
            event_trace.actions()[1].values[0] == 0x38 &&
            event_trace.actions()[1].values[1] == kEventAddress,
        "event-write packets did not preserve their type and address");

  constexpr std::uint64_t kSecondSubmission = kBase + 0x700;
  const std::array second_submission = {Pm4(2, 0x10), 0U};
  WriteDwords(memory, kSecondSubmission, second_submission);
  GpuActionTrace second_trace(2);
  Check(runtime.ProcessCommandBuffer(kSecondSubmission, 2, second_trace) &&
            runtime.ReadRegister(GpuRegisterSpace::kContext, 0x8e) == 0xfU &&
            runtime.ReadRegister(GpuRegisterSpace::kShader, 0xc8) ==
                0x00448582U,
        "register state did not persist across submissions");

  constexpr std::uint64_t kBlocked = kBase + 0x800;
  const std::array blocked_commands = {
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kWaitLabel),
      static_cast<std::uint32_t>(kWaitLabel >> 32U), 0xffU, 0x99U, 0x13U,
      1U,
      Pm4(3, 0x2d), 3U, 2U,
  };
  WriteDwords(memory, kBlocked, blocked_commands);
  GpuActionTrace blocked_trace(4);
  const auto blocked = runtime.ProcessCommandBuffer(
      kBlocked, static_cast<std::uint32_t>(blocked_commands.size()),
      blocked_trace);
  Check(blocked.status == GpuCommandStatus::kBlocked &&
            blocked.packet_address == kBlocked &&
            blocked_trace.actions().size() == 1 &&
            blocked_trace.actions()[0].type == GpuActionType::kWaitMemory,
        "unsatisfied wait did not suspend before the following draw");
  WriteValue32(memory, kWaitLabel, 0x99U);
  blocked_trace.Clear();
  Check(runtime.ProcessCommandBuffer(
            kBlocked, static_cast<std::uint32_t>(blocked_commands.size()),
            blocked_trace) &&
            blocked_trace.actions().size() == 2 &&
            blocked_trace.actions()[1].type == GpuActionType::kDraw,
        "satisfied wait did not resume the command stream");

  constexpr std::uint64_t kResumeRoot = kBase + 0x1100;
  constexpr std::uint64_t kResumeNested = kBase + 0x1200;
  const std::array resume_nested = {
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kWaitLabel),
      static_cast<std::uint32_t>(kWaitLabel >> 32U), 0xffU, 0x44U, 0x13U,
      1U,
      Pm4(3, 0x2d), 5U, 2U,
  };
  const std::array resume_root = {
      Pm4(4, 0x3f), static_cast<std::uint32_t>(kResumeNested),
      static_cast<std::uint32_t>(kResumeNested >> 32U),
      0x0f200000U | static_cast<std::uint32_t>(resume_nested.size()),
      Pm4(5, 0x15), 4U, 3U, 2U, 0x41U,
  };
  WriteValue32(memory, kWaitLabel, 0U);
  WriteDwords(memory, kResumeNested, resume_nested);
  WriteDwords(memory, kResumeRoot, resume_root);
  auto cursor = runtime.BeginCommandBuffer(
      kResumeRoot, static_cast<std::uint32_t>(resume_root.size()));
  GpuActionTrace resume_trace(8);
  const auto first_slice =
      runtime.ResumeCommandBuffer(cursor, resume_trace);
  Check(first_slice.status == GpuCommandStatus::kBlocked &&
            resume_trace.actions().size() == 2 &&
            resume_trace.actions()[0].type ==
                GpuActionType::kIndirectBuffer &&
            resume_trace.actions()[1].type == GpuActionType::kWaitMemory,
        "nested wait did not preserve its submission position");
  const std::array changed_words = {Pm4(2, 0xfe), 0U};
  WriteDwords(memory, kResumeRoot, changed_words);
  WriteDwords(memory, kResumeNested, changed_words);
  WriteValue32(memory, kWaitLabel, 0x44U);
  const auto final_slice =
      runtime.ResumeCommandBuffer(cursor, resume_trace);
  Check(final_slice.status == GpuCommandStatus::kComplete &&
            final_slice.processed_dwords ==
                resume_root.size() + resume_nested.size() &&
            resume_trace.actions().size() == 4 &&
            resume_trace.actions()[2].type == GpuActionType::kDraw &&
            resume_trace.actions()[3].type == GpuActionType::kDispatch,
        "blocked submission did not resume its owned command snapshot");
  WriteValue32(memory, kWaitLabel, 0x99U);

  constexpr std::uint64_t kMalformed = kBase + 0x900;
  const std::array malformed = {Pm4(5, 0x15), 1U};
  WriteDwords(memory, kMalformed, malformed);
  GpuActionTrace error_trace(4);
  Check(runtime.ProcessCommandBuffer(kMalformed, 2, error_trace).status ==
            GpuCommandStatus::kMalformedPacket,
        "truncated PM4 packet was accepted");

  constexpr std::uint64_t kUnsupported = kBase + 0xa00;
  const std::array unsupported = {Pm4(2, 0xfe), 0U};
  WriteDwords(memory, kUnsupported, unsupported);
  Check(runtime.ProcessCommandBuffer(kUnsupported, 2, error_trace).status ==
            GpuCommandStatus::kUnsupportedPacket,
        "unknown PM4 opcode was accepted");

  constexpr std::uint64_t kCycle = kBase + 0xb00;
  const std::array cycle = {
      Pm4(4, 0x3f), static_cast<std::uint32_t>(kCycle),
      static_cast<std::uint32_t>(kCycle >> 32U), 0x0f200004U,
  };
  WriteDwords(memory, kCycle, cycle);
  GpuActionTrace cycle_trace(4);
  Check(runtime.ProcessCommandBuffer(kCycle, 4, cycle_trace).status ==
            GpuCommandStatus::kResourceLimit &&
            cycle_trace.actions().size() == 1,
        "indirect-buffer cycle was not bounded");

  constexpr std::uint64_t kBranch = kBase + 0xc00;
  constexpr std::uint64_t kThen = kBase + 0xd00;
  constexpr std::uint64_t kElse = kBase + 0xe00;
  const std::array then_commands = {Pm4(5, 0x15), 7U, 8U, 9U, 0x41U};
  const std::array else_commands = {Pm4(3, 0x2d), 6U, 2U};
  WriteDwords(memory, kThen, then_commands);
  WriteDwords(memory, kElse, else_commands);
  const std::array branch = {
      Pm4(14, 0x3f), 0x302U,
      static_cast<std::uint32_t>(kWaitLabel),
      static_cast<std::uint32_t>(kWaitLabel >> 32U), 0xffU, 0U, 0x99U, 0U,
      static_cast<std::uint32_t>(kThen),
      static_cast<std::uint32_t>(kThen >> 32U),
      static_cast<std::uint32_t>(then_commands.size()),
      static_cast<std::uint32_t>(kElse),
      static_cast<std::uint32_t>(kElse >> 32U),
      static_cast<std::uint32_t>(else_commands.size()),
  };
  WriteDwords(memory, kBranch, branch);
  GpuActionTrace branch_trace(4);
  Check(runtime.ProcessCommandBuffer(kBranch, 14, branch_trace) &&
            branch_trace.actions().size() == 2 &&
            branch_trace.actions()[0].type ==
                GpuActionType::kIndirectBuffer &&
            branch_trace.actions()[0].values[0] == kThen &&
            branch_trace.actions()[0].values[4] == 1 &&
            branch_trace.actions()[1].type == GpuActionType::kDispatch,
        "conditional branch did not take the matching buffer");
  WriteValue32(memory, kWaitLabel, 0x11U);
  branch_trace.Clear();
  Check(runtime.ProcessCommandBuffer(kBranch, 14, branch_trace) &&
            branch_trace.actions().size() == 2 &&
            branch_trace.actions()[0].values[0] == kElse &&
            branch_trace.actions()[0].values[4] == 0 &&
            branch_trace.actions()[1].type == GpuActionType::kDraw,
        "conditional branch did not take the alternate buffer");

  Check(std::string_view(kajps5::gpu::GpuCommandStatusName(
                            GpuCommandStatus::kUnsupportedPacket)) ==
            "unsupported-packet",
        "command status name is incorrect");

  GpuActionRing ring(2);
  kajps5::gpu::GpuAction ring_action;
  ring_action.type = GpuActionType::kNop;
  Check(ring.Submit(ring_action) == GpuCommandStatus::kComplete,
        "action ring rejected its first action");
  ring_action.type = GpuActionType::kDraw;
  Check(ring.Submit(ring_action) == GpuCommandStatus::kComplete,
        "action ring rejected its second action");
  ring_action.type = GpuActionType::kDispatch;
  Check(ring.Submit(ring_action) == GpuCommandStatus::kComplete &&
            ring.size() == 2 && ring.dropped_count() == 1 &&
            ring.At(0) != nullptr &&
            ring.At(0)->type == GpuActionType::kDraw &&
            ring.At(1) != nullptr &&
            ring.At(1)->type == GpuActionType::kDispatch,
        "action ring did not retain the newest bounded history");
  return 0;
}
