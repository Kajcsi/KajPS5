// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "cpu/native_hle_trampoline.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_export_registry_test: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::HleRegister;
  using kajps5::memory::GuestMemory;

  GuestMemory memory(0x1000, 32);
  const std::array text = {std::byte{'t'}, std::byte{'e'}, std::byte{'s'},
                           std::byte{'t'}, std::byte{0}};
  Check(memory.Write(0x1000, text), "handler string setup failed");
  HleCallContext context(memory);
  Check(context.SetRegister(HleRegister::kRdi, 0x1000),
        "handler argument setup failed");

  ExportRegistry registry;
  std::size_t dispatch_count = 0;
  const auto length_handler = [&dispatch_count](HleCallContext& call) {
    ++dispatch_count;
    const auto address = call.Argument(0);
    if (!address.has_value()) {
      return HleContextStatus::kInvalidArgument;
    }
    const auto value = call.ReadNullTerminatedString(*address, 16);
    if (!value) {
      return value.status;
    }
    call.SetReturn(value.value.size());
    return HleContextStatus::kOk;
  };
  Check(registry.Register("libkernel", "length", length_handler) ==
            ExportRegistryStatus::kOk,
        "kernel handler registration failed");
  Check(registry.Register("compat", "length",
                          [](HleCallContext& call) {
                            call.SetReturn(99);
                            return HleContextStatus::kOk;
                          }) == ExportRegistryStatus::kOk,
        "compat handler registration failed");
  Check(registry.size() == 2, "export registry size is incorrect");
  Check(registry.Register("libkernel", "length", length_handler) ==
            ExportRegistryStatus::kAlreadyExists,
        "duplicate handler registration was accepted");
  Check(registry.Register("", "bad", length_handler) ==
            ExportRegistryStatus::kInvalidArgument &&
            registry.Register("libkernel", "", length_handler) ==
                ExportRegistryStatus::kInvalidArgument &&
            registry.Register("libkernel", "bad", {}) ==
                ExportRegistryStatus::kInvalidArgument,
        "invalid handler registration was accepted");

  std::vector<kajps5::hle::HleExportDefinition> rejected_batch;
  rejected_batch.push_back(
      {"libc", "unique", [](HleCallContext&) {
         return HleContextStatus::kOk;
       }});
  rejected_batch.push_back({"libkernel", "length", length_handler});
  Check(registry.RegisterBatch(std::move(rejected_batch)) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 2 &&
            registry.Dispatch("unique", context).status ==
                ExportRegistryStatus::kNotFound,
        "rejected export batch changed the registry");
  Check(registry.RegisterBatch({}) ==
            ExportRegistryStatus::kInvalidArgument,
        "empty export batch was accepted");

  std::size_t fallback_calls = 0;
  std::size_t replacement_calls = 0;
  Check(registry.RegisterFallback(
            "compat", "late-export", [&fallback_calls](HleCallContext& call) {
              ++fallback_calls;
              call.SetReturn(7);
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "fallback registration failed");
  kajps5::cpu::NativeHleTrampoline late_trampoline(
      memory, registry, "late-export", std::vector<std::string>{"compat"});
  Check(late_trampoline.status() == kajps5::cpu::NativeHleTrampolineStatus::kOk &&
            reinterpret_cast<std::uint64_t (*)()>(
                static_cast<std::uintptr_t>(late_trampoline.address()))() == 7 &&
            fallback_calls == 1,
        "fallback was not dispatched by the existing trampoline");
  Check(registry.Register(
            "compat", "late-export", [&replacement_calls](HleCallContext& call) {
              ++replacement_calls;
              call.SetReturn(11);
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk &&
            reinterpret_cast<std::uint64_t (*)()>(
                static_cast<std::uintptr_t>(late_trampoline.address()))() == 11 &&
            fallback_calls == 1 && replacement_calls == 1,
        "normal registration did not replace the fallback for trampoline dispatch");
  Check(registry.Register("compat", "real-export", length_handler) ==
            ExportRegistryStatus::kOk &&
            registry.RegisterFallback("compat", "real-export", length_handler) ==
                ExportRegistryStatus::kAlreadyExists,
        "fallback overwrote an installed export");

  const std::vector<std::string> kernel_first = {"libkernel", "compat"};
  const auto looked_up = registry.Lookup("length", kernel_first);
  Check(looked_up && looked_up.library == "libkernel" &&
            dispatch_count == 0,
        "ordered HLE lookup called a handler or returned the wrong result");
  const auto dispatched =
      registry.Dispatch("length", kernel_first, context);
  Check(dispatched && dispatched.library == "libkernel" &&
            dispatch_count == 1 &&
            context.GetRegister(HleRegister::kRax).value_or(0) == 4,
        "ordered HLE dispatch returned the wrong result");

  HleCallContext compat_context(memory);
  const std::vector<std::string> compat_only = {"compat"};
  const auto compat =
      registry.Dispatch("length", compat_only, compat_context);
  Check(compat && compat.library == "compat" &&
            compat_context.GetRegister(HleRegister::kRax).value_or(0) == 99,
        "alternate library dispatch failed");
  Check(registry.Dispatch("length", context).status ==
            ExportRegistryStatus::kAmbiguous &&
            dispatch_count == 1,
        "ambiguous unscoped handler was dispatched");
  Check(registry.Lookup("length").status ==
            ExportRegistryStatus::kAmbiguous &&
            dispatch_count == 1,
        "ambiguous unscoped lookup changed handler state");
  const std::vector<std::string> missing_library = {"missing"};
  Check(registry.Lookup("length", missing_library).status ==
            ExportRegistryStatus::kNotFound,
        "lookup escaped the needed-library scope");
  Check(registry.Dispatch("length", missing_library, context).status ==
            ExportRegistryStatus::kNotFound,
        "dispatch escaped the needed-library scope");
  Check(registry.Dispatch("missing", context).status ==
            ExportRegistryStatus::kNotFound,
        "missing handler returned the wrong status");

  HleCallContext fault_context(memory);
  Check(fault_context.SetRegister(HleRegister::kRdi, 0x2000),
        "fault argument setup failed");
  const auto fault =
      registry.Dispatch("length", kernel_first, fault_context);
  Check(fault.status == ExportRegistryStatus::kOk &&
            fault.handler_status == HleContextStatus::kMemoryFault && !fault &&
            dispatch_count == 2,
        "handler memory fault was not propagated");

  const std::vector<std::string> invalid_library = {""};
  Check(registry.Dispatch("length", invalid_library, context).status ==
            ExportRegistryStatus::kInvalidArgument &&
            registry.Dispatch("", context).status ==
                ExportRegistryStatus::kInvalidArgument,
        "invalid dispatch name was accepted");
  Check(kajps5::hle::ExportRegistryStatusName(
            ExportRegistryStatus::kAmbiguous) == "ambiguous",
        "export registry status name is unstable");
  if (failures == 0) {
    std::cout << "hle export registry tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
