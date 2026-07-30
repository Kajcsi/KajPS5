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
            registry.size() == 82,
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

  const std::vector<std::string> wrong_scope = {"libkernel"};
  Check(registry.Dispatch(kajps5::hle::kLibcMemcpyNid, wrong_scope, copy)
                .status == ExportRegistryStatus::kNotFound,
        "libc memory export escaped its library scope");
  return failures == 0 ? 0 : 1;
}
