// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_executor.h"

#include <cstddef>
#include <initializer_list>
#include <vector>

#include "cpu/host_executable_buffer.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kajps5::cpu {
namespace {

constexpr std::size_t kNativeGuestBridgeBytes = 256;
constexpr std::uint64_t kGuestRootFrameSize = 32;
constexpr std::uint64_t kStackAlignment = 16;

#if defined(_WIN32)

struct NativeGuestFaultBoundary {
  std::uint64_t guest_begin = 0;
  std::uint64_t guest_end = 0;
  std::uint64_t recovery_address = 0;
  volatile std::uint64_t* host_stack_slot = nullptr;
  std::uint32_t exception_code = 0;
  std::uint64_t instruction_pointer = 0;
  std::uint64_t fault_address = 0;
  bool caught = false;
};

thread_local NativeGuestFaultBoundary* active_fault_boundary = nullptr;

LONG CALLBACK HandleNativeGuestException(EXCEPTION_POINTERS* exception) {
  auto* const boundary = active_fault_boundary;
  if (boundary == nullptr || exception == nullptr ||
      exception->ExceptionRecord == nullptr ||
      exception->ContextRecord == nullptr ||
      boundary->host_stack_slot == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const auto instruction_pointer =
      static_cast<std::uint64_t>(exception->ContextRecord->Rip);
  const auto host_stack_pointer = *boundary->host_stack_slot;
  if (instruction_pointer < boundary->guest_begin ||
      instruction_pointer >= boundary->guest_end || host_stack_pointer == 0 ||
      boundary->recovery_address == 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  boundary->exception_code = exception->ExceptionRecord->ExceptionCode;
  boundary->instruction_pointer = instruction_pointer;
  if ((boundary->exception_code == EXCEPTION_ACCESS_VIOLATION ||
       boundary->exception_code == EXCEPTION_IN_PAGE_ERROR) &&
      exception->ExceptionRecord->NumberParameters >= 2) {
    boundary->fault_address = static_cast<std::uint64_t>(
        exception->ExceptionRecord->ExceptionInformation[1]);
  }
  boundary->caught = true;
  exception->ContextRecord->Rsp = host_stack_pointer;
  exception->ContextRecord->Rip = boundary->recovery_address;
  return EXCEPTION_CONTINUE_EXECUTION;
}

bool EnsureNativeGuestExceptionHandler() noexcept {
  static void* const handler =
      AddVectoredExceptionHandler(1, HandleNativeGuestException);
  return handler != nullptr;
}

NativeGuestExecutionStatus FaultStatus(std::uint32_t exception_code) noexcept {
  switch (exception_code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
      return NativeGuestExecutionStatus::kGuestMemoryFault;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
      return NativeGuestExecutionStatus::kGuestInstructionFault;
    default:
      return NativeGuestExecutionStatus::kGuestFault;
  }
}

#endif

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
    std::uint64_t entry_point, std::uint64_t guest_memory_begin,
    std::uint64_t guest_memory_end, std::uint64_t stack_top,
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

#if defined(_WIN32)
  const auto recovery_offset = bridge.size();
  Emit(bridge, {0xfc});
  EmitMoveImmediate(bridge, {0x48, 0xb8},
                    static_cast<std::uint64_t>(
                        reinterpret_cast<std::uintptr_t>(host_stack_slot)));
  Emit(bridge, {0x48, 0xc7, 0x00, 0x00, 0x00, 0x00, 0x00});
  Emit(bridge, {0x48, 0x0f, 0xae, 0x0c, 0x24});
  Emit(bridge, {0x48, 0x81, 0xc4, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x31, 0xc0});
  Emit(bridge, {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c});
  Emit(bridge, {0x5e, 0x5f, 0x5d, 0x5b, 0xc3});
#endif

  HostExecutableBuffer entry_bridge(kNativeGuestBridgeBytes);
  if (!entry_bridge.allocated() || !entry_bridge.Write(bridge)) {
    return {NativeGuestExecutionStatus::kHostAllocationFailed, 0};
  }
  if (!entry_bridge.Seal()) {
    return {NativeGuestExecutionStatus::kHostProtectionFailed, 0};
  }
  using EntryBridge = std::uint64_t (*)();
  const auto function = reinterpret_cast<EntryBridge>(entry_bridge.address());
#if defined(_WIN32)
  if (active_fault_boundary != nullptr) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
  if (!EnsureNativeGuestExceptionHandler()) {
    return {NativeGuestExecutionStatus::kFaultBoundaryUnavailable, 0};
  }
  NativeGuestFaultBoundary boundary;
  boundary.guest_begin = guest_memory_begin;
  boundary.guest_end = guest_memory_end;
  boundary.recovery_address =
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
          static_cast<std::byte*>(entry_bridge.address()) + recovery_offset));
  boundary.host_stack_slot = host_stack_slot;
  active_fault_boundary = &boundary;
  const auto return_value = function();
  active_fault_boundary = nullptr;
  if (boundary.caught) {
    return {FaultStatus(boundary.exception_code), 0, boundary.exception_code,
            boundary.instruction_pointer, boundary.fault_address};
  }
  return {NativeGuestExecutionStatus::kOk, return_value};
#else
  (void)guest_memory_begin;
  (void)guest_memory_end;
  return {NativeGuestExecutionStatus::kOk, function()};
#endif
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
  return RunGuestEntry(entry_point, memory.base_address(), memory.end_address(),
                       stack_top, root_frame, parameters_address,
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
    case NativeGuestExecutionStatus::kFaultBoundaryUnavailable:
      return "fault-boundary-unavailable";
    case NativeGuestExecutionStatus::kGuestMemoryFault:
      return "guest-memory-fault";
    case NativeGuestExecutionStatus::kGuestInstructionFault:
      return "guest-instruction-fault";
    case NativeGuestExecutionStatus::kGuestFault:
      return "guest-fault";
    case NativeGuestExecutionStatus::kHostAllocationFailed:
      return "host-allocation-failed";
    case NativeGuestExecutionStatus::kHostProtectionFailed:
      return "host-protection-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
