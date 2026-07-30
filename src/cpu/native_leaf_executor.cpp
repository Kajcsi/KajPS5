// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_leaf_executor.h"

#include <cstddef>
#include <initializer_list>
#include <vector>

#include "cpu/host_executable_buffer.h"

namespace kajps5::cpu {
namespace {

#if defined(_WIN32) && defined(_M_X64)

void Emit(std::vector<std::byte>& code,
          std::initializer_list<unsigned int> bytes) {
  for (const auto byte : bytes) {
    code.push_back(static_cast<std::byte>(byte));
  }
}

void EmitUInt64(std::vector<std::byte>& code, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    code.push_back(
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

NativeExecutionResult ExecuteThroughWindowsAbiBridge(void* guest_entry) {
  constexpr std::size_t kBridgeSize = 64;
  std::vector<std::byte> bridge;
  bridge.reserve(kBridgeSize);
  // Preserve Windows-only nonvolatile state before entering System V code.
  Emit(bridge, {0x57, 0x56});
  Emit(bridge, {0x48, 0x81, 0xec, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x48, 0x0f, 0xae, 0x04, 0x24});
  // Call the guest entry with a System V-aligned stack.
  Emit(bridge, {0x48, 0xb8});
  EmitUInt64(bridge, static_cast<std::uint64_t>(
                         reinterpret_cast<std::uintptr_t>(guest_entry)));
  Emit(bridge, {0xff, 0xd0});
  // Restore the host floating-point and integer state before returning.
  Emit(bridge, {0x48, 0x0f, 0xae, 0x0c, 0x24});
  Emit(bridge, {0x48, 0x81, 0xc4, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x5e, 0x5f, 0xc3});

  HostExecutableBuffer entry_bridge(kBridgeSize);
  if (!entry_bridge.allocated() || !entry_bridge.Write(bridge)) {
    return {NativeExecutionStatus::kHostAllocationFailed, 0};
  }
  if (!entry_bridge.Seal()) {
    return {NativeExecutionStatus::kHostProtectionFailed, 0};
  }
  using EntryBridge = std::uint64_t (*)();
  const auto function =
      reinterpret_cast<EntryBridge>(entry_bridge.address());
  return {NativeExecutionStatus::kOk, function()};
}

#endif

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

  HostExecutableBuffer allocation(code_size);
  if (!allocation.allocated() || !allocation.Write(code)) {
    return {NativeExecutionStatus::kHostAllocationFailed, 0};
  }
  if (!allocation.Seal()) {
    return {NativeExecutionStatus::kHostProtectionFailed, 0};
  }

#if defined(_WIN32) && defined(_M_X64)
  return ExecuteThroughWindowsAbiBridge(allocation.address());
#else
  using LeafFunction = std::uint64_t (*)();
  const auto function = reinterpret_cast<LeafFunction>(allocation.address());
  return {NativeExecutionStatus::kOk, function()};
#endif
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
