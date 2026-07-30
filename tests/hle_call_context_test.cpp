// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_call_context_test: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::HleRegister;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(0x1000, 64);
  HleCallContext context(memory);
  const std::array argument_registers = {
      HleRegister::kRdi, HleRegister::kRsi, HleRegister::kRdx,
      HleRegister::kRcx, HleRegister::kR8,  HleRegister::kR9};
  for (std::size_t index = 0; index < argument_registers.size(); ++index) {
    Check(context.SetRegister(argument_registers[index], index + 1),
          "argument register write failed");
    Check(context.Argument(index).value_or(0) == index + 1,
          "System V argument order is incorrect");
  }
  Check(!context.Argument(argument_registers.size()).has_value(),
        "unmapped stack argument was accepted");
  Check(context.WriteUInt64(0x1028, 7) == HleContextStatus::kOk &&
            context.WriteUInt64(0x1030, 8) == HleContextStatus::kOk &&
            context.SetRegister(HleRegister::kRsp, 0x1020) &&
            context.Argument(6).value_or(0) == 7 &&
            context.Argument(7).value_or(0) == 8,
        "System V stack argument order is incorrect");
  Check(!context.Argument(std::numeric_limits<std::size_t>::max())
             .has_value(),
        "overflowing stack argument was accepted");
  Check(!context.SetRegister(static_cast<HleRegister>(255), 1) &&
            !context.GetRegister(static_cast<HleRegister>(255)).has_value(),
        "invalid register was accepted");

  Check(!context.return_written(), "return started as written");
  context.SetReturn(0xfedcba9876543210);
  Check(context.return_written() &&
            context.GetRegister(HleRegister::kRax).value_or(0) ==
                0xfedcba9876543210,
        "return register was not recorded");

  Check(context.WriteUInt64(0x1000, 0x1122334455667788) ==
            HleContextStatus::kOk,
        "checked 64-bit write failed");
  std::uint64_t value64 = 0;
  Check(context.ReadUInt64(0x1000, value64) == HleContextStatus::kOk &&
            value64 == 0x1122334455667788,
        "checked 64-bit read failed");
  Check(context.WriteUInt32(0x1008, 0xaabbccdd) ==
            HleContextStatus::kOk,
        "checked 32-bit write failed");
  std::uint32_t value32 = 0;
  Check(context.ReadUInt32(0x1008, value32) == HleContextStatus::kOk &&
            value32 == 0xaabbccdd,
        "checked 32-bit read failed");

  const std::array text = {std::byte{'h'}, std::byte{'e'}, std::byte{'l'},
                           std::byte{'l'}, std::byte{'o'}, std::byte{0}};
  Check(memory.Write(0x1010, text), "string setup failed");
  const auto string = context.ReadNullTerminatedString(0x1010, 16);
  Check(string && string.value == "hello", "checked string read failed");

  GuestMemory boundary_memory(0x2000, 4, GuestMemoryProtection::kNone);
  Check(boundary_memory.Map(0x2000, 3, GuestMemoryProtection::kRead),
        "boundary mapping failed");
  const std::array boundary_text = {std::byte{'o'}, std::byte{'k'},
                                    std::byte{0}};
  Check(boundary_memory.Initialize(0x2000, boundary_text),
        "boundary string setup failed");
  HleCallContext boundary_context(boundary_memory);
  const auto boundary =
      boundary_context.ReadNullTerminatedString(0x2000, 4);
  Check(boundary && boundary.value == "ok",
        "terminator before an unmapped byte was not found");

  GuestMemory short_memory(0x3000, 4, GuestMemoryProtection::kNone);
  Check(short_memory.Map(0x3000, 2, GuestMemoryProtection::kRead),
        "short mapping failed");
  const std::array short_text = {std::byte{'n'}, std::byte{'o'}};
  Check(short_memory.Initialize(0x3000, short_text),
        "short string setup failed");
  HleCallContext short_context(short_memory);
  const auto fault = short_context.ReadNullTerminatedString(0x3000, 4);
  Check(fault.status == HleContextStatus::kMemoryFault &&
            fault.value.empty(),
        "unmapped string read returned partial data");

  GuestMemory unterminated_memory(0x4000, 3);
  const std::array unterminated_text = {std::byte{'a'}, std::byte{'b'},
                                        std::byte{'c'}};
  Check(unterminated_memory.Write(0x4000, unterminated_text),
        "unterminated string setup failed");
  HleCallContext unterminated_context(unterminated_memory);
  const auto unterminated =
      unterminated_context.ReadNullTerminatedString(0x4000, 3);
  Check(unterminated.status == HleContextStatus::kUnterminatedString &&
            unterminated.value == "abc",
        "unterminated string returned the wrong result");

  GuestMemory read_only(0x5000, 8, GuestMemoryProtection::kRead);
  HleCallContext read_only_context(read_only);
  Check(read_only_context.WriteUInt64(0x5000, 1) ==
            HleContextStatus::kMemoryFault,
        "read-only guest memory accepted an HLE write");
  GuestMemory partial_memory(0x6000, 16, GuestMemoryProtection::kNone);
  Check(partial_memory.Map(0x6000, 8,
                           GuestMemoryProtection::kRead |
                               GuestMemoryProtection::kWrite),
        "partial write mapping failed");
  const std::array sentinel = {
      std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa},
      std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa}, std::byte{0xaa}};
  Check(partial_memory.Initialize(0x6000, sentinel),
        "partial write setup failed");
  HleCallContext partial_context(partial_memory);
  const std::array oversized = {
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
      std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12},
      std::byte{13}, std::byte{14}, std::byte{15}, std::byte{16}};
  std::array<std::byte, 8> preserved{};
  Check(partial_context.WriteMemory(0x6000, oversized) ==
            HleContextStatus::kMemoryFault &&
            partial_memory.Read(0x6000, preserved) && preserved == sentinel,
        "failed whole-range HLE write changed guest memory");
  Check(partial_context.WriteMemory(0x6000, {}) ==
            HleContextStatus::kInvalidArgument,
        "empty HLE memory write was accepted");
  Check(context.ReadNullTerminatedString(0, 1).status ==
            HleContextStatus::kInvalidArgument &&
            context
                    .ReadNullTerminatedString(
                        0x1000, kajps5::hle::kMaximumHleStringBytes + 1)
                    .status == HleContextStatus::kInvalidArgument,
        "invalid string bounds were accepted");
  Check(kajps5::hle::HleContextStatusName(
            HleContextStatus::kUnterminatedString) == "unterminated-string",
        "HLE context status name is unstable");
  return failures == 0 ? 0 : 1;
}
