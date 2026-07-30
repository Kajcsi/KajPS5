// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/json_exports.h"
#include "kernel/json_value.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_json_exports_test: " << message << '\n';
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
  using kajps5::kernel::KernelRuntime;
  using kajps5::kernel::KernelStatus;
  using kajps5::memory::GuestMemory;

  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterJsonExports(registry, runtime.json_values()) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 16,
        "JSON exports did not register atomically");

  GuestMemory memory(0x1000, 0x1000);
  const std::vector<std::string> json2_scope = {
      kajps5::hle::kJson2LibraryName};
  const std::vector<std::string> json_scope = {
      kajps5::hle::kJsonLibraryName};
  HleCallContext construct(memory);
  Check(construct.SetRegister(HleRegister::kRdi, 0x1100),
        "JSON constructor argument setup failed");
  Check(registry.Dispatch(kajps5::hle::kJsonValueConstructorNid,
                          json2_scope, construct) &&
            construct.GetRegister(HleRegister::kRax).value_or(0) == 0x1100 &&
            runtime.json_values().IsTracked(0x1100) &&
            runtime.json_values().size() == 1,
        "complete JSON value constructor did not create a null shadow");

  HleCallContext reconstruct(memory);
  Check(reconstruct.SetRegister(HleRegister::kRdi, 0x1100),
        "JSON base constructor argument setup failed");
  Check(registry.Dispatch(kajps5::hle::kJsonValueBaseConstructorName,
                          json_scope, reconstruct) &&
            runtime.json_values().size() == 1,
        "base JSON value constructor did not reset the existing shadow");

  HleCallContext destroy(memory);
  Check(destroy.SetRegister(HleRegister::kRdi, 0x1100),
        "JSON destructor argument setup failed");
  Check(registry.Dispatch(kajps5::hle::kJsonValueDestructorNid,
                          json2_scope, destroy) &&
            destroy.GetRegister(HleRegister::kRax).value_or(1) == 0 &&
            !runtime.json_values().IsTracked(0x1100),
        "complete JSON value destructor did not remove the shadow");

  HleCallContext idempotent_destroy(memory);
  Check(idempotent_destroy.SetRegister(HleRegister::kRdi, 0x1200),
        "idempotent destructor argument setup failed");
  Check(registry.Dispatch(kajps5::hle::kJsonValueBaseDestructorNid,
                          json_scope, idempotent_destroy) &&
            runtime.json_values().size() == 0,
        "base JSON value destructor was not idempotent");

  HleCallContext null_construct(memory);
  const auto null_result = registry.Dispatch(
      kajps5::hle::kJsonValueConstructorName, json2_scope, null_construct);
  Check(null_result.handler_status == HleContextStatus::kInvalidArgument &&
            !null_result && runtime.json_values().size() == 0,
        "null JSON value constructor changed shadow state");

  HleCallContext fault(memory);
  Check(fault.SetRegister(HleRegister::kRdi, 0x3000),
        "JSON fault argument setup failed");
  const auto fault_result = registry.Dispatch(
      kajps5::hle::kJsonValueDestructorName, json2_scope, fault);
  Check(fault_result.handler_status == HleContextStatus::kMemoryFault &&
            !fault_result && runtime.json_values().size() == 0,
        "unmapped JSON object was accepted");

  const std::vector<std::string> wrong_scope = {"libSceJsonParser"};
  Check(registry.Dispatch(kajps5::hle::kJsonValueDestructorNid,
                          wrong_scope, destroy)
                .status == ExportRegistryStatus::kNotFound,
        "JSON export lookup escaped its library scope");
  Check(kajps5::hle::RegisterJsonExports(registry, runtime.json_values()) ==
                ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 16,
        "duplicate JSON registration changed the registry");

  ExportRegistry conflict_registry;
  Check(conflict_registry.Register(
            kajps5::hle::kJson2LibraryName,
            kajps5::hle::kJsonValueConstructorName,
            [](HleCallContext&) { return HleContextStatus::kOk; }) ==
            ExportRegistryStatus::kOk,
        "JSON conflict setup failed");
  Check(kajps5::hle::RegisterJsonExports(conflict_registry,
                                         runtime.json_values()) ==
                ExportRegistryStatus::kAlreadyExists &&
            conflict_registry.size() == 1,
        "failed JSON registration changed the registry");

  kajps5::kernel::JsonValueService bounded;
  for (std::size_t index = 0;
       index < kajps5::kernel::kMaximumJsonValueShadows; ++index) {
    Check(bounded.Construct(index + 1) == KernelStatus::kOk,
          "bounded JSON shadow setup failed");
  }
  Check(bounded.Construct(
            kajps5::kernel::kMaximumJsonValueShadows + 1) ==
                KernelStatus::kNoResources &&
            bounded.size() == kajps5::kernel::kMaximumJsonValueShadows,
        "JSON shadow capacity limit was not enforced");

  return failures == 0 ? 0 : 1;
}
