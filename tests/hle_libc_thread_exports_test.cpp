// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/libc_exports.h"
#include "hle/libc_thread_exports.h"
#include "kernel/pthread.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_libc_thread_exports_test: " << message << '\n';
    ++failures;
  }
}

struct CallResult {
  kajps5::hle::HleContextStatus status = kajps5::hle::HleContextStatus::kOk;
  std::uint64_t value = 0;
};

CallResult Call(kajps5::hle::ExportRegistry& registry,
                kajps5::memory::GuestMemory& memory, std::string_view symbol,
                std::uint64_t argument0, std::uint64_t argument1 = 0,
                std::uint64_t argument2 = 0) {
  kajps5::hle::HleCallContext context(memory);
  Check(context.SetRegister(kajps5::hle::HleRegister::kRdi, argument0) &&
            context.SetRegister(kajps5::hle::HleRegister::kRsi, argument1) &&
            context.SetRegister(kajps5::hle::HleRegister::kRdx, argument2),
        "libc thread argument setup failed");
  const std::vector<std::string> libraries = {kajps5::hle::kLibcName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(result.status == kajps5::hle::ExportRegistryStatus::kOk,
        "libc thread export lookup failed");
  return {result.handler_status,
          context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0)};
}

bool ReadUInt64(const kajps5::memory::GuestMemory& memory,
                std::uint64_t address, std::uint64_t& value) {
  std::array<std::byte, sizeof(value)> bytes{};
  if (!memory.Read(address, bytes)) {
    return false;
  }
  value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return true;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleContextStatus;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;

  KernelRuntime runtime;
  const auto thread = runtime.scheduler().CreateThread("main", 0);
  Check(thread && runtime.scheduler().SelectNext() == thread.handle,
        "libc thread test did not start a guest thread");

  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcThreadExports(registry, runtime.pthreads()) ==
                ExportRegistryStatus::kOk &&
            registry.size() == 18,
        "libc thread exports did not register atomically");
  GuestMemory memory(0x1000, 0x2000);

  const auto plain_init =
      Call(registry, memory, kajps5::hle::kLibcMtxInitName, 0x1100, 1);
  std::uint64_t plain_handle = 0;
  const auto read_plain_handle = ReadUInt64(memory, 0x1100, plain_handle);
  const auto plain_mutex = runtime.pthreads().GetMutex(plain_handle);
  Check(plain_init.status == HleContextStatus::kOk && plain_init.value == 0 &&
            read_plain_handle && plain_handle != 0 && plain_mutex &&
            plain_mutex->type == kajps5::kernel::kPthreadMutexErrorCheck,
        "plain C++ mutex initialization failed");
  Check(
      Call(registry, memory, kajps5::hle::kLibcMtxCurrentOwnsNid, 0x1100)
                  .value == 0 &&
          Call(registry, memory, kajps5::hle::kLibcMtxLockNid, 0x1100).value ==
              0 &&
          Call(registry, memory, kajps5::hle::kLibcMtxCurrentOwnsName, 0x1100)
                  .value == 1,
      "C++ mutex ownership was not reported");
  Check(Call(registry, memory, kajps5::hle::kLibcMtxTrylockNid, 0x1100).value ==
            3,
        "owned C++ mutex did not report busy");
  Check(Call(registry, memory, kajps5::hle::kLibcMtxDestroyNid, 0x1100).value ==
                4 &&
            runtime.pthreads().GetMutex(plain_handle).has_value(),
        "locked C++ mutex was destroyed");
  Check(Call(registry, memory, kajps5::hle::kLibcMtxUnlockName, 0x1100).value ==
                0 &&
            Call(registry, memory, kajps5::hle::kLibcMtxTimedlockNid, 0x1100,
                 0xdeadbeef)
                    .value == 0 &&
            Call(registry, memory, kajps5::hle::kLibcMtxUnlockNid, 0x1100)
                    .value == 0 &&
            Call(registry, memory, kajps5::hle::kLibcMtxDestroyName, 0x1100)
                    .value == 0 &&
            !runtime.pthreads().GetMutex(plain_handle).has_value(),
        "plain C++ mutex lifecycle failed");
  std::uint64_t cleared_handle = 1;
  Check(ReadUInt64(memory, 0x1100, cleared_handle) && cleared_handle == 0,
        "destroyed C++ mutex kept its guest handle");

  const auto recursive_init =
      Call(registry, memory, kajps5::hle::kLibcMtxInitNid, 0x1110, 0x100);
  std::uint64_t recursive_handle = 0;
  const auto read_recursive_handle =
      ReadUInt64(memory, 0x1110, recursive_handle);
  const auto recursive_mutex = runtime.pthreads().GetMutex(recursive_handle);
  Check(
      recursive_init.value == 0 && read_recursive_handle && recursive_mutex &&
          recursive_mutex->type == kajps5::kernel::kPthreadMutexRecursive &&
          Call(registry, memory, kajps5::hle::kLibcMtxLockName, 0x1110).value ==
              0 &&
          Call(registry, memory, kajps5::hle::kLibcMtxLockNid, 0x1110).value ==
              0 &&
          runtime.pthreads()
                  .GetMutex(recursive_handle)
                  .value_or(kajps5::kernel::PthreadMutexSnapshot{})
                  .recursion_count == 2 &&
          Call(registry, memory, kajps5::hle::kLibcMtxUnlockNid, 0x1110)
                  .value == 0 &&
          Call(registry, memory, kajps5::hle::kLibcMtxUnlockName, 0x1110)
                  .value == 0 &&
          Call(registry, memory, kajps5::hle::kLibcMtxDestroyNid, 0x1110)
                  .value == 0,
      "recursive C++ mutex lifecycle failed");

  Check(Call(registry, memory, kajps5::hle::kLibcMtxInitWithNameNid, 0x1120, 1,
             0xffffffffffffffff)
                    .value == 0 &&
            Call(registry, memory,
                 kajps5::hle::kLibcMtxInitWithDefaultNameOverrideName, 0x1130,
                 1, 0xffffffffffffffff)
                    .value == 0 &&
            Call(registry, memory, kajps5::hle::kLibcMtxDestroyNid, 0x1120)
                    .value == 0 &&
            Call(registry, memory, kajps5::hle::kLibcMtxDestroyNid, 0x1130)
                    .value == 0,
        "named C++ mutex initialization changed the Kyty contract");

  const auto bad_init = Call(registry, memory, kajps5::hle::kLibcMtxInitNid,
                             memory.end_address() + 8, 1);
  Check(
      bad_init.status == HleContextStatus::kMemoryFault && bad_init.value == 4,
      "invalid C++ mutex output did not report a checked fault");
  Check(kajps5::hle::RegisterLibcThreadExports(registry, runtime.pthreads()) ==
                ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 18,
        "duplicate libc thread registration changed the registry");
  return failures == 0 ? 0 : 1;
}
