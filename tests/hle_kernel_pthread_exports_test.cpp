// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <iostream>
#include <memory>
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

class PthreadClockSource final : public kajps5::kernel::KernelClockSource {
 public:
  [[nodiscard]] std::int64_t RealtimeNanoseconds() const override {
    return realtime_nanoseconds;
  }

  [[nodiscard]] std::uint64_t MonotonicNanoseconds() const override {
    return monotonic_nanoseconds;
  }

  std::int64_t realtime_nanoseconds = 0;
  std::uint64_t monotonic_nanoseconds = 0;
};

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
            registry.size() == 120,
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

  KernelRuntime lifecycle_runtime;
  ExportRegistry lifecycle_registry;
  Check(kajps5::hle::RegisterKernelPthreadExports(
            lifecycle_registry, lifecycle_runtime.pthreads(),
            lifecycle_runtime.scheduler()) == ExportRegistryStatus::kOk,
        "pthread lifecycle exports did not register");
  const auto lifecycle_main =
      lifecycle_runtime.scheduler().CreateThread("main", 700);
  Check(lifecycle_main &&
            lifecycle_runtime.scheduler().SelectNext() ==
                lifecycle_main.handle,
        "pthread lifecycle scheduler setup failed");

  HleCallContext lifecycle_attr(memory);
  Check(lifecycle_attr.SetRegister(HleRegister::kRdi, 0x1200) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kKernelPthreadAttrInitNid,
                     lifecycle_attr) == 0,
        "pthread lifecycle attribute setup failed");
  HleCallContext lifecycle_stack(memory);
  Check(lifecycle_stack.SetRegister(HleRegister::kRdi, 0x1200) &&
            lifecycle_stack.SetRegister(HleRegister::kRsi, 0x180000) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kPosixPthreadAttrSetstacksizeNid,
                     lifecycle_stack) == 0,
        "pthread lifecycle stack setup failed");

  HleCallContext create(memory);
  Check(create.SetRegister(HleRegister::kRdi, 0x1210) &&
            create.SetRegister(HleRegister::kRsi, 0x1200) &&
            create.SetRegister(HleRegister::kRdx, 0x400000) &&
            create.SetRegister(HleRegister::kRcx, 0xbeef) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kPosixPthreadCreateNid, create) == 0,
        "pthread_create failed");
  std::uint64_t created_handle = 0;
  Check(create.ReadUInt64(0x1210, created_handle) == HleContextStatus::kOk &&
            created_handle != 0,
        "pthread_create did not write a guest thread handle");
  const auto created_scheduler_thread =
      lifecycle_runtime.scheduler().Snapshot(created_handle);
  const auto created_pthread =
      lifecycle_runtime.pthreads().GetThread(created_handle);
  Check(created_scheduler_thread && created_pthread &&
            created_scheduler_thread->entry_address == 0x400000 &&
            created_scheduler_thread->argument == 0xbeef &&
            created_pthread->attributes.stack_size == 0x180000,
        "pthread_create lost its entry, argument, or attributes");

  const auto thread_count_before_fault =
      lifecycle_runtime.scheduler().SnapshotAll().size();
  HleCallContext create_fault(memory);
  Check(create_fault.SetRegister(HleRegister::kRdi, 0x800) &&
            create_fault.SetRegister(HleRegister::kRdx, 0x500000) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kPosixPthreadCreateNid,
                     create_fault) == 14 &&
            lifecycle_runtime.scheduler().SnapshotAll().size() ==
                thread_count_before_fault,
        "invalid pthread_create output changed scheduler state");
  HleCallContext create_without_entry(memory);
  Check(create_without_entry.SetRegister(HleRegister::kRdi, 0x1230) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kPosixPthreadCreateNid,
                     create_without_entry) == 22 &&
            lifecycle_runtime.scheduler().SnapshotAll().size() ==
                thread_count_before_fault,
        "pthread_create accepted a null entry point");

  HleCallContext first_join(memory);
  const auto first_join_result =
      first_join.SetRegister(HleRegister::kRdi, created_handle) &&
      first_join.SetRegister(HleRegister::kRsi, 0x1220) &&
      Dispatch(lifecycle_registry, kajps5::hle::kPosixPthreadJoinNid,
               first_join) == 16;
  const auto blocked_main =
      lifecycle_runtime.scheduler().Snapshot(lifecycle_main.handle);
  Check(first_join_result && blocked_main &&
            blocked_main->state == kajps5::kernel::GuestThreadState::kBlocked,
        "pthread_join did not block a live joiner");
  Check(lifecycle_runtime.scheduler().SelectNext() == created_handle,
        "pthread-created worker was not ready to run");

  HleCallContext exit(memory);
  Check(exit.SetRegister(HleRegister::kRdi, 0x77),
        "pthread_exit setup failed");
  const std::vector<std::string> posix_libraries = {
      kajps5::hle::kLibScePosixName};
  const auto exit_dispatched = lifecycle_registry.Dispatch(
      kajps5::hle::kPosixPthreadExitNid, posix_libraries, exit);
  const auto exited_worker =
      lifecycle_runtime.scheduler().Snapshot(created_handle);
  Check(exit_dispatched &&
            exit.GetRegister(HleRegister::kRax).value_or(0) == 0x77 &&
            exited_worker &&
            exited_worker->state == kajps5::kernel::GuestThreadState::kExited,
        "pthread_exit did not save the guest return value");
  Check(lifecycle_runtime.scheduler().SelectNext() == lifecycle_main.handle,
        "pthread_exit did not wake its joiner");

  HleCallContext completed_join(memory);
  Check(completed_join.SetRegister(HleRegister::kRdi, created_handle) &&
            completed_join.SetRegister(HleRegister::kRsi, 0x1220) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kKernelPthreadJoinNid,
                     completed_join) == 0,
        "pthread_join did not complete after thread exit");
  std::uint64_t joined_value = 0;
  Check(completed_join.ReadUInt64(0x1220, joined_value) ==
            HleContextStatus::kOk &&
            joined_value == 0x77,
        "pthread_join wrote the wrong thread return value");

  HleCallContext self_join(memory);
  Check(self_join.SetRegister(HleRegister::kRdi, lifecycle_main.handle) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kPosixPthreadJoinNid, self_join) == 11,
        "pthread_join accepted a self join");
  HleCallContext stale_join(memory);
  Check(stale_join.SetRegister(HleRegister::kRdi, 9999) &&
            Dispatch(lifecycle_registry,
                     kajps5::hle::kPosixPthreadJoinNid, stale_join) == 3,
        "pthread_join returned the wrong stale-thread error");

  KernelRuntime mutex_runtime;
  ExportRegistry mutex_registry;
  Check(kajps5::hle::RegisterKernelPthreadExports(
            mutex_registry, mutex_runtime.pthreads(),
            mutex_runtime.scheduler()) == ExportRegistryStatus::kOk,
        "pthread mutex exports did not register");
  const auto mutex_owner =
      mutex_runtime.scheduler().CreateThread("mutex-owner", 700);
  const auto mutex_waiter =
      mutex_runtime.scheduler().CreateThread("mutex-waiter", 700);
  Check(mutex_owner && mutex_waiter &&
            mutex_runtime.scheduler().SelectNext() == mutex_owner.handle,
        "pthread mutex scheduler setup failed");

  HleCallContext mutex_attr_init(memory);
  Check(mutex_attr_init.SetRegister(HleRegister::kRdi, 0x1300) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexattrInitNid,
                     mutex_attr_init) == 0,
        "pthread mutex attribute initialization failed");
  HleCallContext mutex_attr_type(memory);
  Check(mutex_attr_type.SetRegister(HleRegister::kRdi, 0x1300) &&
            mutex_attr_type.SetRegister(
                HleRegister::kRsi,
                kajps5::kernel::kPthreadMutexRecursive) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kKernelPthreadMutexattrSettypeNid,
                     mutex_attr_type) == 0,
        "pthread mutex type setup failed");
  HleCallContext invalid_mutex_protocol(memory);
  Check(invalid_mutex_protocol.SetRegister(HleRegister::kRdi, 0x1300) &&
            invalid_mutex_protocol.SetRegister(HleRegister::kRsi, 3) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexattrSetprotocolNid,
                     invalid_mutex_protocol) == 22,
        "pthread accepted an invalid mutex protocol");

  HleCallContext mutex_init(memory);
  Check(mutex_init.SetRegister(HleRegister::kRdi, 0x1310) &&
            mutex_init.SetRegister(HleRegister::kRsi, 0x1300) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexInitNid,
                     mutex_init) == 0,
        "pthread mutex initialization failed");
  std::uint64_t mutex_handle = 0;
  const auto mutex_handle_read =
      mutex_init.ReadUInt64(0x1310, mutex_handle) == HleContextStatus::kOk;
  const auto initialized_mutex =
      mutex_runtime.pthreads().GetMutex(mutex_handle);
  Check(mutex_handle_read && mutex_handle != 0 && initialized_mutex &&
            initialized_mutex->type ==
                kajps5::kernel::kPthreadMutexRecursive,
        "pthread mutex did not use its guest attribute");

  HleCallContext mutex_lock(memory);
  Check(mutex_lock.SetRegister(HleRegister::kRdi, 0x1310) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     mutex_lock) == 0,
        "pthread mutex lock failed");
  HleCallContext mutex_relock(memory);
  const auto relock_result =
      mutex_relock.SetRegister(HleRegister::kRdi, 0x1310) &&
      Dispatch(mutex_registry, kajps5::hle::kKernelPthreadMutexLockNid,
               mutex_relock) == 0;
  const auto relocked_mutex = mutex_runtime.pthreads().GetMutex(mutex_handle);
  Check(relock_result && relocked_mutex &&
            relocked_mutex->recursion_count == 2,
        "recursive pthread mutex lock failed");
  HleCallContext busy_mutex_destroy(memory);
  Check(busy_mutex_destroy.SetRegister(HleRegister::kRdi, 0x1310) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexDestroyNid,
                     busy_mutex_destroy) == 16,
        "pthread destroyed an owned mutex");
  for (int index = 0; index < 2; ++index) {
    HleCallContext mutex_unlock(memory);
    Check(mutex_unlock.SetRegister(HleRegister::kRdi, 0x1310) &&
              Dispatch(mutex_registry,
                       kajps5::hle::kPosixPthreadMutexUnlockNid,
                       mutex_unlock) == 0,
          "recursive pthread mutex unlock failed");
  }
  HleCallContext mutex_destroy(memory);
  Check(mutex_destroy.SetRegister(HleRegister::kRdi, 0x1310) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kKernelPthreadMutexDestroyNid,
                     mutex_destroy) == 0 &&
            mutex_destroy.ReadUInt64(0x1310, mutex_handle) ==
                HleContextStatus::kOk &&
            mutex_handle == 0,
        "pthread mutex destruction did not clear its guest handle");

  HleCallContext static_setup(memory);
  Check(static_setup.WriteUInt64(0x1320, 0) == HleContextStatus::kOk,
        "static pthread mutex setup failed");
  HleCallContext static_lock(memory);
  Check(static_lock.SetRegister(HleRegister::kRdi, 0x1320) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     static_lock) == 0 &&
            static_lock.ReadUInt64(0x1320, mutex_handle) ==
                HleContextStatus::kOk &&
            mutex_handle != 0,
        "static pthread mutex was not initialized on first use");
  HleCallContext static_relock(memory);
  Check(static_relock.SetRegister(HleRegister::kRdi, 0x1320) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     static_relock) == 11,
        "error-checking pthread mutex did not reject a self lock");
  Check(mutex_runtime.scheduler().YieldCurrent() &&
            mutex_runtime.scheduler().SelectNext() == mutex_waiter.handle,
        "pthread mutex contention setup failed");
  HleCallContext blocked_lock(memory);
  Check(blocked_lock.SetRegister(HleRegister::kRdi, 0x1320) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     blocked_lock) == 16,
        "contended pthread mutex did not block");
  Check(mutex_runtime.scheduler().SelectNext() == mutex_owner.handle,
        "pthread mutex owner did not resume");
  HleCallContext owner_unlock(memory);
  Check(owner_unlock.SetRegister(HleRegister::kRdi, 0x1320) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kKernelPthreadMutexUnlockNid,
                     owner_unlock) == 0 &&
            mutex_runtime.pthreads().ExitCurrent(0),
        "pthread mutex owner did not wake its waiter");
  Check(mutex_runtime.scheduler().SelectNext() == mutex_waiter.handle,
        "pthread mutex waiter was not woken");
  HleCallContext resumed_lock(memory);
  Check(resumed_lock.SetRegister(HleRegister::kRdi, 0x1320) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     resumed_lock) == 0,
        "woken pthread mutex call did not complete");
  HleCallContext waiter_unlock(memory);
  Check(waiter_unlock.SetRegister(HleRegister::kRdi, 0x1320) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexUnlockNid,
                     waiter_unlock) == 0,
        "pthread mutex waiter could not unlock");

  HleCallContext adaptive_setup(memory);
  Check(adaptive_setup.WriteUInt64(0x1330, 1) == HleContextStatus::kOk,
        "adaptive pthread mutex setup failed");
  HleCallContext adaptive_lock(memory);
  Check(adaptive_lock.SetRegister(HleRegister::kRdi, 0x1330) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kKernelPthreadMutexLockNid,
                     adaptive_lock) == 0,
        "adaptive pthread mutex lock failed");
  HleCallContext adaptive_relock(memory);
  Check(adaptive_relock.SetRegister(HleRegister::kRdi, 0x1330) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kKernelPthreadMutexLockNid,
                     adaptive_relock) == 0,
        "adaptive pthread mutex duplicate lock was not idempotent");
  HleCallContext adaptive_trylock(memory);
  Check(adaptive_trylock.SetRegister(HleRegister::kRdi, 0x1330) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kPosixPthreadMutexTrylockNid,
                     adaptive_trylock) == 16,
        "adaptive pthread mutex trylock ignored ownership");
  HleCallContext adaptive_unlock(memory);
  Check(adaptive_unlock.SetRegister(HleRegister::kRdi, 0x1330) &&
            Dispatch(mutex_registry,
                     kajps5::hle::kKernelPthreadMutexUnlockNid,
                     adaptive_unlock) == 0,
        "adaptive pthread mutex needed more than one matching unlock");

  KernelRuntime condition_runtime;
  ExportRegistry condition_registry;
  Check(kajps5::hle::RegisterKernelPthreadExports(
            condition_registry, condition_runtime.pthreads(),
            condition_runtime.scheduler()) == ExportRegistryStatus::kOk,
        "pthread condition exports did not register");
  const auto condition_waiter =
      condition_runtime.scheduler().CreateThread("condition-waiter", 700);
  const auto condition_signaler =
      condition_runtime.scheduler().CreateThread("condition-signaler", 700);
  Check(condition_waiter && condition_signaler &&
            condition_runtime.scheduler().SelectNext() ==
                condition_waiter.handle,
        "pthread condition scheduler setup failed");

  HleCallContext condition_init(memory);
  Check(condition_init.SetRegister(HleRegister::kRdi, 0x1400) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadCondInitNid,
                     condition_init) == 0,
        "pthread condition initialization failed");
  std::uint64_t condition_handle = 0;
  Check(condition_init.ReadUInt64(0x1400, condition_handle) ==
                HleContextStatus::kOk &&
            condition_handle != 0,
        "pthread condition initialization did not write a guest handle");
  HleCallContext condition_mutex_setup(memory);
  Check(condition_mutex_setup.WriteUInt64(0x1410, 0) ==
            HleContextStatus::kOk,
        "pthread condition mutex setup failed");
  HleCallContext condition_mutex_lock(memory);
  Check(condition_mutex_lock.SetRegister(HleRegister::kRdi, 0x1410) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     condition_mutex_lock) == 0,
        "pthread condition mutex lock failed");
  HleCallContext condition_wait(memory);
  Check(condition_wait.SetRegister(HleRegister::kRdi, 0x1400) &&
            condition_wait.SetRegister(HleRegister::kRsi, 0x1410) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadCondWaitNid,
                     condition_wait) == 16,
        "pthread condition wait did not block");
  const auto hle_waiting_condition =
      condition_runtime.pthreads().GetCondition(condition_handle);
  Check(hle_waiting_condition &&
            hle_waiting_condition->waiting_count == 1 &&
            hle_waiting_condition->active_waiter_count == 1 &&
            condition_runtime.scheduler().SelectNext() ==
                condition_signaler.handle,
        "pthread condition wait did not enter the scheduler");
  HleCallContext busy_condition_destroy(memory);
  Check(busy_condition_destroy.SetRegister(HleRegister::kRdi, 0x1400) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadCondDestroyNid,
                     busy_condition_destroy) == 16,
        "pthread destroyed a condition with an active waiter");
  HleCallContext busy_condition_mutex_destroy(memory);
  Check(busy_condition_mutex_destroy.SetRegister(HleRegister::kRdi, 0x1410) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadMutexDestroyNid,
                     busy_condition_mutex_destroy) == 16,
        "pthread destroyed a mutex used by a condition wait");
  HleCallContext condition_signal(memory);
  Check(condition_signal.SetRegister(HleRegister::kRdi, 0x1400) &&
            Dispatch(condition_registry,
                     kajps5::hle::kKernelPthreadCondSignalNid,
                     condition_signal) == 0 &&
            condition_runtime.pthreads().ExitCurrent(0) &&
            condition_runtime.scheduler().SelectNext() ==
                condition_waiter.handle,
        "pthread condition signal did not wake its waiter");
  HleCallContext resumed_condition_wait(memory);
  Check(resumed_condition_wait.SetRegister(HleRegister::kRdi, 0x1400) &&
            resumed_condition_wait.SetRegister(HleRegister::kRsi, 0x1410) &&
            Dispatch(condition_registry,
                     kajps5::hle::kKernelPthreadCondWaitNid,
                     resumed_condition_wait) == 0,
        "pthread condition waiter did not reacquire its mutex");
  HleCallContext condition_mutex_unlock(memory);
  Check(condition_mutex_unlock.SetRegister(HleRegister::kRdi, 0x1410) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadMutexUnlockNid,
                     condition_mutex_unlock) == 0,
        "pthread condition waiter could not unlock its mutex");
  HleCallContext condition_destroy(memory);
  Check(condition_destroy.SetRegister(HleRegister::kRdi, 0x1400) &&
            Dispatch(condition_registry,
                     kajps5::hle::kKernelPthreadCondDestroyNid,
                     condition_destroy) == 0 &&
            condition_destroy.ReadUInt64(0x1400, condition_handle) ==
                HleContextStatus::kOk &&
            condition_handle == 0,
        "pthread condition destruction did not clear its guest handle");
  HleCallContext condition_mutex_destroy(memory);
  Check(condition_mutex_destroy.SetRegister(HleRegister::kRdi, 0x1410) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadMutexDestroyNid,
                     condition_mutex_destroy) == 0,
        "pthread condition mutex destruction failed");

  HleCallContext static_condition_setup(memory);
  Check(static_condition_setup.WriteUInt64(0x1420, 0) ==
            HleContextStatus::kOk,
        "static pthread condition setup failed");
  HleCallContext static_condition_broadcast(memory);
  Check(static_condition_broadcast.SetRegister(HleRegister::kRdi, 0x1420) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadCondBroadcastNid,
                     static_condition_broadcast) == 0 &&
            static_condition_broadcast.ReadUInt64(0x1420, condition_handle) ==
                HleContextStatus::kOk &&
            condition_handle != 0,
        "static pthread condition was not initialized on first use");
  HleCallContext static_condition_destroy(memory);
  Check(static_condition_destroy.SetRegister(HleRegister::kRdi, 0x1420) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadCondDestroyNid,
                     static_condition_destroy) == 0,
        "static pthread condition destruction failed");
  HleCallContext condition_fault(memory);
  Check(condition_fault.SetRegister(HleRegister::kRdi, 0x800) &&
            Dispatch(condition_registry,
                     kajps5::hle::kPosixPthreadCondInitNid,
                     condition_fault) == 14,
        "invalid pthread condition output changed service state");

  auto timed_source = std::make_unique<PthreadClockSource>();
  auto* timed_source_view = timed_source.get();
  timed_source_view->realtime_nanoseconds = 100'000'000'000;
  timed_source_view->monotonic_nanoseconds = 1'000'000;
  KernelRuntime timed_runtime(std::move(timed_source));
  ExportRegistry timed_registry;
  Check(kajps5::hle::RegisterKernelPthreadExports(
            timed_registry, timed_runtime.pthreads(),
            timed_runtime.scheduler()) == ExportRegistryStatus::kOk,
        "pthread timed condition exports did not register");
  const auto timed_waiter =
      timed_runtime.scheduler().CreateThread("timed-waiter", 700);
  Check(timed_waiter && timed_runtime.scheduler().SelectNext() ==
                              timed_waiter.handle,
        "pthread timed condition scheduler setup failed");
  HleCallContext static_timed_setup(memory);
  Check(static_timed_setup.WriteUInt64(0x1540, 0) == HleContextStatus::kOk &&
            static_timed_setup.WriteUInt64(0x1550, 0) == HleContextStatus::kOk,
        "static pthread timed condition setup failed");
  HleCallContext static_timed_fault(memory);
  std::uint64_t unchanged_condition = 1;
  std::uint64_t unchanged_mutex = 1;
  Check(static_timed_fault.SetRegister(HleRegister::kRdi, 0x1540) &&
            static_timed_fault.SetRegister(HleRegister::kRsi, 0x1550) &&
            static_timed_fault.SetRegister(HleRegister::kRdx, 0x800) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondTimedwaitNid,
                     static_timed_fault) == 14 &&
            static_timed_fault.ReadUInt64(0x1540, unchanged_condition) ==
                HleContextStatus::kOk &&
            static_timed_fault.ReadUInt64(0x1550, unchanged_mutex) ==
                HleContextStatus::kOk &&
            unchanged_condition == 0 && unchanged_mutex == 0,
        "bad pthread deadline initialized static wait objects");
  HleCallContext timed_condition_init(memory);
  Check(timed_condition_init.SetRegister(HleRegister::kRdi, 0x1500) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondInitNid,
                     timed_condition_init) == 0,
        "pthread timed condition initialization failed");
  HleCallContext timed_mutex_setup(memory);
  Check(timed_mutex_setup.WriteUInt64(0x1510, 0) == HleContextStatus::kOk,
        "pthread timed condition mutex setup failed");
  HleCallContext timed_mutex_lock(memory);
  Check(timed_mutex_lock.SetRegister(HleRegister::kRdi, 0x1510) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     timed_mutex_lock) == 0,
        "pthread timed condition mutex lock failed");
  HleCallContext deadline_setup(memory);
  Check(deadline_setup.WriteUInt64(0x1520, 100) == HleContextStatus::kOk &&
            deadline_setup.WriteUInt64(0x1528, 5'000) ==
                HleContextStatus::kOk,
        "pthread absolute deadline setup failed");
  HleCallContext absolute_timed_wait(memory);
  Check(absolute_timed_wait.SetRegister(HleRegister::kRdi, 0x1500) &&
            absolute_timed_wait.SetRegister(HleRegister::kRsi, 0x1510) &&
            absolute_timed_wait.SetRegister(HleRegister::kRdx, 0x1520) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondTimedwaitNid,
                     absolute_timed_wait) == 16,
        "pthread absolute condition wait did not block");
  Check(!timed_runtime.scheduler().SelectNext(),
        "pthread absolute condition wait woke before its deadline");
  timed_source_view->monotonic_nanoseconds = 1'005'000;
  Check(timed_runtime.scheduler().SelectNext() == timed_waiter.handle &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondTimedwaitNid,
                     absolute_timed_wait) == 60,
        "pthread absolute condition wait returned the wrong timeout");

  HleCallContext timed_mutex_unlock(memory);
  Check(timed_mutex_unlock.SetRegister(HleRegister::kRdi, 0x1510) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadMutexUnlockNid,
                     timed_mutex_unlock) == 0 &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadMutexLockNid,
                     timed_mutex_lock) == 0,
        "pthread timed condition mutex could not be relocked");
  Check(deadline_setup.WriteUInt64(0x1528, 1'000'000'000) ==
            HleContextStatus::kOk,
        "invalid pthread deadline setup failed");
  HleCallContext invalid_timed_wait(memory);
  Check(invalid_timed_wait.SetRegister(HleRegister::kRdi, 0x1500) &&
            invalid_timed_wait.SetRegister(HleRegister::kRsi, 0x1510) &&
            invalid_timed_wait.SetRegister(HleRegister::kRdx, 0x1520) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondTimedwaitNid,
                     invalid_timed_wait) == 22,
        "pthread accepted an invalid absolute condition deadline");
  HleCallContext faulted_timed_wait(memory);
  Check(faulted_timed_wait.SetRegister(HleRegister::kRdi, 0x1500) &&
            faulted_timed_wait.SetRegister(HleRegister::kRsi, 0x1510) &&
            faulted_timed_wait.SetRegister(HleRegister::kRdx, 0x800) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondTimedwaitNid,
                     faulted_timed_wait) == 14,
        "pthread accepted an unreadable absolute condition deadline");

  HleCallContext relative_timed_wait(memory);
  Check(relative_timed_wait.SetRegister(HleRegister::kRdi, 0x1500) &&
            relative_timed_wait.SetRegister(HleRegister::kRsi, 0x1510) &&
            relative_timed_wait.SetRegister(HleRegister::kRdx, 10) &&
            Dispatch(timed_registry,
                     kajps5::hle::kKernelPthreadCondTimedwaitNid,
                     relative_timed_wait) ==
                KernelResult(kajps5::hle::kKernelHleErrorBusy),
        "scePthread relative condition wait did not block");
  timed_source_view->monotonic_nanoseconds += 10'000;
  Check(timed_runtime.scheduler().SelectNext() == timed_waiter.handle &&
            Dispatch(timed_registry,
                     kajps5::hle::kKernelPthreadCondTimedwaitNid,
                     relative_timed_wait) ==
                KernelResult(kajps5::hle::kKernelHleErrorTimedOut),
        "scePthread relative condition wait returned the wrong timeout");
  Check(Dispatch(timed_registry,
                 kajps5::hle::kPosixPthreadMutexUnlockNid,
                 timed_mutex_unlock) == 0,
        "pthread timed-out waiter did not own its mutex");
  HleCallContext timed_condition_destroy(memory);
  Check(timed_condition_destroy.SetRegister(HleRegister::kRdi, 0x1500) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadCondDestroyNid,
                     timed_condition_destroy) == 0,
        "pthread timed condition destruction failed");
  HleCallContext timed_mutex_destroy(memory);
  Check(timed_mutex_destroy.SetRegister(HleRegister::kRdi, 0x1510) &&
            Dispatch(timed_registry,
                     kajps5::hle::kPosixPthreadMutexDestroyNid,
                     timed_mutex_destroy) == 0,
        "pthread timed condition mutex destruction failed");

  return failures == 0 ? 0 : 1;
}
