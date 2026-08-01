// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_guest_executor.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <limits>
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

constexpr std::size_t kNativeGuestBridgeBytes = 512;
constexpr std::uint64_t kGuestRootFrameSize = 32;
constexpr std::uint64_t kStackAlignment = 16;
constexpr std::uint64_t kNativeControlNone = 0;
constexpr std::uint64_t kNativeControlBlocked = 1;
constexpr std::uint64_t kNativeControlYielded = 3;

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

NativeGuestExecutionResult ControlResult(
    std::uint64_t control_request, hle::HleContextStatus hle_status) noexcept {
  NativeGuestExecutionResult result;
  result.hle_status = hle_status;
  if (control_request == kNativeControlBlocked) {
    result.status = NativeGuestExecutionStatus::kHleBlocked;
  } else if (control_request == kNativeControlYielded) {
    result.status = NativeGuestExecutionStatus::kHleYielded;
  } else if (hle_status == hle::HleContextStatus::kGuestExit) {
    result.status = NativeGuestExecutionStatus::kGuestExit;
  } else {
    result.status = NativeGuestExecutionStatus::kHleDispatchFailed;
  }
  return result;
}

NativeGuestExecutionResult RunGuestEntryBridge(
    std::uint64_t entry_point, std::uint64_t guest_memory_begin,
    std::uint64_t guest_memory_end, std::uint64_t stack_top,
    std::uint64_t root_frame, const std::array<std::uint64_t, 6>& arguments,
    volatile std::uint64_t* host_stack_slot,
    volatile std::uint64_t* recovery_address,
    volatile std::uint64_t* control_request,
    hle::HleContextStatus* hle_status) {
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
  EmitMoveImmediate(bridge, {0x48, 0xbf}, arguments[0]);
  EmitMoveImmediate(bridge, {0x48, 0xbe}, arguments[1]);
  EmitMoveImmediate(bridge, {0x48, 0xba}, arguments[2]);
  EmitMoveImmediate(bridge, {0x48, 0xb9}, arguments[3]);
  EmitMoveImmediate(bridge, {0x49, 0xb8}, arguments[4]);
  EmitMoveImmediate(bridge, {0x49, 0xb9}, arguments[5]);
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
  *recovery_address = boundary.recovery_address;
  active_fault_boundary = &boundary;
  const auto return_value = function();
  active_fault_boundary = nullptr;
  *recovery_address = 0;
  if (boundary.caught) {
    NativeGuestExecutionResult result;
    result.status = FaultStatus(boundary.exception_code);
    result.host_exception_code = boundary.exception_code;
    result.fault_instruction_pointer = boundary.instruction_pointer;
    result.fault_address = boundary.fault_address;
    return result;
  }
#else
  (void)guest_memory_begin;
  (void)guest_memory_end;
  *recovery_address =
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
          static_cast<std::byte*>(entry_bridge.address()) + recovery_offset));
  const auto return_value = function();
  *recovery_address = 0;
#endif
  if (*control_request != kNativeControlNone) {
    return ControlResult(*control_request, *hle_status);
  }
  return {NativeGuestExecutionStatus::kOk, return_value};
}

bool WriteGuestUInt64(memory::GuestMemory& memory, std::uint64_t address,
                      std::uint64_t value) {
  std::array<std::byte, sizeof(value)> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  return memory.Write(address, bytes);
}

NativeGuestExecutionResult RunGuestContinuationBridge(
    memory::GuestMemory& memory, std::uint64_t resume_instruction_pointer,
    std::uint64_t resume_stack_pointer, std::uint64_t root_return_slot,
    std::uint64_t return_value,
    const std::array<std::uint64_t, 6>& nonvolatile_registers,
    const std::array<std::byte, 512>& floating_state,
    volatile std::uint64_t* host_stack_slot,
    volatile std::uint64_t* recovery_address,
    volatile std::uint64_t* control_request,
    hle::HleContextStatus* hle_status) {
  std::vector<std::byte> bridge;
  bridge.reserve(kNativeGuestBridgeBytes);

  Emit(bridge, {0x53, 0x55, 0x57, 0x56});
  Emit(bridge, {0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
  Emit(bridge, {0x48, 0x81, 0xec, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x48, 0x0f, 0xae, 0x04, 0x24});
  EmitMoveImmediate(bridge, {0x48, 0xb8},
                    static_cast<std::uint64_t>(
                        reinterpret_cast<std::uintptr_t>(host_stack_slot)));
  Emit(bridge, {0x48, 0x89, 0x20});

  EmitMoveImmediate(bridge, {0x48, 0xb8},
                    static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                        floating_state.data())));
  Emit(bridge, {0x48, 0x0f, 0xae, 0x08});
  EmitMoveImmediate(bridge, {0x48, 0xbb}, nonvolatile_registers[5]);
  EmitMoveImmediate(bridge, {0x48, 0xbd}, nonvolatile_registers[4]);
  EmitMoveImmediate(bridge, {0x49, 0xbc}, nonvolatile_registers[3]);
  EmitMoveImmediate(bridge, {0x49, 0xbd}, nonvolatile_registers[2]);
  EmitMoveImmediate(bridge, {0x49, 0xbe}, nonvolatile_registers[1]);
  EmitMoveImmediate(bridge, {0x49, 0xbf}, nonvolatile_registers[0]);
  EmitMoveImmediate(bridge, {0x49, 0xbb}, resume_instruction_pointer);
  EmitMoveImmediate(bridge, {0x48, 0xb8}, return_value);
  EmitMoveImmediate(bridge, {0x48, 0xbc}, resume_stack_pointer);
  Emit(bridge, {0x41, 0xff, 0xe3});

  const auto guest_return_offset = bridge.size();
  Emit(bridge, {0x49, 0x89, 0xc3});
  EmitMoveImmediate(bridge, {0x48, 0xb8},
                    static_cast<std::uint64_t>(
                        reinterpret_cast<std::uintptr_t>(host_stack_slot)));
  Emit(bridge, {0x48, 0x8b, 0x20});
  Emit(bridge, {0x48, 0xc7, 0x00, 0x00, 0x00, 0x00, 0x00});
  Emit(bridge, {0x48, 0x0f, 0xae, 0x0c, 0x24});
  Emit(bridge, {0x48, 0x81, 0xc4, 0x08, 0x02, 0x00, 0x00});
  Emit(bridge, {0x4c, 0x89, 0xd8});
  Emit(bridge, {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c});
  Emit(bridge, {0x5e, 0x5f, 0x5d, 0x5b, 0xc3});

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

  HostExecutableBuffer resume_bridge(kNativeGuestBridgeBytes);
  if (!resume_bridge.allocated() || !resume_bridge.Write(bridge)) {
    return {NativeGuestExecutionStatus::kHostAllocationFailed, 0};
  }
  const auto guest_return_address =
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
          static_cast<std::byte*>(resume_bridge.address()) +
          guest_return_offset));
  if (!WriteGuestUInt64(memory, root_return_slot, guest_return_address)) {
    return {NativeGuestExecutionStatus::kGuestStackNotAccessible, 0};
  }
  if (!resume_bridge.Seal()) {
    return {NativeGuestExecutionStatus::kHostProtectionFailed, 0};
  }

  using ResumeBridge = std::uint64_t (*)();
  const auto function = reinterpret_cast<ResumeBridge>(resume_bridge.address());
  const auto recovery =
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
          static_cast<std::byte*>(resume_bridge.address()) + recovery_offset));
  *recovery_address = recovery;
#if defined(_WIN32)
  if (!EnsureNativeGuestExceptionHandler()) {
    *recovery_address = 0;
    return {NativeGuestExecutionStatus::kFaultBoundaryUnavailable, 0};
  }
  NativeGuestFaultBoundary boundary;
  boundary.guest_begin = memory.base_address();
  boundary.guest_end = memory.end_address();
  boundary.recovery_address = recovery;
  boundary.host_stack_slot = host_stack_slot;
  active_fault_boundary = &boundary;
  const auto guest_return_value = function();
  active_fault_boundary = nullptr;
  *recovery_address = 0;
  if (boundary.caught) {
    NativeGuestExecutionResult result;
    result.status = FaultStatus(boundary.exception_code);
    result.host_exception_code = boundary.exception_code;
    result.fault_instruction_pointer = boundary.instruction_pointer;
    result.fault_address = boundary.fault_address;
    return result;
  }
#else
  const auto guest_return_value = function();
  *recovery_address = 0;
#endif
  if (*control_request != kNativeControlNone) {
    return ControlResult(*control_request, *hle_status);
  }
  return {NativeGuestExecutionStatus::kOk, guest_return_value};
}

#endif

}  // namespace

NativeGuestExecutionResult NativeGuestExecutor::Execute(
    memory::GuestMemory& memory, std::uint64_t entry_point,
    std::uint64_t stack_address, std::uint64_t stack_size,
    std::uint64_t parameters_address, std::uint64_t exit_handler_address,
    NativeGuestExecutionContext* execution_context) const {
  const std::array arguments = {parameters_address, exit_handler_address};
  return ExecuteEntry(memory, entry_point, stack_address, stack_size, arguments,
                      sizeof(NativeGuestEntryParameters), execution_context);
}

NativeGuestExecutionResult NativeGuestExecutor::ExecuteThread(
    memory::GuestMemory& memory, std::uint64_t entry_point,
    std::uint64_t stack_address, std::uint64_t stack_size,
    std::uint64_t argument,
    NativeGuestExecutionContext* execution_context) const {
  const std::array arguments = {argument};
  return ExecuteEntry(memory, entry_point, stack_address, stack_size, arguments,
                      0, execution_context);
}

NativeGuestExecutionResult NativeGuestExecutor::ExecuteFunction(
    memory::GuestMemory& memory, std::uint64_t entry_point,
    std::uint64_t stack_address, std::uint64_t stack_size,
    std::span<const std::uint64_t> arguments,
    NativeGuestExecutionContext* execution_context) const {
  return ExecuteEntry(memory, entry_point, stack_address, stack_size, arguments,
                      0, execution_context);
}

NativeGuestExecutionResult NativeGuestExecutor::ExecuteEntry(
    memory::GuestMemory& memory, std::uint64_t entry_point,
    std::uint64_t stack_address, std::uint64_t stack_size,
    std::span<const std::uint64_t> arguments,
    std::uint64_t readable_first_argument_size,
    NativeGuestExecutionContext* execution_context) const {
  NativeGuestExecutionContext local_context;
  auto* const context =
      execution_context != nullptr ? execution_context : &local_context;
  if (arguments.size() > 6 || context->active() ||
      context->control_request_ != NativeGuestExecutionContext::kControlNone) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
#if defined(_WIN32)
  if (active_fault_boundary != nullptr) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
#endif
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
  if (readable_first_argument_size != 0 &&
      (arguments.empty() ||
       !memory.CanAccess(arguments[0], readable_first_argument_size,
                         memory::GuestMemoryProtection::kRead))) {
    return {NativeGuestExecutionStatus::kGuestParametersNotReadable, 0};
  }

  std::array<std::uint64_t, 6> argument_registers{};
  std::copy(arguments.begin(), arguments.end(), argument_registers.begin());

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
  context->guest_memory_base_ = memory.base_address();
  context->guest_memory_end_ = memory.end_address();
  context->root_return_slot_ = stack_top - sizeof(std::uint64_t);
  const auto result = RunGuestEntry(memory, entry_point, stack_top, root_frame,
                                    argument_registers, *context);
  if (result.status != NativeGuestExecutionStatus::kHleBlocked &&
      result.status != NativeGuestExecutionStatus::kHleYielded) {
    ResetExecutionContext(*context);
  }
  return result;
#endif
}

NativeGuestExecutionResult NativeGuestExecutor::Resume(
    memory::GuestMemory& memory,
    NativeGuestExecutionContext& execution_context) const {
#if !defined(_M_X64) && !defined(__x86_64__)
  (void)memory;
  (void)execution_context;
  return {NativeGuestExecutionStatus::kUnsupportedHost, 0};
#else
  const auto resume_arguments_address = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(execution_context.resume_arguments_));
  constexpr auto captured_call_frame_bytes = 7 * sizeof(std::uint64_t);
  if (!memory.host_mapped() || execution_context.active() ||
      !execution_context.suspended() ||
      execution_context.guest_memory_base_ != memory.base_address() ||
      execution_context.guest_memory_end_ != memory.end_address() ||
      execution_context.resume_hle_dispatch_ == nullptr ||
      execution_context.resume_hle_state_ == nullptr ||
      execution_context.resume_arguments_ == nullptr ||
      resume_arguments_address > std::numeric_limits<std::uint64_t>::max() -
                                     captured_call_frame_bytes ||
      resume_arguments_address + captured_call_frame_bytes !=
          execution_context.resume_stack_pointer_ ||
      !memory.CanAccess(resume_arguments_address, captured_call_frame_bytes,
                        memory::GuestMemoryProtection::kRead) ||
      !memory.CanExecute(execution_context.resume_instruction_pointer_, 1) ||
      !memory.CanAccess(execution_context.root_return_slot_,
                        sizeof(std::uint64_t),
                        memory::GuestMemoryProtection::kRead |
                            memory::GuestMemoryProtection::kWrite)) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
#if defined(_WIN32)
  if (active_fault_boundary != nullptr) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }
#endif

  execution_context.control_request_ =
      NativeGuestExecutionContext::kControlNone;
  execution_context.hle_status_ = hle::HleContextStatus::kOk;
  auto hle_return = execution_context.completed_hle_return_value_;
  if (execution_context.retry_hle_on_resume_) {
    execution_context.retrying_hle_dispatch_ = true;
    hle_return = execution_context.resume_hle_dispatch_(
        execution_context.resume_hle_state_,
        execution_context.resume_arguments_,
        execution_context.floating_state_.data(),
        execution_context.nonvolatile_registers_.data());
    execution_context.retrying_hle_dispatch_ = false;
  }
  if (execution_context.control_request_ !=
      NativeGuestExecutionContext::kControlNone) {
    const auto result = ControlResult(execution_context.control_request_,
                                      execution_context.hle_status_);
    if (result.status != NativeGuestExecutionStatus::kHleBlocked &&
        result.status != NativeGuestExecutionStatus::kHleYielded) {
      ResetExecutionContext(execution_context);
    }
    return result;
  }

  const auto result =
      RunGuestContinuation(memory, execution_context, hle_return);
  if (result.status != NativeGuestExecutionStatus::kHleBlocked &&
      result.status != NativeGuestExecutionStatus::kHleYielded) {
    ResetExecutionContext(execution_context);
  }
  return result;
#endif
}

NativeGuestExecutionResult NativeGuestExecutor::Resume(
    memory::GuestMemory& memory, NativeGuestContinuation& continuation,
    NativeGuestExecutionContext& execution_context) const {
  if (!continuation.valid_ || execution_context.active() ||
      execution_context.control_request_ !=
          NativeGuestExecutionContext::kControlNone ||
      !memory.host_mapped() ||
      continuation.guest_memory_base_ != memory.base_address() ||
      continuation.guest_memory_end_ != memory.end_address()) {
    return {NativeGuestExecutionStatus::kInvalidArgument, 0};
  }

  execution_context.control_request_ =
      NativeGuestExecutionContext::kControlBlocked;
  execution_context.hle_status_ = continuation.hle_status_;
  execution_context.retry_hle_on_resume_ =
      continuation.retry_hle_on_resume_ != 0;
  execution_context.completed_hle_return_value_ =
      continuation.completed_hle_return_value_;
  execution_context.resume_hle_dispatch_ = continuation.resume_hle_dispatch_;
  execution_context.resume_hle_state_ = continuation.resume_hle_state_;
  execution_context.resume_arguments_ = continuation.resume_arguments_;
  execution_context.resume_instruction_pointer_ =
      continuation.resume_instruction_pointer_;
  execution_context.resume_stack_pointer_ = continuation.resume_stack_pointer_;
  execution_context.root_return_slot_ = continuation.root_return_slot_;
  execution_context.guest_memory_base_ = continuation.guest_memory_base_;
  execution_context.guest_memory_end_ = continuation.guest_memory_end_;
  execution_context.nonvolatile_registers_ =
      continuation.nonvolatile_registers_;
  execution_context.floating_state_ = continuation.floating_state_;
  ResetContinuation(continuation);
  return Resume(memory, execution_context);
}

bool NativeGuestExecutor::TakeContinuation(
    NativeGuestExecutionContext& execution_context,
    NativeGuestContinuation& continuation) const noexcept {
  if (!execution_context.suspended() || continuation.valid_) {
    return false;
  }

  continuation.valid_ = true;
  continuation.hle_status_ = execution_context.hle_status_;
  continuation.retry_hle_on_resume_ =
      execution_context.retry_hle_on_resume_ ? 1 : 0;
  continuation.completed_hle_return_value_ =
      execution_context.completed_hle_return_value_;
  continuation.resume_hle_dispatch_ = execution_context.resume_hle_dispatch_;
  continuation.resume_hle_state_ = execution_context.resume_hle_state_;
  continuation.resume_arguments_ = execution_context.resume_arguments_;
  continuation.resume_instruction_pointer_ =
      execution_context.resume_instruction_pointer_;
  continuation.resume_stack_pointer_ = execution_context.resume_stack_pointer_;
  continuation.root_return_slot_ = execution_context.root_return_slot_;
  continuation.guest_memory_base_ = execution_context.guest_memory_base_;
  continuation.guest_memory_end_ = execution_context.guest_memory_end_;
  continuation.nonvolatile_registers_ =
      execution_context.nonvolatile_registers_;
  continuation.floating_state_ = execution_context.floating_state_;
  ResetExecutionContext(execution_context);
  return true;
}

NativeGuestExecutionResult NativeGuestExecutor::RunGuestEntry(
    memory::GuestMemory& memory, std::uint64_t entry_point,
    std::uint64_t stack_top, std::uint64_t root_frame,
    const std::array<std::uint64_t, 6>& arguments,
    NativeGuestExecutionContext& execution_context) {
#if !defined(_M_X64) && !defined(__x86_64__)
  (void)memory;
  (void)entry_point;
  (void)stack_top;
  (void)root_frame;
  (void)arguments;
  (void)execution_context;
  return {NativeGuestExecutionStatus::kUnsupportedHost, 0};
#else
  return RunGuestEntryBridge(
      entry_point, memory.base_address(), memory.end_address(), stack_top,
      root_frame, arguments, &execution_context.host_stack_pointer_,
      &execution_context.recovery_address_, &execution_context.control_request_,
      &execution_context.hle_status_);
#endif
}

NativeGuestExecutionResult NativeGuestExecutor::RunGuestContinuation(
    memory::GuestMemory& memory, NativeGuestExecutionContext& execution_context,
    std::uint64_t hle_return_value) {
#if !defined(_M_X64) && !defined(__x86_64__)
  (void)memory;
  (void)execution_context;
  (void)hle_return_value;
  return {NativeGuestExecutionStatus::kUnsupportedHost, 0};
#else
  return RunGuestContinuationBridge(
      memory, execution_context.resume_instruction_pointer_,
      execution_context.resume_stack_pointer_,
      execution_context.root_return_slot_, hle_return_value,
      execution_context.nonvolatile_registers_,
      execution_context.floating_state_, &execution_context.host_stack_pointer_,
      &execution_context.recovery_address_, &execution_context.control_request_,
      &execution_context.hle_status_);
#endif
}

void NativeGuestExecutor::ResetExecutionContext(
    NativeGuestExecutionContext& execution_context) noexcept {
  execution_context.host_stack_pointer_ = 0;
  execution_context.recovery_address_ = 0;
  execution_context.control_request_ =
      NativeGuestExecutionContext::kControlNone;
  execution_context.retrying_hle_dispatch_ = false;
  execution_context.hle_status_ = hle::HleContextStatus::kOk;
  execution_context.retry_hle_on_resume_ = true;
  execution_context.completed_hle_return_value_ = 0;
  execution_context.resume_hle_dispatch_ = nullptr;
  execution_context.resume_hle_state_ = nullptr;
  execution_context.resume_arguments_ = nullptr;
  execution_context.resume_instruction_pointer_ = 0;
  execution_context.resume_stack_pointer_ = 0;
  execution_context.root_return_slot_ = 0;
  execution_context.guest_memory_base_ = 0;
  execution_context.guest_memory_end_ = 0;
  execution_context.nonvolatile_registers_.fill(0);
  execution_context.floating_state_.fill(std::byte{0});
}

void NativeGuestExecutor::ResetContinuation(
    NativeGuestContinuation& continuation) noexcept {
  continuation.valid_ = false;
  continuation.hle_status_ = hle::HleContextStatus::kOk;
  continuation.retry_hle_on_resume_ = 1;
  continuation.completed_hle_return_value_ = 0;
  continuation.resume_hle_dispatch_ = nullptr;
  continuation.resume_hle_state_ = nullptr;
  continuation.resume_arguments_ = nullptr;
  continuation.resume_instruction_pointer_ = 0;
  continuation.resume_stack_pointer_ = 0;
  continuation.root_return_slot_ = 0;
  continuation.guest_memory_base_ = 0;
  continuation.guest_memory_end_ = 0;
  continuation.nonvolatile_registers_.fill(0);
  continuation.floating_state_.fill(std::byte{0});
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
    case NativeGuestExecutionStatus::kHleBlocked:
      return "hle-blocked";
    case NativeGuestExecutionStatus::kHleYielded:
      return "hle-yielded";
    case NativeGuestExecutionStatus::kHleDispatchFailed:
      return "hle-dispatch-failed";
    case NativeGuestExecutionStatus::kGuestExit:
      return "guest-exit";
    case NativeGuestExecutionStatus::kHostAllocationFailed:
      return "host-allocation-failed";
    case NativeGuestExecutionStatus::kHostProtectionFailed:
      return "host-protection-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
