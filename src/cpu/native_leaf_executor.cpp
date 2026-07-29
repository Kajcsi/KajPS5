// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_leaf_executor.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace kajps5::cpu {
namespace {

#if defined(_WIN32)

void* AllocateWritable(std::size_t size) noexcept {
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

bool MakeExecutable(void* address, std::size_t size) noexcept {
  DWORD old_protection = 0;
  if (VirtualProtect(address, size, PAGE_EXECUTE_READ, &old_protection) == 0) {
    return false;
  }
  return FlushInstructionCache(GetCurrentProcess(), address, size) != 0;
}

void FreeAllocation(void* address, std::size_t) noexcept {
  if (address != nullptr) {
    VirtualFree(address, 0, MEM_RELEASE);
  }
}

#elif defined(__unix__) || defined(__APPLE__)

void* AllocateWritable(std::size_t size) noexcept {
  auto* allocation =
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
           -1, 0);
  return allocation == MAP_FAILED ? nullptr : allocation;
}

bool MakeExecutable(void* address, std::size_t size) noexcept {
  if (mprotect(address, size, PROT_READ | PROT_EXEC) != 0) {
    return false;
  }
  auto* begin = static_cast<char*>(address);
  __builtin___clear_cache(begin, begin + size);
  return true;
}

void FreeAllocation(void* address, std::size_t size) noexcept {
  if (address != nullptr) {
    munmap(address, size);
  }
}

#else

void* AllocateWritable(std::size_t) noexcept { return nullptr; }
bool MakeExecutable(void*, std::size_t) noexcept { return false; }
void FreeAllocation(void*, std::size_t) noexcept {}

#endif

class HostAllocation final {
 public:
  explicit HostAllocation(std::size_t size) noexcept
      : address_(AllocateWritable(size)), size_(size) {}
  ~HostAllocation() { FreeAllocation(address_, size_); }

  HostAllocation(const HostAllocation&) = delete;
  HostAllocation& operator=(const HostAllocation&) = delete;

  [[nodiscard]] void* address() const noexcept { return address_; }

 private:
  void* address_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace

NativeExecutionResult NativeLeafExecutor::Execute(
    const memory::GuestMemory& memory, std::uint64_t entry_point,
    std::size_t code_size) const {
  if (code_size == 0 || code_size > kMaximumNativeLeafCodeSize) {
    return {NativeExecutionStatus::kInvalidArgument, 0};
  }
  if (!memory.CanExecute(entry_point, code_size)) {
    return {NativeExecutionStatus::kGuestCodeNotExecutable, 0};
  }
  if (!memory.CanAccess(entry_point, code_size,
                        memory::GuestMemoryProtection::kRead)) {
    return {NativeExecutionStatus::kGuestCodeNotReadable, 0};
  }

#if !defined(_M_X64) && !defined(__x86_64__)
  return {NativeExecutionStatus::kUnsupportedHost, 0};
#else
  std::vector<std::byte> code(code_size);
  if (!memory.Read(entry_point, code)) {
    return {NativeExecutionStatus::kGuestCodeNotReadable, 0};
  }

  HostAllocation allocation(code_size);
  if (allocation.address() == nullptr) {
    return {NativeExecutionStatus::kHostAllocationFailed, 0};
  }
  std::memcpy(allocation.address(), code.data(), code.size());
  if (!MakeExecutable(allocation.address(), code.size())) {
    return {NativeExecutionStatus::kHostProtectionFailed, 0};
  }

  using LeafFunction = std::uint64_t (*)();
  const auto function = reinterpret_cast<LeafFunction>(allocation.address());
  return {NativeExecutionStatus::kOk, function()};
#endif
}

std::string_view NativeExecutionStatusName(
    NativeExecutionStatus status) noexcept {
  switch (status) {
    case NativeExecutionStatus::kOk: return "ok";
    case NativeExecutionStatus::kUnsupportedHost: return "unsupported-host";
    case NativeExecutionStatus::kInvalidArgument: return "invalid-argument";
    case NativeExecutionStatus::kGuestCodeNotExecutable:
      return "guest-code-not-executable";
    case NativeExecutionStatus::kGuestCodeNotReadable:
      return "guest-code-not-readable";
    case NativeExecutionStatus::kHostAllocationFailed:
      return "host-allocation-failed";
    case NativeExecutionStatus::kHostProtectionFailed:
      return "host-protection-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
