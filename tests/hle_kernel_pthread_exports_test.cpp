// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/kernel_pthread_exports.h"
#include "kernel/pthread.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_pthread_exports_test: " << message << '\n';
    ++failures;
  }
}

std::uint64_t KernelResult(std::int32_t result) {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
}

std::uint64_t Dispatch(kajps5::hle::ExportRegistry& registry,
                       std::string_view symbol,
                       kajps5::hle::HleCallContext& context) {
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(static_cast<bool>(result), "pthread export dispatch failed");
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
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
  Check(kajps5::hle::RegisterKernelPthreadExports(
            registry, runtime.pthreads(), runtime.scheduler()) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 44,
        "pthread exports did not register atomically");

  GuestMemory memory(0x1000, 0x4000);
  HleCallContext attr_init(memory);
  Check(attr_init.SetRegister(HleRegister::kRdi, 0x1100) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadAttrInitNid,
                     attr_init) == 0,
        "pthread attribute initialization failed");
  std::uint64_t attribute_handle = 0;
  Check(attr_init.ReadUInt64(0x1100, attribute_handle) ==
            HleContextStatus::kOk &&
            attribute_handle != 0 && runtime.pthreads().attribute_count() == 1,
        "pthread attribute initialization did not write a guest handle");
  const auto default_attribute =
      runtime.pthreads().GetAttribute(attribute_handle);
  Check(default_attribute &&
            default_attribute->affinity_mask ==
                kajps5::kernel::kPthreadDefaultAffinityMask &&
            default_attribute->stack_size ==
                kajps5::kernel::kPthreadDefaultStackSize &&
            default_attribute->guard_size ==
                kajps5::kernel::kPthreadDefaultGuardSize &&
            default_attribute->priority ==
                kajps5::kernel::kPthreadDefaultPriority,
        "pthread attribute defaults do not match the guest ABI");

  HleCallContext get_stack(memory);
  Check(get_stack.SetRegister(HleRegister::kRdi, 0x1100) &&
            get_stack.SetRegister(HleRegister::kRsi, 0x1110) &&
            Dispatch(registry,
                     kajps5::hle::kPosixPthreadAttrGetstacksizeNid,
                     get_stack) == 0,
        "pthread default stack-size query failed");
  std::uint64_t stack_size = 0;
  Check(get_stack.ReadUInt64(0x1110, stack_size) == HleContextStatus::kOk &&
            stack_size == kajps5::kernel::kPthreadDefaultStackSize,
        "pthread default stack-size query returned the wrong value");

  HleCallContext small_stack(memory);
  Check(small_stack.SetRegister(HleRegister::kRdi, 0x1100) &&
            small_stack.SetRegister(HleRegister::kRsi, 0x2000) &&
            Dispatch(registry,
                     kajps5::hle::kPosixPthreadAttrSetstacksizeNid,
                     small_stack) == 22 &&
            runtime.pthreads().GetAttribute(attribute_handle)->stack_size ==
                kajps5::kernel::kPthreadDefaultStackSize,
        "pthread accepted a stack smaller than the guest minimum");

  HleCallContext set_stack(memory);
  Check(set_stack.SetRegister(HleRegister::kRdi, 0x1100) &&
            set_stack.SetRegister(HleRegister::kRsi, 0x180000) &&
            Dispatch(registry,
                     kajps5::hle::kKernelPthreadAttrSetstacksizeNid,
                     set_stack) == 0 &&
            runtime.pthreads().GetAttribute(attribute_handle)->stack_size ==
                0x180000,
        "pthread stack-size update failed");

  HleCallContext attr_fault(memory);
  Check(attr_fault.SetRegister(HleRegister::kRdi, 0x800) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadAttrInitNid,
                     attr_fault) == 14 &&
            runtime.pthreads().attribute_count() == 1,
        "invalid pthread attribute output changed service state");

  const auto main_thread = runtime.scheduler().CreateThread("main", 700);
  const auto worker_thread = runtime.scheduler().CreateThread("worker", 700);
  Check(main_thread && worker_thread &&
            runtime.scheduler().SelectNext() == main_thread.handle,
        "pthread scheduler setup failed");

  HleCallContext self(memory);
  Check(Dispatch(registry, kajps5::hle::kPosixPthreadSelfNid, self) ==
            main_thread.handle,
        "pthread_self returned the wrong guest thread");

  HleCallContext equal(memory);
  Check(equal.SetRegister(HleRegister::kRdi, main_thread.handle) &&
            equal.SetRegister(HleRegister::kRsi, main_thread.handle) &&
            Dispatch(registry, kajps5::hle::kKernelPthreadEqualNid, equal) == 1,
        "pthread_equal rejected equal handles");
  HleCallContext not_equal(memory);
  Check(not_equal.SetRegister(HleRegister::kRdi, main_thread.handle) &&
            not_equal.SetRegister(HleRegister::kRsi, worker_thread.handle) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadEqualNid,
                     not_equal) == 0,
        "pthread_equal accepted different handles");

  HleCallContext key_create(memory);
  Check(key_create.SetRegister(HleRegister::kRdi, 0x1120) &&
            key_create.SetRegister(HleRegister::kRsi, 0x12345678) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadKeyCreateNid,
                     key_create) == 0,
        "pthread TLS key creation failed");
  std::uint32_t key = 256;
  Check(key_create.ReadUInt32(0x1120, key) == HleContextStatus::kOk &&
            key == 0 && runtime.pthreads().key_count() == 1,
        "pthread TLS key creation returned the wrong key");

  HleCallContext set_main_value(memory);
  Check(set_main_value.SetRegister(HleRegister::kRdi, key) &&
            set_main_value.SetRegister(HleRegister::kRsi, 0xcafe) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadSetspecificNid,
                     set_main_value) == 0,
        "pthread_setspecific failed for the main guest thread");
  HleCallContext get_main_value(memory);
  Check(get_main_value.SetRegister(HleRegister::kRdi, key) &&
            Dispatch(registry, kajps5::hle::kKernelPthreadGetspecificNid,
                     get_main_value) == 0xcafe,
        "pthread_getspecific lost the main guest thread value");

  HleCallContext yield(memory);
  Check(Dispatch(registry, kajps5::hle::kPosixPthreadYieldNid, yield) == 0 &&
            runtime.scheduler().SelectNext() == worker_thread.handle,
        "pthread_yield did not return control to the guest scheduler");
  HleCallContext get_worker_value(memory);
  Check(get_worker_value.SetRegister(HleRegister::kRdi, key) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadGetspecificNid,
                     get_worker_value) == 0,
        "pthread TLS value leaked between guest threads");

  HleCallContext delete_key(memory);
  Check(delete_key.SetRegister(HleRegister::kRdi, key) &&
            Dispatch(registry, kajps5::hle::kKernelPthreadKeyDeleteNid,
                     delete_key) == 0 &&
            runtime.pthreads().key_count() == 0,
        "pthread TLS key deletion failed");
  HleCallContext stale_key(memory);
  Check(stale_key.SetRegister(HleRegister::kRdi, key) &&
            stale_key.SetRegister(HleRegister::kRsi, 1) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadSetspecificNid,
                     stale_key) == 22,
        "pthread accepted a deleted TLS key");

  for (std::size_t index = 0; index <
                              kajps5::kernel::kMaximumPthreadKeys;
       ++index) {
    Check(static_cast<bool>(runtime.pthreads().CreateKey(0)),
          "pthread TLS key table exhausted too early");
  }
  Check(runtime.pthreads().CreateKey(0).status == KernelStatus::kNoResources,
        "pthread TLS key table exceeded its guest limit");

  HleCallContext attr_destroy(memory);
  Check(attr_destroy.SetRegister(HleRegister::kRdi, 0x1100) &&
            Dispatch(registry, kajps5::hle::kPosixPthreadAttrDestroyNid,
                     attr_destroy) == 0 &&
            runtime.pthreads().attribute_count() == 0 &&
            attr_destroy.ReadUInt64(0x1100, attribute_handle) ==
                HleContextStatus::kOk &&
            attribute_handle == 0,
        "pthread attribute destruction did not clear the guest handle");

  HleCallContext stale_attr(memory);
  Check(stale_attr.SetRegister(HleRegister::kRdi, 0x1100) &&
            Dispatch(registry, kajps5::hle::kKernelPthreadAttrDestroyNid,
                     stale_attr) ==
                KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "pthread accepted an already destroyed attribute");

  return failures == 0 ? 0 : 1;
}
