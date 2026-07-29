// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "core/memory/guest_memory.h"
#include "cpu/native_leaf_executor.h"
#include "loader/elf.h"

namespace {

constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kCodeOffset = 0x100;
constexpr std::uint64_t kCodeAddress = 0x1000;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "native_leaf_executor_test: " << message << '\n';
    ++failures;
  }
}

void Write16(std::vector<std::byte>& image, std::size_t offset,
             std::uint16_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write32(std::vector<std::byte>& image, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::vector<std::byte>& image, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    image[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::vector<std::byte> MakeLeafElf() {
  constexpr std::size_t kCodeSize = 6;
  std::vector<std::byte> image(kCodeOffset + kCodeSize);
  image[0] = std::byte{0x7f};
  image[1] = std::byte{'E'};
  image[2] = std::byte{'L'};
  image[3] = std::byte{'F'};
  image[4] = std::byte{2};
  image[5] = std::byte{1};
  image[6] = std::byte{1};
  Write16(image, 16, 2);
  Write16(image, 18, 62);
  Write32(image, 20, 1);
  Write64(image, 24, kCodeAddress);
  Write64(image, 32, kProgramHeaderOffset);
  Write16(image, 52, 64);
  Write16(image, 54, 56);
  Write16(image, 56, 1);

  Write32(image, kProgramHeaderOffset, 1);
  Write32(image, kProgramHeaderOffset + 4, 5);
  Write64(image, kProgramHeaderOffset + 8, kCodeOffset);
  Write64(image, kProgramHeaderOffset + 16, kCodeAddress);
  Write64(image, kProgramHeaderOffset + 32, kCodeSize);
  Write64(image, kProgramHeaderOffset + 40, kCodeSize);
  Write64(image, kProgramHeaderOffset + 48, 0x100);

  image[kCodeOffset] = std::byte{0xb8};
  image[kCodeOffset + 1] = std::byte{0x2a};
  image[kCodeOffset + 2] = std::byte{0};
  image[kCodeOffset + 3] = std::byte{0};
  image[kCodeOffset + 4] = std::byte{0};
  image[kCodeOffset + 5] = std::byte{0xc3};
  return image;
}

}  // namespace

int main() {
  using kajps5::cpu::NativeExecutionStatus;
  using kajps5::cpu::NativeLeafExecutor;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  const auto image = MakeLeafElf();
  GuestMemory memory(kCodeAddress, 6, GuestMemoryProtection::kNone);
  const auto loaded = kajps5::loader::LoadElf64(image, memory);
  Check(static_cast<bool>(loaded), "public leaf ELF did not load");

  NativeLeafExecutor executor;
  const auto result = executor.Execute(memory, loaded.metadata.entry_point, 6);
  if (result.status == NativeExecutionStatus::kUnsupportedHost) {
    std::cout << "native leaf execution is unsupported on this host\n";
    return failures == 0 ? 0 : 1;
  }
  Check(result && result.return_value == 42,
        "public leaf entry did not return 42");
  Check(executor.Execute(memory, kCodeAddress, 0).status ==
            NativeExecutionStatus::kInvalidArgument,
        "zero code size was accepted");
  Check(executor
            .Execute(memory, kCodeAddress,
                     kajps5::cpu::kMaximumNativeLeafCodeSize + 1)
            .status == NativeExecutionStatus::kInvalidArgument,
        "oversized leaf code was accepted");

  GuestMemory non_executable(kCodeAddress, 6,
                             GuestMemoryProtection::kRead);
  Check(executor.Execute(non_executable, kCodeAddress, 6).status ==
            NativeExecutionStatus::kGuestCodeNotExecutable,
        "non-executable guest memory was accepted");

  GuestMemory execute_only(kCodeAddress, 6,
                           GuestMemoryProtection::kNone);
  Check(execute_only.Map(kCodeAddress, 6,
                         GuestMemoryProtection::kExecute),
        "execute-only test mapping failed");
  Check(executor.Execute(execute_only, kCodeAddress, 6).status ==
            NativeExecutionStatus::kGuestCodeNotReadable,
        "unreadable guest code was accepted");
  Check(kajps5::cpu::NativeExecutionStatusName(
            NativeExecutionStatus::kGuestCodeNotExecutable) ==
            "guest-code-not-executable",
        "native execution status name is unstable");

  return failures == 0 ? 0 : 1;
}
