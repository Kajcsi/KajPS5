// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/libc_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_libc_memory_math_test: " << message << '\n';
    ++failures;
  }
}

void SetArguments(kajps5::hle::HleCallContext& context,
                  std::initializer_list<std::uint64_t> arguments) {
  constexpr std::array registers = {
      kajps5::hle::HleRegister::kRdi,
      kajps5::hle::HleRegister::kRsi,
      kajps5::hle::HleRegister::kRdx};
  std::size_t index = 0;
  for (const auto argument : arguments) {
    Check(index < registers.size() &&
              context.SetRegister(registers[index], argument),
          "argument setup failed");
    ++index;
  }
}

template <typename T>
kajps5::hle::HleVectorValue ScalarVector(T value) {
  kajps5::hle::HleVectorValue result{};
  std::memcpy(result.data(), &value, sizeof(value));
  return result;
}

template <typename T>
std::optional<T> ScalarReturn(const kajps5::hle::HleCallContext& context) {
  const auto vector = context.VectorReturn(0);
  if (!vector) {
    return std::nullopt;
  }
  T value{};
  std::memcpy(&value, vector->data(), sizeof(value));
  return value;
}

std::optional<float> CallFloat(
    const kajps5::hle::ExportRegistry& registry,
    std::span<const std::string> scope, kajps5::memory::GuestMemory& memory,
  const char* symbol, float input) {
  kajps5::hle::HleCallContext context(memory);
  const std::array<kajps5::hle::HleVectorValue, 1> arguments = {
      ScalarVector(input)};
  if (!context.SetCapturedVectorArguments(arguments) ||
      !registry.Dispatch(symbol, scope, context)) {
    return std::nullopt;
  }
  return ScalarReturn<float>(context);
}

std::optional<double> CallDouble(
    const kajps5::hle::ExportRegistry& registry,
    std::span<const std::string> scope, kajps5::memory::GuestMemory& memory,
  const char* symbol, double input) {
  kajps5::hle::HleCallContext context(memory);
  const std::array<kajps5::hle::HleVectorValue, 1> arguments = {
      ScalarVector(input)};
  if (!context.SetCapturedVectorArguments(arguments) ||
      !registry.Dispatch(symbol, scope, context)) {
    return std::nullopt;
  }
  return ScalarReturn<double>(context);
}

std::uint64_t ReturnValue(const kajps5::hle::HleCallContext& context) {
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

template <typename T>
std::optional<T> ReadGuestScalar(const kajps5::memory::GuestMemory& memory,
                                 std::uint64_t address) {
  std::array<std::byte, sizeof(T)> bytes{};
  if (!memory.Read(address, bytes)) {
    return std::nullopt;
  }
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
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
        "test memory did not map");
  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcExports(
            registry, runtime.cxa_guards(), runtime.process_lifecycle(),
            runtime.libc_heap(), memory) == ExportRegistryStatus::kOk &&
            registry.size() == 96,
        "libc exports did not register atomically");
  const std::vector<std::string> scope = {kajps5::hle::kLibcName};

  const std::array source = {
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
  Check(memory.Write(kPage + 0x80, source), "copy source setup failed");
  HleCallContext copy(memory);
  SetArguments(copy, {kPage + 0x100, kPage + 0x80, source.size()});
  std::array<std::byte, source.size()> copied{};
  Check(registry.Dispatch(kajps5::hle::kLibcMemcpyNid, scope, copy) &&
            ReturnValue(copy) == kPage + 0x100 &&
            memory.Read(kPage + 0x100, copied) && copied == source,
        "memcpy did not copy checked guest bytes");

  HleCallContext fill(memory);
  SetArguments(fill, {kPage + 0x100, 0xab, source.size()});
  const std::array expected_fill = {
      std::byte{0xab}, std::byte{0xab}, std::byte{0xab}, std::byte{0xab}};
  Check(registry.Dispatch(kajps5::hle::kLibcMemsetName, scope, fill) &&
            memory.Read(kPage + 0x100, copied) && copied == expected_fill,
        "memset did not fill checked guest bytes");

  HleCallContext bad_copy(memory);
  SetArguments(bad_copy, {kPage + 0x100, 0x90000, source.size()});
  const auto bad_copy_result = registry.Dispatch(
      kajps5::hle::kLibcMemcpyName, scope, bad_copy);
  Check(bad_copy_result.handler_status == HleContextStatus::kMemoryFault &&
            memory.Read(kPage + 0x100, copied) && copied == expected_fill,
        "failed memcpy changed its destination");

  const std::array left = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
                           std::byte{0}};
  const std::array right = {std::byte{'a'}, std::byte{'b'}, std::byte{'d'},
                            std::byte{0}};
  Check(memory.Write(kPage + 0x200, left) &&
            memory.Write(kPage + 0x210, right),
        "string setup failed");
  HleCallContext compare(memory);
  SetArguments(compare, {kPage + 0x200, kPage + 0x210});
  Check(registry.Dispatch(kajps5::hle::kLibcStrcmpNid, scope, compare) &&
            std::bit_cast<std::int64_t>(ReturnValue(compare)) < 0,
        "strcmp did not preserve unsigned byte ordering");
  HleCallContext length(memory);
  SetArguments(length, {kPage + 0x200});
  Check(registry.Dispatch(kajps5::hle::kLibcStrlenNid, scope, length) &&
            ReturnValue(length) == 3,
        "strlen returned the wrong byte count");

  const std::array left_wide = {
      std::byte{'a'}, std::byte{0}, std::byte{'b'}, std::byte{0},
      std::byte{0}, std::byte{0}};
  const std::array right_wide = {
      std::byte{'a'}, std::byte{0}, std::byte{'c'}, std::byte{0},
      std::byte{0}, std::byte{0}};
  Check(memory.Write(kPage + 0x240, left_wide) &&
            memory.Write(kPage + 0x250, right_wide),
        "wide string setup failed");
  HleCallContext wide_compare(memory);
  SetArguments(wide_compare, {kPage + 0x240, kPage + 0x250});
  Check(registry.Dispatch(kajps5::hle::kLibcWcscmpNid, scope,
                          wide_compare) &&
            std::bit_cast<std::int64_t>(ReturnValue(wide_compare)) < 0,
        "wcscmp did not compare 16-bit guest units");

  const std::array number = {
      std::byte{' '}, std::byte{'+'}, std::byte{'1'}, std::byte{'2'},
      std::byte{'.'}, std::byte{'5'}, std::byte{'x'}, std::byte{0}};
  Check(memory.Write(kPage + 0x280, number), "number string setup failed");
  HleCallContext atof_call(memory);
  SetArguments(atof_call, {kPage + 0x280});
  Check(registry.Dispatch(kajps5::hle::kLibcAtofNid, scope, atof_call) &&
            ScalarReturn<double>(atof_call) &&
            *ScalarReturn<double>(atof_call) == 12.5,
        "atof did not return the parsed scalar value");

  const auto exp2 = CallFloat(registry, scope, memory,
                              kajps5::hle::kLibcExp2fNid, 3.0F);
  const auto sine = CallFloat(registry, scope, memory,
                              kajps5::hle::kLibcSinfNid, 0.5F);
  const auto cosine = CallFloat(registry, scope, memory,
                                kajps5::hle::kLibcCosfNid, 0.5F);
  const auto acosine = CallFloat(registry, scope, memory,
                                 kajps5::hle::kLibcAcosfNid, 0.5F);
  const auto tangent = CallFloat(registry, scope, memory,
                                 kajps5::hle::kLibcTanfNid, 0.25F);
  const auto arctangent = CallDouble(registry, scope, memory,
                                     kajps5::hle::kLibcAtanNid, 0.5);
  Check(exp2 && *exp2 == 8.0F && sine &&
            std::abs(*sine - std::sin(0.5F)) < 1.0e-6F && cosine &&
            std::abs(*cosine - std::cos(0.5F)) < 1.0e-6F && acosine &&
            std::abs(*acosine - std::acos(0.5F)) < 1.0e-6F && tangent &&
            std::abs(*tangent - std::tan(0.25F)) < 1.0e-6F && arctangent &&
            std::abs(*arctangent - std::atan(0.5)) < 1.0e-12,
        "unary libc math returned the wrong value");

  HleCallContext atan2_call(memory);
  const std::array<kajps5::hle::HleVectorValue, 2> atan2_arguments = {
      ScalarVector(1.0), ScalarVector(1.0)};
  Check(atan2_call.SetCapturedVectorArguments(atan2_arguments) &&
            registry.Dispatch(kajps5::hle::kLibcAtan2Nid, scope,
                              atan2_call) &&
            ScalarReturn<double>(atan2_call) &&
            std::abs(*ScalarReturn<double>(atan2_call) - std::atan2(1.0, 1.0)) <
                1.0e-12,
        "atan2 did not consume both vector arguments");

  HleCallContext sincos_call(memory);
  SetArguments(sincos_call, {kPage + 0x300, kPage + 0x308});
  const std::array<kajps5::hle::HleVectorValue, 1> sincos_argument = {
      ScalarVector(0.5)};
  const auto sincos_set =
      sincos_call.SetCapturedVectorArguments(sincos_argument);
  const auto sincos_result = registry.Dispatch(
      kajps5::hle::kLibcSincosNid, scope, sincos_call);
  const auto sine_double = ReadGuestScalar<double>(memory, kPage + 0x300);
  const auto cosine_double = ReadGuestScalar<double>(memory, kPage + 0x308);
  Check(sincos_set && sincos_result && sine_double && cosine_double &&
            std::abs(*sine_double - std::sin(0.5)) < 1.0e-12 &&
            std::abs(*cosine_double - std::cos(0.5)) < 1.0e-12,
        "sincos did not write both checked outputs");
  constexpr double kPreservedOutput = 77.0;
  const auto preserved_bytes = ScalarVector(kPreservedOutput);
  Check(memory.Write(kPage + 0x310,
                     std::span(preserved_bytes).first(sizeof(double))),
        "sincos failure setup failed");
  HleCallContext bad_sincos(memory);
  SetArguments(bad_sincos, {kPage + 0x310, 0x90000});
  const auto bad_sincos_set =
      bad_sincos.SetCapturedVectorArguments(sincos_argument);
  const auto bad_sincos_result = registry.Dispatch(
      kajps5::hle::kLibcSincosNid, scope, bad_sincos);
  Check(bad_sincos_set &&
            bad_sincos_result.handler_status ==
                HleContextStatus::kMemoryFault &&
            ReadGuestScalar<double>(memory, kPage + 0x310) ==
                kPreservedOutput,
        "failed sincos changed its first output");

  HleCallContext sincosf_call(memory);
  SetArguments(sincosf_call, {kPage + 0x320, kPage + 0x324});
  const std::array<kajps5::hle::HleVectorValue, 1> sincosf_argument = {
      ScalarVector(0.25F)};
  const auto sincosf_set =
      sincosf_call.SetCapturedVectorArguments(sincosf_argument);
  const auto sincosf_result = registry.Dispatch(
      kajps5::hle::kLibcSincosfNid, scope, sincosf_call);
  const auto sine_float = ReadGuestScalar<float>(memory, kPage + 0x320);
  const auto cosine_float = ReadGuestScalar<float>(memory, kPage + 0x324);
  Check(sincosf_set && sincosf_result && sine_float && cosine_float &&
            std::abs(*sine_float - std::sin(0.25F)) < 1.0e-6F &&
            std::abs(*cosine_float - std::cos(0.25F)) < 1.0e-6F,
        "sincosf did not write both checked outputs");
  HleCallContext missing_vector(memory);
  Check(registry.Dispatch(kajps5::hle::kLibcSinfNid, scope,
                          missing_vector)
                .handler_status == HleContextStatus::kInvalidArgument,
        "math call accepted a missing vector argument");

  HleCallContext allocate(memory);
  SetArguments(allocate, {48});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcOperatorNewNid, scope, allocate)),
        "operator new dispatch failed");
  const auto allocation = ReturnValue(allocate);
  Check(allocation != 0 && allocation % 16 == 0 &&
            runtime.libc_heap().allocation_count() == 1,
        "operator new did not use the checked guest heap");
  HleCallContext release(memory);
  SetArguments(release, {allocation});
  Check(registry.Dispatch(kajps5::hle::kLibcOperatorDeleteNid, scope,
                          release) &&
            runtime.libc_heap().allocation_count() == 0 &&
            !memory.QueryRegion(allocation).has_value(),
        "operator delete did not release the guest allocation");

  HleCallContext allocate_array(memory);
  SetArguments(allocate_array, {80});
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcOperatorNewArrayNid, scope, allocate_array)),
        "array operator new dispatch failed");
  const auto array_allocation = ReturnValue(allocate_array);
  HleCallContext release_array(memory);
  SetArguments(release_array, {array_allocation});
  Check(array_allocation != 0 &&
            registry.Dispatch(kajps5::hle::kLibcOperatorDeleteArrayNid,
                              scope, release_array) &&
            runtime.libc_heap().allocation_count() == 0,
        "array allocation did not use the checked guest heap");

  const std::vector<std::string> wrong_scope = {"libkernel"};
  Check(registry.Dispatch(kajps5::hle::kLibcMemcpyNid, wrong_scope, copy)
                .status == ExportRegistryStatus::kNotFound,
        "libc memory export escaped its library scope");
  return failures == 0 ? 0 : 1;
}
