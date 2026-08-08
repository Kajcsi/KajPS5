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
#include "hle/libc_exports.h"
#include "kernel/process_lifecycle.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_libc_lifecycle_test: " << message << '\n';
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
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(
      0x1000, 0x1000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite |
          GuestMemoryProtection::kExecute);
  KernelRuntime runtime;
  ExportRegistry registry;
  Check(kajps5::hle::RegisterLibcExports(
            registry, runtime.cxa_guards(), runtime.process_lifecycle(),
            runtime.libc_heap(), memory) == ExportRegistryStatus::kOk &&
            registry.size() == 108,
        "libc lifecycle exports did not register atomically");

  const std::vector<std::string> libc_scope = {kajps5::hle::kLibcName};
  constexpr std::uint64_t kParams = 0x1200;
  HleCallContext environment_setup(memory);
  Check(environment_setup.WriteUInt32(kParams, 1) == HleContextStatus::kOk &&
            environment_setup.WriteUInt64(kParams + 8, 0x1300) ==
                HleContextStatus::kOk &&
            environment_setup.WriteUInt64(kParams + 16, 0) ==
                HleContextStatus::kOk &&
            environment_setup.WriteUInt64(kParams + 24, 0) ==
                HleContextStatus::kOk,
        "environment fixture setup failed");

  HleCallContext init_environment(memory);
  Check(init_environment.SetRegister(HleRegister::kRdi, kParams),
        "environment argument setup failed");
  Check(static_cast<bool>(registry.Dispatch(
            kajps5::hle::kLibcInitEnvNid, libc_scope, init_environment)),
        "checked environment initialization failed");
  const auto environment = runtime.process_lifecycle().environment();
  Check(environment.initialized && environment.argc == 1 &&
            environment.argv_address == kParams + 8 &&
            environment.envp_address == kParams + 24,
        "environment addresses were not preserved");

  HleCallContext invalid_environment(memory);
  Check(invalid_environment.WriteUInt32(kParams, 3) ==
            HleContextStatus::kOk &&
            invalid_environment.SetRegister(HleRegister::kRdi, kParams),
        "invalid environment setup failed");
  const auto invalid_init = registry.Dispatch(
      kajps5::hle::kLibcInitEnvName, libc_scope, invalid_environment);
  Check(invalid_init.handler_status == HleContextStatus::kInvalidArgument &&
            runtime.process_lifecycle().environment().argc == 1,
        "invalid argument count changed the process environment");

  HleCallContext reset_environment(memory);
  Check(registry.Dispatch(kajps5::hle::kLibcInitEnvName, libc_scope,
                          reset_environment) &&
            !runtime.process_lifecycle().environment().initialized,
        "null environment did not reset process arguments");

  HleCallContext first_atexit(memory);
  Check(first_atexit.SetRegister(HleRegister::kRdi, 0x1400) &&
            registry.Dispatch(kajps5::hle::kLibcAtexitNid, libc_scope,
                              first_atexit),
        "first atexit callback registration failed");
  HleCallContext second_atexit(memory);
  Check(second_atexit.SetRegister(HleRegister::kRdi, 0x1500) &&
            registry.Dispatch(kajps5::hle::kLibcAtexitName, libc_scope,
                              second_atexit),
        "second atexit callback registration failed");
  const auto atexit_callbacks =
      runtime.process_lifecycle().PendingAtexitCallbacks();
  Check(atexit_callbacks.size() == 2 && atexit_callbacks[0] == 0x1500 &&
            atexit_callbacks[1] == 0x1400,
        "atexit callbacks did not retain last-in, first-out order");

  HleCallContext cxa_atexit(memory);
  Check(cxa_atexit.SetRegister(HleRegister::kRdi, 0x1600) &&
            cxa_atexit.SetRegister(HleRegister::kRsi, 0x1700) &&
            cxa_atexit.SetRegister(HleRegister::kRdx, 0x1800) &&
            registry.Dispatch(kajps5::hle::kLibcCxaAtexitNid, libc_scope,
                              cxa_atexit),
        "C++ destructor registration failed");
  const auto destructors =
      runtime.process_lifecycle().PendingCxaDestructors(0x1800);
  Check(destructors.size() == 1 && destructors[0].function == 0x1600 &&
            destructors[0].argument == 0x1700 &&
            destructors[0].module == 0x1800,
        "C++ destructor fields were not preserved");

  GuestMemory non_executable(0x3000, 0x100);
  HleCallContext rejected_callback(non_executable);
  Check(rejected_callback.SetRegister(HleRegister::kRdi, 0x3020),
        "non-executable callback setup failed");
  const auto rejected = registry.Dispatch(
      kajps5::hle::kLibcAtexitNid, libc_scope, rejected_callback);
  Check(rejected.handler_status == HleContextStatus::kMemoryFault &&
            rejected_callback.GetRegister(HleRegister::kRax).value_or(0) ==
                1 &&
            runtime.process_lifecycle().callback_count() == 3,
        "non-executable callback changed lifecycle state");

  HleCallContext request_exit(memory);
  Check(request_exit.SetRegister(HleRegister::kRdi, 0xffffffffU),
        "exit status setup failed");
  const auto exit_result = registry.Dispatch(
      kajps5::hle::kLibcExitNid, libc_scope, request_exit);
  const auto exit_request = runtime.process_lifecycle().exit_request();
  Check(exit_result.handler_status == HleContextStatus::kGuestExit &&
            !exit_result && exit_request.requested &&
            exit_request.status == -1,
        "guest exit did not preserve its signed status");

  HleCallContext repeated_exit(memory);
  Check(repeated_exit.SetRegister(HleRegister::kRdi, 7),
        "repeated exit status setup failed");
  Check(registry.Dispatch(kajps5::hle::kLibcCatchReturnFromMainNid,
                          libc_scope, repeated_exit)
                .handler_status == HleContextStatus::kGuestExit &&
            runtime.process_lifecycle().exit_request().status == -1,
        "a later exit request replaced the first status");

  HleCallContext abort(memory);
  const auto abort_result = registry.Dispatch(
      kajps5::hle::kLibcAbortNid, libc_scope, abort);
  Check(abort_result.handler_status == HleContextStatus::kFatalGuestError &&
            !abort.return_written(),
        "abort did not stop at the fatal guest boundary");

  kajps5::kernel::ProcessLifecycleService bounded;
  for (std::size_t index = 0;
       index < kajps5::kernel::kMaximumProcessExitCallbacks; ++index) {
    Check(bounded.RegisterAtexit(index + 1) == KernelStatus::kOk,
          "bounded callback setup failed");
  }
  Check(bounded.RegisterCxaDestructor({0x5000, 0, 0}) ==
                KernelStatus::kNoResources &&
            bounded.callback_count() ==
                kajps5::kernel::kMaximumProcessExitCallbacks,
        "process callback capacity limit was not enforced");

  const std::vector<std::string> wrong_scope = {"libkernel"};
  Check(registry.Dispatch(kajps5::hle::kLibcExitNid, wrong_scope,
                          request_exit)
                .status == ExportRegistryStatus::kNotFound,
        "libc lifecycle export escaped its library scope");

  return failures == 0 ? 0 : 1;
}
