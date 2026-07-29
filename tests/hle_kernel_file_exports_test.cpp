// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
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

std::string ReadText(kajps5::memory::GuestMemory& memory,
                     std::uint64_t address, std::size_t size) {
  std::vector<std::byte> bytes(size);
  if (!memory.Read(address, bytes)) {
    return {};
  }
  std::string text;
  text.reserve(size);
  for (const auto byte : bytes) {
    text.push_back(static_cast<char>(byte));
  }
  return text;
}

std::uint64_t ReadLittleEndian(std::span<const std::byte> bytes,
                               std::size_t offset, std::size_t size) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < size; ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleRegister;
  using kajps5::kernel::KernelRuntime;
  using kajps5::memory::GuestMemory;

  GuestMemory memory(0x1000, 0x10000);
  KernelRuntime runtime;
  Check(runtime.files().RegisterReadOnlyFile(
            "/app0/data/test.bin", Bytes("abcdef")) ==
            kajps5::kernel::KernelStatus::kOk,
        "file fixture registration failed");
  std::vector<std::byte> large_file(20 * 1024);
  for (std::size_t index = 0; index < large_file.size(); ++index) {
    large_file[index] = static_cast<std::byte>(index & 0xffU);
  }
  Check(runtime.files().RegisterReadOnlyFile(
            "/app0/data/large.bin", large_file) ==
            kajps5::kernel::KernelStatus::kOk,
        "large file fixture registration failed");

  ExportRegistry registry;
  Check(kajps5::hle::RegisterKernelFileExports(registry, runtime.files()) ==
            ExportRegistryStatus::kOk &&
            registry.size() == 14,
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

  HleCallContext stat_context(memory);
  Check(stat_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            stat_context.SetRegister(HleRegister::kRsi, 0x2200),
        "stat argument setup failed");
  std::array<std::byte, kajps5::hle::kKernelStatSize> stat_bytes{};
  Check(Dispatch(registry, kajps5::hle::kKernelStatNid, stat_context) == 0 &&
            memory.Read(0x2200, stat_bytes) &&
            ReadLittleEndian(stat_bytes, 4, 4) != 0 &&
            ReadLittleEndian(stat_bytes, 8, 2) == 0x81ff &&
            ReadLittleEndian(stat_bytes, 10, 2) == 1 &&
            ReadLittleEndian(stat_bytes, 72, 8) == 6 &&
            ReadLittleEndian(stat_bytes, 80, 8) == 1 &&
            ReadLittleEndian(stat_bytes, 88, 4) == 512,
        "NID stat returned the wrong regular-file metadata");

  HleCallContext fstat_context(memory);
  Check(fstat_context.SetRegister(HleRegister::kRdi, handle) &&
            fstat_context.SetRegister(HleRegister::kRsi, 0x2300),
        "fstat argument setup failed");
  std::array<std::byte, kajps5::hle::kKernelStatSize> fstat_bytes{};
  Check(Dispatch(registry, kajps5::hle::kKernelFstatName, fstat_context) == 0 &&
            memory.Read(0x2300, fstat_bytes) && fstat_bytes == stat_bytes,
        "named fstat did not match path metadata");

  HleCallContext null_stat_context(memory);
  Check(null_stat_context.SetRegister(HleRegister::kRdi, 0x1100) &&
            null_stat_context.SetRegister(HleRegister::kRsi, 0),
        "null stat output setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelStatName,
                 null_stat_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "null stat output returned the wrong kernel result");

  HleCallContext null_fstat_context(memory);
  Check(null_fstat_context.SetRegister(HleRegister::kRdi, handle) &&
            null_fstat_context.SetRegister(HleRegister::kRsi, 0),
        "null fstat output setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelFstatName,
                 null_fstat_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "null fstat output returned the wrong kernel result");

  kajps5::memory::GuestMemory partial_memory(
      0x30000, kajps5::hle::kKernelStatSize,
      kajps5::memory::GuestMemoryProtection::kNone);
  Check(partial_memory.Map(
            0x30000, kajps5::hle::kKernelStatSize - 1,
            kajps5::memory::GuestMemoryProtection::kRead |
                kajps5::memory::GuestMemoryProtection::kWrite),
        "partial stat mapping failed");
  std::array<std::byte, kajps5::hle::kKernelStatSize - 1> stat_sentinel{};
  stat_sentinel.fill(std::byte{0xcc});
  Check(partial_memory.Initialize(0x30000, stat_sentinel),
        "partial stat sentinel setup failed");
  HleCallContext partial_stat_context(partial_memory);
  Check(partial_stat_context.SetRegister(HleRegister::kRdi, handle) &&
            partial_stat_context.SetRegister(HleRegister::kRsi, 0x30000),
        "partial fstat argument setup failed");
  std::array<std::byte, kajps5::hle::kKernelStatSize - 1> stat_preserved{};
  Check(Dispatch(registry, kajps5::hle::kKernelFstatNid,
                 partial_stat_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorFault) &&
            partial_memory.Read(0x30000, stat_preserved) &&
            stat_preserved == stat_sentinel,
        "failed fstat write changed guest memory");

  HleCallContext read_context(memory);
  Check(read_context.SetRegister(HleRegister::kRdi, handle) &&
            read_context.SetRegister(HleRegister::kRsi, 0x2800) &&
            read_context.SetRegister(HleRegister::kRdx, 2),
        "read argument setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReadNid, read_context) == 2 &&
            ReadText(memory, 0x2800, 2) == "ab",
        "NID read returned the wrong bytes");

  HleCallContext pread_context(memory);
  Check(pread_context.SetRegister(HleRegister::kRdi, handle) &&
            pread_context.SetRegister(HleRegister::kRsi, 0x2810) &&
            pread_context.SetRegister(HleRegister::kRdx, 2) &&
            pread_context.SetRegister(HleRegister::kRcx, 4),
        "pread argument setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPreadName, pread_context) == 2 &&
            ReadText(memory, 0x2810, 2) == "ef",
        "named pread returned the wrong bytes");

  HleCallContext tell_context(memory);
  Check(tell_context.SetRegister(HleRegister::kRdi, handle) &&
            tell_context.SetRegister(HleRegister::kRsi, 0) &&
            tell_context.SetRegister(HleRegister::kRdx, 1),
        "current-position setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelLseekNid, tell_context) == 2,
        "pread changed the shared file position");

  HleCallContext seek_context(memory);
  Check(seek_context.SetRegister(HleRegister::kRdi, handle) &&
            seek_context.SetRegister(HleRegister::kRsi,
                                     static_cast<std::uint64_t>(-1)) &&
            seek_context.SetRegister(HleRegister::kRdx, 2),
        "end-relative seek setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelLseekName, seek_context) == 5,
        "named end-relative seek failed");

  HleCallContext tail_context(memory);
  Check(tail_context.SetRegister(HleRegister::kRdi, handle) &&
            tail_context.SetRegister(HleRegister::kRsi, 0x2820) &&
            tail_context.SetRegister(HleRegister::kRdx, 2),
        "tail read setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReadName, tail_context) == 1 &&
            ReadText(memory, 0x2820, 1) == "f",
        "end-of-file read returned the wrong bytes");

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

  HleCallContext stale_read_context(memory);
  Check(stale_read_context.SetRegister(HleRegister::kRdi, handle) &&
            stale_read_context.SetRegister(HleRegister::kRsi, 0x2800) &&
            stale_read_context.SetRegister(HleRegister::kRdx, 1),
        "stale read setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReadName,
                 stale_read_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorBadFileDescriptor),
        "stale read returned the wrong kernel result");

  HleCallContext stale_fstat_context(memory);
  Check(stale_fstat_context.SetRegister(HleRegister::kRdi, handle) &&
            stale_fstat_context.SetRegister(HleRegister::kRsi, 0x2300),
        "stale fstat setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelFstatName,
                 stale_fstat_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorBadFileDescriptor),
        "stale fstat returned the wrong kernel result");

  HleCallContext missing_context(memory);
  auto missing = Bytes("/app0/missing.bin");
  missing.push_back(std::byte{0});
  Check(memory.Initialize(0x1200, missing) &&
            missing_context.SetRegister(HleRegister::kRdi, 0x1200),
        "missing path setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelOpenName, missing_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorNotFound),
        "missing open returned the wrong kernel result");

  std::array<std::byte, kajps5::hle::kKernelStatSize> missing_sentinel{};
  missing_sentinel.fill(std::byte{0xa5});
  Check(memory.Initialize(0x2400, missing_sentinel),
        "missing stat sentinel setup failed");
  HleCallContext missing_stat_context(memory);
  Check(missing_stat_context.SetRegister(HleRegister::kRdi, 0x1200) &&
            missing_stat_context.SetRegister(HleRegister::kRsi, 0x2400),
        "missing stat setup failed");
  std::array<std::byte, kajps5::hle::kKernelStatSize> missing_preserved{};
  Check(Dispatch(registry, kajps5::hle::kKernelStatName,
                 missing_stat_context) ==
                KernelResult(kajps5::hle::kKernelHleErrorNotFound) &&
            memory.Read(0x2400, missing_preserved) &&
            missing_preserved == missing_sentinel,
        "missing stat changed guest memory");

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
  Check(fault_context.SetRegister(HleRegister::kRdi, 0x20000),
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

  HleCallContext fault_open_context(memory);
  Check(fault_open_context.SetRegister(HleRegister::kRdi, 0x1100),
        "fault-read open setup failed");
  const auto fault_handle = Dispatch(
      registry, kajps5::hle::kKernelOpenName, fault_open_context);
  HleCallContext fault_read_context(memory);
  Check(fault_read_context.SetRegister(HleRegister::kRdi, fault_handle) &&
            fault_read_context.SetRegister(HleRegister::kRsi, 0x20000) &&
            fault_read_context.SetRegister(HleRegister::kRdx, 2),
        "fault-read argument setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReadName,
                 fault_read_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorFault),
        "unmapped output returned the wrong kernel result");
  HleCallContext preserved_context(memory);
  Check(preserved_context.SetRegister(HleRegister::kRdi, fault_handle) &&
            preserved_context.SetRegister(HleRegister::kRsi, 0x2830) &&
            preserved_context.SetRegister(HleRegister::kRdx, 2),
        "preserved-position read setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReadName,
                 preserved_context) == 2 &&
            ReadText(memory, 0x2830, 2) == "ab",
        "failed guest write advanced the file position");

  HleCallContext negative_pread_context(memory);
  Check(negative_pread_context.SetRegister(HleRegister::kRdi, fault_handle) &&
            negative_pread_context.SetRegister(HleRegister::kRsi, 0x2840) &&
            negative_pread_context.SetRegister(HleRegister::kRdx, 1) &&
            negative_pread_context.SetRegister(
                HleRegister::kRcx, static_cast<std::uint64_t>(-1)),
        "negative pread setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelPreadNid,
                 negative_pread_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "negative pread returned the wrong kernel result");

  HleCallContext whence_context(memory);
  Check(whence_context.SetRegister(HleRegister::kRdi, fault_handle) &&
            whence_context.SetRegister(HleRegister::kRsi, 0) &&
            whence_context.SetRegister(HleRegister::kRdx, 3),
        "invalid-whence setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelLseekName, whence_context) ==
            KernelResult(kajps5::hle::kKernelHleErrorInvalidArgument),
        "invalid seek origin returned the wrong kernel result");

  HleCallContext zero_read_context(memory);
  Check(zero_read_context.SetRegister(HleRegister::kRdi, fault_handle) &&
            zero_read_context.SetRegister(HleRegister::kRsi, 0) &&
            zero_read_context.SetRegister(HleRegister::kRdx, 0),
        "zero read setup failed");
  Check(Dispatch(registry, kajps5::hle::kKernelReadName,
                 zero_read_context) == 0,
        "zero-byte read rejected a null output pointer");

  auto large_path = Bytes("/app0/data/large.bin");
  large_path.push_back(std::byte{0});
  Check(memory.Initialize(0x3000, large_path),
        "large guest path setup failed");
  HleCallContext large_open_context(memory);
  Check(large_open_context.SetRegister(HleRegister::kRdi, 0x3000),
        "large-file open setup failed");
  const auto large_handle = Dispatch(
      registry, kajps5::hle::kKernelOpenName, large_open_context);
  HleCallContext large_read_context(memory);
  Check(large_read_context.SetRegister(HleRegister::kRdi, large_handle) &&
            large_read_context.SetRegister(HleRegister::kRsi, 0x6000) &&
            large_read_context.SetRegister(HleRegister::kRdx,
                                           large_file.size()),
        "large read setup failed");
  std::vector<std::byte> large_observed(large_file.size());
  Check(Dispatch(registry, kajps5::hle::kKernelReadName,
                 large_read_context) == large_file.size() &&
            memory.Read(0x6000, large_observed) &&
            large_observed == large_file,
        "chunked read returned the wrong bytes");

  HleCallContext large_close_context(memory);
  Check(large_close_context.SetRegister(HleRegister::kRdi, large_handle) &&
            Dispatch(registry, kajps5::hle::kKernelCloseName,
                     large_close_context) == 0,
        "large-file close failed");

  HleCallContext fault_close_context(memory);
  Check(fault_close_context.SetRegister(HleRegister::kRdi, fault_handle) &&
            Dispatch(registry, kajps5::hle::kKernelCloseName,
                     fault_close_context) == 0,
        "fault-read fixture close failed");

  Check(kajps5::hle::RegisterKernelFileExports(registry, runtime.files()) ==
            ExportRegistryStatus::kAlreadyExists &&
            registry.size() == 14,
        "duplicate file export batch changed the registry");
  return failures == 0 ? 0 : 1;
}
