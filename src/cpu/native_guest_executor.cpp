// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_executor.h"

#include <cstddef>
#include <initializer_list>
#include <vector>

#include "cpu/host_executable_buffer.h"

namespace kajps5::cpu {
namespace {

constexpr std::size_t kNativeGuestBridgeBytes = 256;
constexpr std::uint64_t kGuestRootFrameSize = 32;
constexpr std::uint64_t kStackAlignment = 16;

void Emit(std::vector<std::byte>& code,
          std::initializer_list<unsigned int> bytes) {
  for (const auto byte : bytes) {
    code.push_back(static_cast<std::byte>(byte));
  }
}

void EmitUInt64(std::vector<std::byte>& code, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    code.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  }
}

void EmitMoveImmediate(std::vector<std::byte>& code,
                       std::initializer_list<unsigned int> opcode,
                       std::uint64_t value) {
  Emit(code, opcode);
  EmitUInt64(code, value);
}

#if defined(_M_X64) || defined(__x86_64__)

NativeGuestExecutionResult RunGuestEntry(
    std::uint64_t entry_point, std::uint64_t stack_top,
    std::uint64_t root_frame, std::uint64_t parameters_address,
    std::uint64_t exit_handler_address,
    volatile std::uint64_t* host_stack_slot) {
  std::vector<std::byte> bridge;
  bridge.reserve(kNativeGuestBridgeBytes);

  // Preserve every register that differs between the Windows and System V
  // nonvolatile sets, then save the host floating-point control state.
  Emit(bridge, {0x53, 0x55, 0x57, 0x56});
  Emit(bridge, {0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
  Emit(bridge, {0x48, 0x81, 0xec, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x48, 0x0f, 0xae, 0x04, 0x24});
  Emit(bridge, {0x49, 0x89, 0xe4});
  EmitMoveImmediate(bridge, {0x48, 0xb8},
                    static_cast<std::uint64_t>(
                        reinterpret_cast<std::uintptr_t>(host_stack_slot)));
  Emit(bridge, {0x4c, 0x89, 0x20});

  EmitMoveImmediate(bridge, {0x48, 0xb8}, stack_top);
  Emit(bridge, {0x48, 0x89, 0xc4});
  EmitMoveImmediate(bridge, {0x48, 0xbd}, root_frame);
  EmitMoveImmediate(bridge, {0x48, 0xbf}, parameters_address);
  EmitMoveImmediate(bridge, {0x48, 0xbe}, exit_handler_address);
  EmitMoveImmediate(bridge, {0x48, 0xb8}, entry_point);
  Emit(bridge, {0xff, 0xd0});
  Emit(bridge, {0x49, 0x89, 0xc3});
  Emit(bridge, {0x4c, 0x89, 0xe4});
  EmitMoveImmediate(bridge, {0x48, 0xb8},
                    static_cast<std::uint64_t>(
                        reinterpret_cast<std::uintptr_t>(host_stack_slot)));
  Emit(bridge, {0x48, 0xc7, 0x00, 0x00, 0x00, 0x00, 0x00});

  Emit(bridge, {0x48, 0x0f, 0xae, 0x0c, 0x24});
  Emit(bridge, {0x48, 0x81, 0xc4, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x4c, 0x89, 0xd8});
  Emit(bridge, {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c});
  Emit(bridge, {0x5e, 0x5f, 0x5d, 0x5b, 0xc3});

  HostExecutableBuffer entry_bridge(kNativeGuestBridgeBytes);
  if (!entry_bridge.allocated() || !entry_bridge.Write(bridge)) {
    return {NativeGuestExecutionStatus::kHostAllocationFailed, 0};
  }
  if (!entry_bridge.Seal()) {
    return {NativeGuestExecutionStatus::kHostProtectionFailed, 0};
  }
  using EntryBridge = std::uint64_t (*)();
  const auto function = reinterpret_cast<EntryBridge>(entry_bridge.address());
  return {NativeGuestExecutionStatus::kOk, function()};
}

#endif

}  // namespace

NativeGuestExecutionResult NativeGuestExecutor::Execute(
    memory::GuestMemory& memory, std::uint64_t entry_point,
    std::uint64_t stack_address, std::uint64_t stack_size,
    std::uint64_t parameters_address, std::uint64_t exit_handler_address,
    NativeGuestExecutionContext* execution_context) const {
  NativeGuestExecutionContext local_context;
  auto* const context =
      execution_context != nullptr ? execution_context : &local_context;
  if (context->active()) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
  if (stack_size < kMinimumNativeGuestStackSize || stack_size > memory.size() ||
      stack_address > memory.end_address() ||
      stack_size > memory.end_address() - stack_address) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
  if (!memory.host_mapped()) {
    return {NativeGuestExecutionStatus::kHostMappingRequired, 0};
  }
  if (!memory.CanExecute(entry_point, 1)) {
    return {NativeGuestExecutionStatus::kGuestCodeNotExecutable, 0};
  }
  constexpr auto stack_access = memory::GuestMemoryProtection::kRead |
                                memory::GuestMemoryProtection::kWrite;
  if (!memory.CanAccess(stack_address, stack_size, stack_access)) {
    return {NativeGuestExecutionStatus::kGuestStackNotAccessible, 0};
  }
  if (!memory.CanAccess(parameters_address, sizeof(NativeGuestEntryParameters),
                        memory::GuestMemoryProtection::kRead)) {
    return {NativeGuestExecutionStatus::kGuestParametersNotReadable, 0};
  }

  const auto stack_end = stack_address + stack_size;
  const auto stack_top = stack_end & ~(kStackAlignment - 1);
  if (stack_top < stack_address + kGuestRootFrameSize) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
  const auto root_frame = stack_top - kGuestRootFrameSize;
  if (!memory.Fill(root_frame, 2 * sizeof(std::uint64_t), std::byte{0})) {
    return {NativeGuestExecutionStatus::kGuestStackNotAccessible, 0};
  }

#if !defined(_M_X64) && !defined(__x86_64__)
  return {NativeGuestExecutionStatus::kUnsupportedHost, 0};
#else
  return RunGuestEntry(entry_point, stack_top, root_frame, parameters_address,
                       exit_handler_address, &context->host_stack_pointer_);
#endif
}

std::string_view NativeGuestExecutionStatusName(
    NativeGuestExecutionStatus status) noexcept {
  switch (status) {
    case NativeGuestExecutionStatus::kOk:
      return "ok";
    case NativeGuestExecutionStatus::kUnsupportedHost:
      return "unsupported-host";
    case NativeGuestExecutionStatus::kInvalidArgument:
      return "invalid-argument";
    case NativeGuestExecutionStatus::kHostMappingRequired:
      return "host-mapping-required";
    case NativeGuestExecutionStatus::kGuestCodeNotExecutable:
      return "guest-code-not-executable";
    case NativeGuestExecutionStatus::kGuestStackNotAccessible:
      return "guest-stack-not-accessible";
    case NativeGuestExecutionStatus::kGuestParametersNotReadable:
      return "guest-parameters-not-readable";
    case NativeGuestExecutionStatus::kHostAllocationFailed:
      return "host-allocation-failed";
    case NativeGuestExecutionStatus::kHostProtectionFailed:
      return "host-protection-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
