// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_hle_trampoline.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <span>
#include <utility>

#include "core/memory/guest_memory.h"
#include "cpu/host_executable_buffer.h"
#include "cpu/native_guest_executor.h"
#include "hle/call_context.h"
#include "hle/libc_exports.h"

namespace kajps5::cpu {
namespace {

constexpr std::size_t kNativeHleTrampolineBytes = 256;
constexpr std::size_t kFxSaveXmmOffset = 160;

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

bool IsHotMemoryCopySymbol(std::string_view symbol) noexcept {
  return symbol == hle::kLibcMemcpyName || symbol == hle::kLibcMemcpyNid ||
         symbol == hle::kLibcMemmoveName || symbol == hle::kLibcMemmoveNid;
}

}  // namespace

struct NativeHleTrampoline::State {
  memory::GuestMemory* memory = nullptr;
  const hle::ExportRegistry* registry = nullptr;
  std::string symbol;
  std::string resolved_library;
  std::vector<std::string> library_order;
  std::size_t stack_argument_count = 0;
  NativeGuestExecutionContext* execution_context = nullptr;
  mutable std::mutex mutex;
  NativeHleDispatchSnapshot last_dispatch;
};

NativeHleTrampoline::NativeHleTrampoline(
    memory::GuestMemory& memory, const hle::ExportRegistry& registry,
    std::string symbol, std::vector<std::string> library_order,
    std::size_t stack_argument_count,
    NativeGuestExecutionContext* execution_context)
    : state_(std::make_unique<State>()) {
  state_->memory = &memory;
  state_->registry = &registry;
  state_->symbol = std::move(symbol);
  state_->library_order = std::move(library_order);
  state_->stack_argument_count = stack_argument_count;
  state_->execution_context = execution_context;
  Build();
}

NativeHleTrampoline::~NativeHleTrampoline() = default;

NativeHleTrampolineStatus NativeHleTrampoline::status() const noexcept {
  return status_;
}

std::uint64_t NativeHleTrampoline::address() const noexcept {
  return status_ == NativeHleTrampolineStatus::kOk
             ? static_cast<std::uint64_t>(
                   reinterpret_cast<std::uintptr_t>(code_->address()))
             : 0;
}

NativeHleDispatchSnapshot NativeHleTrampoline::last_dispatch() const {
  std::lock_guard lock(state_->mutex);
  return state_->last_dispatch;
}

std::uint64_t NativeHleTrampoline::Dispatch(
    void* opaque_state, const std::uint64_t* arguments,
    std::byte* floating_state,
    const std::uint64_t* nonvolatile_registers) noexcept {
  auto* state = static_cast<State*>(opaque_state);
  if (state == nullptr || arguments == nullptr || floating_state == nullptr ||
      nonvolatile_registers == nullptr || state->memory == nullptr ||
      state->registry == nullptr) {
    return 0;
  }

  try {
    if (state->resolved_library == hle::kLibcName &&
        IsHotMemoryCopySymbol(state->symbol)) {
      const auto destination = arguments[0];
      const auto source = arguments[1];
      const auto length = arguments[2];
      if (length == 0 || state->memory->Copy(destination, source, length)) {
        std::lock_guard lock(state->mutex);
        state->last_dispatch = {};
        state->last_dispatch.lookup_status = hle::ExportRegistryStatus::kOk;
        state->last_dispatch.handler_status = hle::HleContextStatus::kOk;
        state->last_dispatch.return_written = true;
        state->last_dispatch.library = state->resolved_library;
        return destination;
      }
    }

    hle::HleCallContext context(*state->memory);
    constexpr std::array registers = {
        hle::HleRegister::kRdi, hle::HleRegister::kRsi, hle::HleRegister::kRdx,
        hle::HleRegister::kRcx, hle::HleRegister::kR8,  hle::HleRegister::kR9};
    for (std::size_t index = 0; index < registers.size(); ++index) {
      (void)context.SetRegister(registers[index], arguments[index]);
    }
    if (!context.SetCapturedStackArguments(std::span(
            arguments + registers.size() + 1, state->stack_argument_count))) {
      std::lock_guard lock(state->mutex);
      state->last_dispatch = {};
      state->last_dispatch.lookup_status =
          hle::ExportRegistryStatus::kInvalidArgument;
      state->last_dispatch.handler_status =
          hle::HleContextStatus::kInvalidArgument;
      return 0;
    }
    std::array<hle::HleVectorValue, hle::kHleVectorArgumentRegisterCount>
        vector_arguments{};
    for (std::size_t index = 0; index < vector_arguments.size(); ++index) {
      std::memcpy(vector_arguments[index].data(),
                  floating_state + kFxSaveXmmOffset +
                      index * hle::kHleVectorRegisterBytes,
                  hle::kHleVectorRegisterBytes);
    }
    (void)context.SetCapturedVectorArguments(vector_arguments);
    const auto result =
        state->registry->Dispatch(state->symbol, state->library_order, context);
    const auto return_value =
        context.GetRegister(hle::HleRegister::kRax).value_or(0);
    for (std::size_t index = 0; index < hle::kHleVectorReturnRegisterCount;
         ++index) {
      const auto vector_return = context.VectorReturn(index);
      if (vector_return) {
        std::memcpy(floating_state + kFxSaveXmmOffset +
                        index * hle::kHleVectorRegisterBytes,
                    vector_return->data(), vector_return->size());
      }
    }
    auto* const execution_context = state->execution_context;
    if (execution_context != nullptr &&
        (execution_context->active() ||
         execution_context->retrying_hle_dispatch_)) {
      execution_context->hle_status_ =
          result.status == hle::ExportRegistryStatus::kOk
              ? result.handler_status
              : hle::HleContextStatus::kInvalidArgument;
      const auto blocked =
          execution_context->hle_status_ == hle::HleContextStatus::kBlocked;
      const auto yielded =
          execution_context->hle_status_ == hle::HleContextStatus::kOk &&
          context.yield_requested();
      if (blocked || yielded) {
        execution_context->retry_hle_on_resume_ = blocked;
        execution_context->completed_hle_return_value_ = return_value;
        execution_context->resume_hle_dispatch_ = &Dispatch;
        execution_context->resume_hle_state_ = state;
        execution_context->resume_arguments_ = arguments;
        execution_context->resume_instruction_pointer_ = arguments[6];
        execution_context->resume_stack_pointer_ = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(arguments + 7));
        std::copy_n(nonvolatile_registers,
                    execution_context->nonvolatile_registers_.size(),
                    execution_context->nonvolatile_registers_.begin());
        std::memcpy(execution_context->floating_state_.data(), floating_state,
                    execution_context->floating_state_.size());
        execution_context->control_request_ =
            blocked ? NativeGuestExecutionContext::kControlBlocked
                    : NativeGuestExecutionContext::kControlYielded;
      } else if (execution_context->hle_status_ != hle::HleContextStatus::kOk) {
        execution_context->control_request_ =
            NativeGuestExecutionContext::kControlStopped;
      }
    }
    {
      std::lock_guard lock(state->mutex);
      state->last_dispatch = {};
      state->last_dispatch.lookup_status = result.status;
      state->last_dispatch.handler_status = result.handler_status;
      state->last_dispatch.return_written = context.return_written();
      for (std::size_t index = 0;
           index < state->last_dispatch.vector_return_written.size(); ++index) {
        state->last_dispatch.vector_return_written[index] =
            context.vector_return_written(index);
      }
      state->last_dispatch.library = result.library;
    }
    return return_value;
  } catch (...) {
    auto* const execution_context = state->execution_context;
    if (execution_context != nullptr &&
        (execution_context->active() ||
         execution_context->retrying_hle_dispatch_)) {
      execution_context->hle_status_ = hle::HleContextStatus::kFatalGuestError;
      execution_context->control_request_ =
          NativeGuestExecutionContext::kControlStopped;
    }
    std::lock_guard lock(state->mutex);
    state->last_dispatch = {};
    state->last_dispatch.host_exception = true;
    return 0;
  }
}

void NativeHleTrampoline::Build() {
  const auto lookup =
      state_->registry->Lookup(state_->symbol, state_->library_order);
  if (state_->symbol.empty() || state_->library_order.empty() ||
      state_->stack_argument_count > hle::kMaximumCapturedHleStackArguments ||
      !lookup) {
    status_ = NativeHleTrampolineStatus::kInvalidArgument;
    return;
  }
  state_->resolved_library = lookup.library;

#if !defined(_M_X64) && !defined(__x86_64__)
  status_ = NativeHleTrampolineStatus::kUnsupportedHost;
  return;
#else
  std::vector<std::byte> code;
  code.reserve(kNativeHleTrampolineBytes);
  // Build an argument pack in System V register order.
  Emit(code, {0x41, 0x51, 0x41, 0x50, 0x51, 0x52, 0x56, 0x57});
#if defined(_WIN32)
  // Keep Windows shadow space below an aligned FXSAVE64 register image.
  Emit(code, {0x48, 0x81, 0xec, 0x28, 0x02, 0x00, 0x00});
  Emit(code, {0x48, 0x0f, 0xae, 0x44, 0x24, 0x20});
  // Windows passes state, integer pack, floating state, and saved nonvolatile
  // registers in RCX/RDX/R8/R9.
  Emit(code, {0x48, 0x8d, 0x94, 0x24, 0x28, 0x02, 0x00, 0x00});
  Emit(code, {0x4c, 0x8d, 0x44, 0x24, 0x20, 0x48, 0xb9});
#else
  // Keep an aligned FXSAVE64 register image below the integer pack.
  Emit(code, {0x48, 0x81, 0xec, 0x08, 0x02, 0x00, 0x00});
  Emit(code, {0x48, 0x0f, 0xae, 0x04, 0x24});
  // System V passes state, integer pack, floating state, and saved nonvolatile
  // registers in RDI/RSI/RDX/RCX.
  Emit(code, {0x48, 0x8d, 0xb4, 0x24, 0x08, 0x02, 0x00, 0x00});
  Emit(code, {0x48, 0x89, 0xe2, 0x48, 0xbf});
#endif
  EmitUInt64(code, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(state_.get())));
  Emit(code, {0x48, 0xb8});
  EmitUInt64(code, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(&Dispatch)));
  if (state_->execution_context == nullptr) {
    Emit(code, {0x48, 0x83, 0xec, 0x30});
#if defined(_WIN32)
    Emit(code, {0x49, 0x89, 0xe1});
#else
    Emit(code, {0x48, 0x89, 0xe1});
#endif
    Emit(code, {0xff, 0xd0});
    Emit(code, {0x48, 0x83, 0xc4, 0x30});
  } else {
    // SharpEmu's direct backend returns to the saved host stack before a host
    // handler runs. Preserve the guest nonvolatile registers and return to the
    // guest stack after dispatch. An inactive context returns zero instead of
    // changing RSP.
    Emit(code, {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
    Emit(code, {0x49, 0x89, 0xe4});
#if defined(_WIN32)
    Emit(code, {0x49, 0x89, 0xe1});
#else
    Emit(code, {0x48, 0x89, 0xe1});
#endif
    Emit(code, {0x49, 0xbb});
    EmitUInt64(code,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                   &state_->execution_context->host_stack_pointer_)));
    Emit(code, {0x4d, 0x8b, 0x1b, 0x4d, 0x85, 0xdb});
    Emit(code, {0x75, 0x05, 0x45, 0x31, 0xd2});
#if defined(_WIN32)
    Emit(code, {0xeb, 0x0c, 0x4c, 0x89, 0xdc});
    Emit(code, {0x48, 0x83, 0xec, 0x20});
#else
    Emit(code, {0xeb, 0x08, 0x4c, 0x89, 0xdc});
#endif
    Emit(code, {0xff, 0xd0, 0x49, 0x89, 0xc2});
    Emit(code, {0x48, 0xb8});
    EmitUInt64(code,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                   &state_->execution_context->control_request_)));
    Emit(code, {0x48, 0x83, 0x38, 0x00, 0x74, 0x00});
    const auto normal_return_jump = code.size() - 1;
#if defined(_WIN32)
    Emit(code, {0x48, 0x83, 0xc4, 0x20});
#endif
    Emit(code, {0x48, 0xb8});
    EmitUInt64(code,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                   &state_->execution_context->recovery_address_)));
    Emit(code, {0x48, 0x8b, 0x00, 0xff, 0xe0});
    const auto normal_return = code.size();
    code[normal_return_jump] =
        static_cast<std::byte>(normal_return - (normal_return_jump + 1));
    Emit(code, {0x4c, 0x89, 0xe4});
    Emit(code, {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b});
  }
#if defined(_WIN32)
  Emit(code, {0x48, 0x0f, 0xae, 0x4c, 0x24, 0x20});
  Emit(code, {0x48, 0x81, 0xc4, 0x28, 0x02, 0x00, 0x00});
#else
  Emit(code, {0x48, 0x0f, 0xae, 0x0c, 0x24});
  Emit(code, {0x48, 0x81, 0xc4, 0x08, 0x02, 0x00, 0x00});
#endif
  if (state_->execution_context != nullptr) {
    Emit(code, {0x4c, 0x89, 0xd0});
  }
  Emit(code, {0x48, 0x83, 0xc4, 0x30, 0xc3});

  code_ = std::make_unique<HostExecutableBuffer>(kNativeHleTrampolineBytes);
  if (!code_->allocated() || !code_->Write(code)) {
    code_.reset();
    status_ = NativeHleTrampolineStatus::kHostAllocationFailed;
    return;
  }
  if (!code_->Seal()) {
    code_.reset();
    status_ = NativeHleTrampolineStatus::kHostProtectionFailed;
    return;
  }
  status_ = NativeHleTrampolineStatus::kOk;
#endif
}

std::string_view NativeHleTrampolineStatusName(
    NativeHleTrampolineStatus status) noexcept {
  switch (status) {
    case NativeHleTrampolineStatus::kOk:
      return "ok";
    case NativeHleTrampolineStatus::kUnsupportedHost:
      return "unsupported-host";
    case NativeHleTrampolineStatus::kInvalidArgument:
      return "invalid-argument";
    case NativeHleTrampolineStatus::kHostAllocationFailed:
      return "host-allocation-failed";
    case NativeHleTrampolineStatus::kHostProtectionFailed:
      return "host-protection-failed";
  }
  return "unknown";
}

}  // namespace kajps5::cpu
