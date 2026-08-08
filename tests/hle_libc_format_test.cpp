// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/libc_exports.h"
#include "hle/libc_format.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_libc_format_test: " << message << '\n';
    ++failures;
  }
}

void SetArguments(kajps5::hle::HleCallContext& context,
                  std::initializer_list<std::uint64_t> arguments) {
  constexpr std::array registers = {
      kajps5::hle::HleRegister::kRdi, kajps5::hle::HleRegister::kRsi,
      kajps5::hle::HleRegister::kRdx, kajps5::hle::HleRegister::kRcx,
      kajps5::hle::HleRegister::kR8,  kajps5::hle::HleRegister::kR9};
  std::size_t index = 0;
  for (const auto argument : arguments) {
    Check(index < registers.size() &&
              context.SetRegister(registers[index], argument),
          "argument setup failed");
    ++index;
  }
}

std::vector<std::byte> Bytes(std::string_view value) {
  std::vector<std::byte> bytes;
  bytes.reserve(value.size() + 1);
  for (const auto character : value) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

std::string ReadString(kajps5::memory::GuestMemory& memory,
                       std::uint64_t address) {
  kajps5::hle::HleCallContext context(memory);
  const auto result = context.ReadNullTerminatedString(
      address, kajps5::hle::kMaximumHleStringBytes);
  return result ? result.value : std::string{};
}

std::uint64_t ReturnValue(const kajps5::hle::HleCallContext& context) {
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

kajps5::hle::HleVectorValue ScalarVector(double value) {
  kajps5::hle::HleVectorValue result{};
  std::memcpy(result.data(), &value, sizeof(value));
  return result;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kPage = 0x10000;
  GuestMemory memory(kPage, 0x40000, GuestMemoryProtection::kNone);
  Check(memory.Map(kPage, 0x1000,
                   GuestMemoryProtection::kRead |
                       GuestMemoryProtection::kWrite),
        "format test memory did not map");
  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcExports(
            registry, runtime.cxa_guards(), runtime.process_lifecycle(),
            runtime.libc_heap(), memory) == ExportRegistryStatus::kOk &&
            registry.size() == 110,
        "libc format exports did not register atomically");
  const std::vector<std::string> scope = {kajps5::hle::kLibcName};

  constexpr std::uint64_t kDestination = kPage + 0x100;
  constexpr std::uint64_t kFormat = kPage + 0x200;
  constexpr std::uint64_t kText = kPage + 0x300;
  const auto format = Bytes("id=%04d hex=%#x str=%.3s pct=%%");
  const auto text = Bytes("hello");
  Check(memory.Write(kFormat, format) && memory.Write(kText, text),
        "direct format setup failed");
  HleCallContext snprintf_call(memory);
  SetArguments(snprintf_call,
               {kDestination, 64, kFormat, 7, 0x2a, kText});
  const std::string expected = "id=0007 hex=0x2a str=hel pct=%";
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid, scope,
                          snprintf_call) &&
            ReturnValue(snprintf_call) == expected.size() &&
            ReadString(memory, kDestination) == expected,
        "snprintf did not format register arguments");

  HleCallContext truncated(memory);
  SetArguments(truncated, {kDestination, 8, kFormat, 7, 0x2a, kText});
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSnprintfName, scope,
                          truncated) &&
            ReturnValue(truncated) == expected.size() &&
            ReadString(memory, kDestination) == expected.substr(0, 7),
        "snprintf truncation returned the wrong result");

  constexpr std::uint64_t kLiteralFormat = kPage + 0x240;
  const auto literal_format = Bytes("hello");
  Check(memory.Write(kLiteralFormat, literal_format),
        "literal format setup failed");
  HleCallContext size_query(memory);
  SetArguments(size_query, {0, 0, kLiteralFormat});
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid, scope,
                          size_query) &&
            ReturnValue(size_query) == 5,
        "snprintf size query required a destination");

  constexpr std::uint64_t kFloatFormat = kPage + 0x260;
  const auto float_format = Bytes("%.2f");
  Check(memory.Write(kFloatFormat, float_format),
        "floating format setup failed");
  HleCallContext float_call(memory);
  SetArguments(float_call, {kDestination, 64, kFloatFormat});
  const std::array<kajps5::hle::HleVectorValue, 1> vector_arguments = {
      ScalarVector(1.25)};
  Check(float_call.SetCapturedVectorArguments(vector_arguments) &&
            registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid,
                              scope, float_call) &&
            ReadString(memory, kDestination) == "1.25",
        "snprintf did not read a floating register argument");

  constexpr std::uint64_t kMixedFormat = kPage + 0x2c0;
  const auto mixed_format = Bytes("%d %.1f %d %d %d");
  Check(memory.Write(kMixedFormat, mixed_format),
        "mixed argument format setup failed");
  HleCallContext mixed_call(memory);
  SetArguments(mixed_call,
               {kDestination, 64, kMixedFormat, 1, 2, 3});
  const std::array<std::uint64_t, 1> stack_arguments = {4};
  const std::array<kajps5::hle::HleVectorValue, 1> mixed_vector_arguments = {
      ScalarVector(1.5)};
  Check(mixed_call.SetCapturedStackArguments(stack_arguments) &&
            mixed_call.SetCapturedVectorArguments(mixed_vector_arguments) &&
            registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid,
                              scope, mixed_call) &&
            ReadString(memory, kDestination) == "1 1.5 2 3 4",
        "snprintf did not keep integer, vector, and stack arguments separate");

  constexpr std::uint64_t kSprintfFormat = kPage + 0x280;
  const auto sprintf_format = Bytes("v=%d");
  Check(memory.Write(kSprintfFormat, sprintf_format),
        "sprintf setup failed");
  HleCallContext sprintf_call(memory);
  SetArguments(sprintf_call, {kDestination, kSprintfFormat, 9});
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSprintfNid, scope,
                          sprintf_call) &&
            ReturnValue(sprintf_call) == 3 &&
            ReadString(memory, kDestination) == "v=9",
        "sprintf did not format its first variable argument");

  constexpr std::uint64_t kVaFormat = kPage + 0x2a0;
  constexpr std::uint64_t kVaList = kPage + 0x400;
  constexpr std::uint64_t kRegisterSave = kPage + 0x500;
  constexpr std::uint64_t kOverflow = kPage + 0x700;
  const auto va_format = Bytes("%d %.1f %s");
  HleCallContext setup(memory);
  Check(memory.Write(kVaFormat, va_format) &&
            setup.WriteUInt32(kVaList, 0) == HleContextStatus::kOk &&
            setup.WriteUInt32(kVaList + 4, 48) == HleContextStatus::kOk &&
            setup.WriteUInt64(kVaList + 8, kOverflow) ==
                HleContextStatus::kOk &&
            setup.WriteUInt64(kVaList + 16, kRegisterSave) ==
                HleContextStatus::kOk &&
            setup.WriteUInt64(kRegisterSave, 42) == HleContextStatus::kOk &&
            setup.WriteUInt64(kRegisterSave + 8, kText) ==
                HleContextStatus::kOk &&
            setup.WriteUInt64(kRegisterSave + 48,
                              std::bit_cast<std::uint64_t>(2.5)) ==
                HleContextStatus::kOk,
        "va_list setup failed");
  HleCallContext vsnprintf_call(memory);
  SetArguments(vsnprintf_call,
               {kDestination, 64, kVaFormat, kVaList});
  Check(registry.Dispatch(kajps5::hle::detail::kLibcVsnprintfNid, scope,
                          vsnprintf_call) &&
            ReadString(memory, kDestination) == "42 2.5 hello",
        "vsnprintf did not follow the System V va_list layout");

  constexpr std::uint64_t kOverflowFormat = kPage + 0x2e0;
  const auto overflow_format = Bytes("%d %.1f");
  Check(memory.Write(kOverflowFormat, overflow_format) &&
            setup.WriteUInt32(kVaList, 48) == HleContextStatus::kOk &&
            setup.WriteUInt32(kVaList + 4, 176) == HleContextStatus::kOk &&
            setup.WriteUInt64(kOverflow, 55) == HleContextStatus::kOk &&
            setup.WriteUInt64(kOverflow + 8,
                              std::bit_cast<std::uint64_t>(3.5)) ==
                HleContextStatus::kOk,
        "va_list overflow setup failed");
  HleCallContext overflow_call(memory);
  SetArguments(overflow_call,
               {kDestination, 64, kOverflowFormat, kVaList});
  Check(registry.Dispatch(kajps5::hle::detail::kLibcVsnprintfNid, scope,
                          overflow_call) &&
            ReadString(memory, kDestination) == "55 3.5",
        "vsnprintf did not read overflow arguments in order");

  constexpr std::uint64_t kCountFormat = kPage + 0x320;
  constexpr std::uint64_t kCount = kPage + 0x3c0;
  const auto count_format = Bytes("abc%n-%d");
  HleCallContext count_setup(memory);
  Check(memory.Write(kCountFormat, count_format) &&
            count_setup.WriteUInt32(kCount, 0xffffffffU) ==
                HleContextStatus::kOk,
        "format count setup failed");
  HleCallContext count_call(memory);
  SetArguments(count_call,
               {kDestination, 64, kCountFormat, kCount, 9});
  std::uint32_t count = 0;
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid, scope,
                          count_call) &&
            count_setup.ReadUInt32(kCount, count) == HleContextStatus::kOk &&
            count == 3 && ReadString(memory, kDestination) == "abc-9",
        "snprintf did not store its formatted byte count");

  constexpr std::uint64_t kLastByte = kPage + 0xfff;
  const std::array sentinel = {std::byte{0x7f}};
  const auto staged_count_format = Bytes("abc%n");
  Check(memory.Write(kCountFormat, staged_count_format) &&
            count_setup.WriteUInt32(kCount, 0xffffffffU) ==
                HleContextStatus::kOk &&
            memory.Write(kLastByte, sentinel),
        "failed-output sentinel setup failed");
  HleCallContext bad_output(memory);
  SetArguments(bad_output, {kLastByte, 4, kCountFormat, kCount});
  std::array<std::byte, 1> preserved{};
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid, scope,
                          bad_output)
                .handler_status == HleContextStatus::kMemoryFault &&
            memory.Read(kLastByte, preserved) && preserved == sentinel &&
            count_setup.ReadUInt32(kCount, count) == HleContextStatus::kOk &&
            count == 0xffffffffU,
        "failed formatted output changed guest memory or a count target");

  const std::vector<std::string> wrong_scope = {"libkernel"};
  Check(registry.Dispatch(kajps5::hle::detail::kLibcSnprintfNid,
                          wrong_scope, snprintf_call)
                .status == ExportRegistryStatus::kNotFound,
        "libc format export escaped its library scope");
  return failures == 0 ? 0 : 1;
}
