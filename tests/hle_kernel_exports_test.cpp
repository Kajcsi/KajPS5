// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/kernel_clock_exports.h"
#include "hle/kernel_event_flag_exports.h"
#include "hle/kernel_exports.h"
#include "hle/kernel_file_exports.h"
#include "hle/kernel_memory_exports.h"
#include "hle/kernel_pthread_exports.h"
#include "kernel/clock.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_exports_test: " << message << '\n';
    ++failures;
  }
}

class TestClockSource final : public kajps5::kernel::KernelClockSource {
 public:
  [[nodiscard]] std::int64_t RealtimeNanoseconds() const override {
    return 0;
  }

  [[nodiscard]] std::uint64_t MonotonicNanoseconds() const override {
    return monotonic_nanoseconds;
  }

  std::uint64_t monotonic_nanoseconds = 2'000'000'000;
};

std::vector<std::byte> Bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const auto character : text) {
    result.push_back(static_cast<std::byte>(character));
  }
  return result;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::HleRegister;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;

  auto source = std::make_unique<TestClockSource>();
  auto* const source_view = source.get();
  KernelRuntime runtime(std::move(source));
  source_view->monotonic_nanoseconds = 2'500'000'000;
  Check(runtime.files().RegisterReadOnlyFile("/app0/default.bin", Bytes("x")) ==
            kajps5::kernel::KernelStatus::kOk,
        "default file fixture registration failed");
  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelExports(registry, runtime) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 248,
        "default kernel exports did not register atomically");

  GuestMemory memory(0x1000, 0x1000);
  HleCallContext clock_context(memory);
  // PS5 ELF metadata uses this exact lowercase library name.
  const std::vector<std::string> libraries = {"libkernel"};
  HleCallContext direct_memory_context(memory);
  HleCallContext default_proc_param_context(memory);
  HleCallContext default_malloc_replace_context(memory);
  HleCallContext default_new_replace_context(memory);
  Check(registry.Dispatch(kajps5::hle::kKernelGetProcParamName, libraries,
                          default_proc_param_context) &&
            default_proc_param_context.GetRegister(HleRegister::kRax)
                    .value_or(1) == 0,
        "default proc-param export did not return zero");
  Check(runtime.sanitizer_malloc_replace_address() == 0 &&
            runtime.sanitizer_new_replace_address() == 0 &&
            runtime.application_heap_api_address() == 0 &&
            runtime.thread_atexit_count_callback() == 0 &&
            runtime.thread_atexit_report_callback() == 0 &&
            runtime.thread_dtors_callback() == 0 &&
            registry.Dispatch(
                kajps5::hle::kKernelGetSanitizerMallocReplaceExternalName,
                libraries, default_malloc_replace_context) &&
            default_malloc_replace_context.GetRegister(HleRegister::kRax)
                    .value_or(1) ==
                0 &&
            registry.Dispatch(
                kajps5::hle::kKernelGetSanitizerNewReplaceExternalNid,
                libraries, default_new_replace_context) &&
            default_new_replace_context.GetRegister(HleRegister::kRax)
                    .value_or(1) ==
                0,
        "runtime registration defaults are not zero");
  constexpr std::uint64_t kProcessParametersAddress = 0x1234'5678'9abc'def0;
  runtime.SetProcessParametersAddress(kProcessParametersAddress);
  HleCallContext proc_param_name_context(memory);
  HleCallContext proc_param_nid_context(memory);
  Check(registry.Dispatch(kajps5::hle::kKernelGetProcParamName, libraries,
                          proc_param_name_context) &&
            proc_param_name_context.GetRegister(HleRegister::kRax).value_or(0) ==
                kProcessParametersAddress &&
            registry.Dispatch(kajps5::hle::kKernelGetProcParamNid, libraries,
                              proc_param_nid_context) &&
            proc_param_nid_context.GetRegister(HleRegister::kRax).value_or(0) ==
                kProcessParametersAddress,
        "proc-param name and NID exports did not use the shared runtime");
  constexpr std::uint64_t kMallocReplaceAddress = 0x40000300;
  constexpr std::uint64_t kNewReplaceAddress = 0x40000400;
  runtime.SetSanitizerMallocReplaceAddress(kMallocReplaceAddress);
  runtime.SetSanitizerNewReplaceAddress(kNewReplaceAddress);
  HleCallContext malloc_replace_name_context(memory);
  HleCallContext malloc_replace_nid_context(memory);
  HleCallContext new_replace_name_context(memory);
  HleCallContext new_replace_nid_context(memory);
  Check(registry.Dispatch(
                kajps5::hle::kKernelGetSanitizerMallocReplaceExternalName,
                libraries, malloc_replace_name_context) &&
            malloc_replace_name_context.GetRegister(HleRegister::kRax)
                    .value_or(0) ==
                kMallocReplaceAddress &&
            registry.Dispatch(
                kajps5::hle::kKernelGetSanitizerMallocReplaceExternalNid,
                libraries, malloc_replace_nid_context) &&
            malloc_replace_nid_context.GetRegister(HleRegister::kRax)
                    .value_or(0) ==
                kMallocReplaceAddress &&
            registry.Dispatch(
                kajps5::hle::kKernelGetSanitizerNewReplaceExternalName,
                libraries, new_replace_name_context) &&
            new_replace_name_context.GetRegister(HleRegister::kRax)
                    .value_or(0) ==
                kNewReplaceAddress &&
            registry.Dispatch(
                kajps5::hle::kKernelGetSanitizerNewReplaceExternalNid,
                libraries, new_replace_nid_context) &&
            new_replace_nid_context.GetRegister(HleRegister::kRax)
                    .value_or(0) ==
                kNewReplaceAddress,
        "sanitizer replacement getter aliases did not use the shared runtime");
  const auto check_setter = [&](std::string_view name, std::uint64_t value) {
    HleCallContext context(memory);
    return context.SetRegister(HleRegister::kRdi, value) &&
           registry.Dispatch(name, libraries, context) &&
           context.return_written() &&
           context.GetRegister(HleRegister::kRax).value_or(1) == 0;
  };
  Check(check_setter(kajps5::hle::kKernelRtldSetApplicationHeapApiName,
                     0x1111) &&
            runtime.application_heap_api_address() == 0x1111 &&
            check_setter(kajps5::hle::kKernelRtldSetApplicationHeapApiNid,
                         0x2222) &&
            runtime.application_heap_api_address() == 0x2222 &&
            check_setter(kajps5::hle::kKernelSetThreadAtexitCountName,
                         0x3333) &&
            runtime.thread_atexit_count_callback() == 0x3333 &&
            check_setter(kajps5::hle::kKernelSetThreadAtexitCountNid,
                         0x4444) &&
            runtime.thread_atexit_count_callback() == 0x4444 &&
            check_setter(kajps5::hle::kKernelSetThreadAtexitReportName,
                         0x5555) &&
            runtime.thread_atexit_report_callback() == 0x5555 &&
            check_setter(kajps5::hle::kKernelSetThreadAtexitReportNid,
                         0x6666) &&
            runtime.thread_atexit_report_callback() == 0x6666 &&
            check_setter(kajps5::hle::kKernelSetThreadDtorsName, 0x7777) &&
            runtime.thread_dtors_callback() == 0x7777 &&
            check_setter(kajps5::hle::kKernelSetThreadDtorsNid, 0x8888) &&
            runtime.thread_dtors_callback() == 0x8888,
        "runtime registration setter aliases did not replace shared state");
  Check(registry.Dispatch(kajps5::hle::kKernelGetDirectMemorySizeNid,
                          libraries, direct_memory_context) &&
            direct_memory_context.GetRegister(HleRegister::kRax).value_or(0) ==
                runtime.direct_memory().size(),
        "default direct-memory export did not use the shared runtime");
  Check(registry.Dispatch(kajps5::hle::kKernelGetProcessTimeCounterNid,
                          libraries, clock_context) &&
            clock_context.GetRegister(HleRegister::kRax).value_or(0) ==
                500'000'000,
        "default clock export did not use the shared runtime");
  HleCallContext stack_failure(memory);
  Check(registry.Dispatch(kajps5::hle::kKernelStackCheckFailNid,
                          libraries, stack_failure)
                .handler_status == HleContextStatus::kFatalGuestError,
        "stack corruption did not stop guest execution");

  auto path = Bytes("/app0/default.bin");
  path.push_back(std::byte{0});
  Check(memory.Initialize(0x1100, path), "default guest path setup failed");
  HleCallContext open_context(memory);
  Check(open_context.SetRegister(HleRegister::kRdi, 0x1100),
        "default open setup failed");
  Check(static_cast<bool>(registry.Dispatch(kajps5::hle::kKernelOpenNid,
                                            libraries, open_context)),
        "default file export dispatch failed");
  const auto handle = open_context.GetRegister(HleRegister::kRax).value_or(0);
  Check(handle != 0 && runtime.handles().size() == 1,
        "default file export did not use the shared runtime");

  auto event_name = Bytes("default-event");
  event_name.push_back(std::byte{0});
  Check(memory.Initialize(0x1200, event_name),
        "default event name setup failed");
  HleCallContext event_context(memory);
  Check(event_context.SetRegister(HleRegister::kRdi, 0x1300) &&
            event_context.SetRegister(HleRegister::kRsi, 0x1200) &&
            event_context.SetRegister(HleRegister::kRdx, 0x21) &&
            event_context.SetRegister(HleRegister::kRcx, 1),
        "default event create setup failed");
  Check(registry.Dispatch(kajps5::hle::kKernelCreateEventFlagNid,
                          libraries, event_context) &&
            event_context.GetRegister(HleRegister::kRax).value_or(1) == 0 &&
            runtime.handles().size() == 2,
        "default event export did not use the shared runtime");

  Check(kajps5::hle::RegisterKernelExports(registry, runtime) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 248,
        "duplicate default registration changed the registry");

  ExportRegistry conflict_registry;
  Check(conflict_registry.Register(
            kajps5::hle::kLibKernelName, kajps5::hle::kKernelOpenName,
            [](HleCallContext&) { return HleContextStatus::kOk; }) ==
            ExportRegistryStatus::kOk,
        "kernel export conflict setup failed");
  Check(kajps5::hle::RegisterKernelExports(conflict_registry, runtime) ==
            ExportRegistryStatus::kAlreadyExists &&
            conflict_registry.size() == 1,
        "failed default registration changed the registry");

  return failures == 0 ? 0 : 1;
}
