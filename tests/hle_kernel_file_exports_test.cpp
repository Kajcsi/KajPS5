// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/kernel_file_exports.h"
#include "kernel/runtime.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_kernel_file_exports_test: " << message << '\n';
    ++failures;
  }
}

std::vector<std::byte> Bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const auto character : text) {
    result.push_back(static_cast<std::byte>(character));
  }
  return result;
}

std::uint64_t KernelResult(std::int32_t result) {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
}

std::uint64_t Dispatch(kajps5::hle::ExportRegistry& registry,
                       std::string_view symbol,
                       kajps5::hle::HleCallContext& context) {
  const std::vector<std::string> libraries = {kajps5::hle::kLibKernelName};
  const auto result = registry.Dispatch(symbol, libraries, context);
  Check(static_cast<bool>(result), "file export dispatch failed");
  return context.GetRegister(kajps5::hle::HleRegister::kRax).value_or(0);
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleRegister;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;

  GuestMemory memory(0x1000, 0x3000);
  KernelRuntime runtime;
  Check(runtime.files().RegisterReadOnlyFile(
            "/app0/data/test.bin", Bytes("abcdef")) ==
            kajps5::kernel::KernelStatus::kOk,
        "file fixture registration failed");

  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelFileExports(registry, runtime.files()) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 4,
        "file export registration failed");

  auto path = Bytes("/app0/data/test.bin");
  path.push_back(std::byte{0});
  Check(memory.Initialize(0x1100, path), "guest path setup failed");

  HleCallContext open_context(memory);
  Check(open_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            open_context.SetRegister(HleRegister::kRsi, 0) &&
            open_context.SetRegister(HleRegister::kRdx, 0),
        "open argument setup failed");
  const auto handle = Dispatch(registry, kajps5::hle::kKernelOpenNid,
                               open_context);
  Check(handle != 0 && runtime.handles().size() == 1,
        "NID open did not return a file descriptor");

  HleCallContext close_context(memory);
  Check(close_context.SetRegister(HleRegister::kRdi, handle),
        "close argument setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCloseName, close_context) == 0 &&
            runtime.handles().size() == 0,
        "named close did not release the descriptor");

  HleCallContext stale_context(memory);
  Check(stale_context.SetRegister(HleRegister::kRdi, handle),
        "stale close setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelCloseNid, stale_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorBadFileDescriptor),
        "stale close returned the wrong kernel result");

  HleCallContext missing_context(memory);
  auto missing = Bytes("/app0/missing.bin");
  missing.push_back(std::byte{0});
  Check(memory.Initialize(0x1200, missing) &&
            missing_context.SetRegister(HleRegister::kRdi, 0x1200),
        "missing path setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelOpenName, missing_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorNotFound),
        "missing open returned the wrong kernel result");

  HleCallContext write_context(memory);
  Check(write_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            write_context.SetRegister(HleRegister::kRsi, 1),
        "write-open setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelOpenName, write_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorPermissionDenied),
        "write open returned the wrong kernel result");

  HleCallContext flags_context(memory);
  Check(flags_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            flags_context.SetRegister(HleRegister::kRsi, 3),
        "invalid-flags setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelOpenName, flags_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "invalid open flags returned the wrong kernel result");

  HleCallContext fault_context(memory);
  Check(fault_context.SetRegister(HleRegister::kRdi, 0x5000),
        "faulting path setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelOpenName, fault_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "unmapped path returned the wrong kernel result");

  std::array<std::byte, kajps5::kernel::kMaximumGuestPathLength + 1>
      unterminated{};
  unterminated.fill(std::byte{'a'});
  Check(memory.Initialize(0x1800, unterminated),
        "unterminated path setup failed");
  HleCallContext unterminated_context(memory);
  Check(unterminated_context.SetRegister(HleRegister::kRdi, 0x1800),
        "unterminated path argument setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelOpenName,
                 unterminated_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "unterminated path returned the wrong kernel result");

  Check(kajps5::hle::RegisterKernelFileExports(registry, runtime.files()) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 4,
        "duplicate file export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
