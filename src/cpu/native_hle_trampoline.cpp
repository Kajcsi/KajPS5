// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_hle_trampoline.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <span>
#include <utility>

#include "core/memory/guest_memory.h"
#include "cpu/host_executable_buffer.h"
#include "hle/call_context.h"
#include "hle/libc_exports.h"

namespace kajps5::cpu {
namespace {

constexpr std::size_t kNativeHleTrampolineBytes = 128;
constexpr std::size_t kFxSaveXmmOffset = 160;

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
  mutable std::mutex mutex;
  NativeHleDispatchSnapshot last_dispatch;
};

NativeHleTrampoline::NativeHleTrampoline(
    memory::GuestMemory& memory, const hle::ExportRegistry& registry,
    std::string symbol, std::vector<std::string> library_order,
    std::size_t stack_argument_count)
    : state_(std::make_unique<State>()) {
  state_->memory = &memory;
  state_->registry = &registry;
  state_->symbol = std::move(symbol);
  state_->library_order = std::move(library_order);
  state_->stack_argument_count = stack_argument_count;
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
    std::byte* floating_state) noexcept {
  auto* state = static_cast<State*>(opaque_state);
  if (state == nullptr || arguments == nullptr || floating_state == nullptr ||
      state->memory == nullptr || state->registry == nullptr) {
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
        hle::HleRegister::kRdi, hle::HleRegister::kRsi,
        hle::HleRegister::kRdx, hle::HleRegister::kRcx,
        hle::HleRegister::kR8,  hle::HleRegister::kR9};
    for (std::size_t index = 0; index < registers.size(); ++index) {
      (void)context.SetRegister(registers[index], arguments[index]);
    }
    if (!context.SetCapturedStackArguments(
            std::span(arguments + registers.size() + 1,
                      state->stack_argument_count))) {
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
    const auto result = state->registry->Dispatch(
        state->symbol, state->library_order, context);
    const auto return_value =
        context.GetRegister(hle::HleRegister::kRax).value_or(0);
    for (std::size_t index = 0;
         index < hle::kHleVectorReturnRegisterCount; ++index) {
      const auto vector_return = context.VectorReturn(index);
      if (vector_return) {
        std::memcpy(floating_state + kFxSaveXmmOffset +
                        index * hle::kHleVectorRegisterBytes,
                    vector_return->data(), vector_return->size());
      }
    }
    {
      std::lock_guard lock(state->mutex);
      state->last_dispatch = {};
      state->last_dispatch.lookup_status = result.status;
      state->last_dispatch.handler_status = result.handler_status;
      state->last_dispatch.return_written = context.return_written();
      for (std::size_t index = 0;
           index < state->last_dispatch.vector_return_written.size();
           ++index) {
        state->last_dispatch.vector_return_written[index] =
            context.vector_return_written(index);
      }
      state->last_dispatch.library = result.library;
    }
    return return_value;
  } catch (...) {
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
      state_->stack_argument_count >
          hle::kMaximumCapturedHleStackArguments ||
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
  // Windows passes state, integer pack, and floating state in RCX/RDX/R8.
  Emit(code, {0x48, 0x8d, 0x94, 0x24, 0x28, 0x02, 0x00, 0x00});
  Emit(code, {0x4c, 0x8d, 0x44, 0x24, 0x20, 0x48, 0xb9});
#else
  // Keep an aligned FXSAVE64 register image below the integer pack.
  Emit(code, {0x48, 0x81, 0xec, 0x08, 0x02, 0x00, 0x00});
  Emit(code, {0x48, 0x0f, 0xae, 0x04, 0x24});
  // System V passes state, integer pack, and floating state in RDI/RSI/RDX.
  Emit(code, {0x48, 0x8d, 0xb4, 0x24, 0x08, 0x02, 0x00, 0x00});
  Emit(code, {0x48, 0x89, 0xe2, 0x48, 0xbf});
#endif
  EmitUInt64(code, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(state_.get())));
  Emit(code, {0x48, 0xb8});
  EmitUInt64(code, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(&Dispatch)));
  Emit(code, {0xff, 0xd0});
#if defined(_WIN32)
  Emit(code, {0x48, 0x0f, 0xae, 0x4c, 0x24, 0x20});
  Emit(code, {0x48, 0x81, 0xc4, 0x28, 0x02, 0x00, 0x00});
#else
  Emit(code, {0x48, 0x0f, 0xae, 0x0c, 0x24});
  Emit(code, {0x48, 0x81, 0xc4, 0x08, 0x02, 0x00, 0x00});
#endif
  Emit(code, {0x48, 0x83, 0xc4, 0x30, 0xc3});

  code_ =
      std::make_unique<HostExecutableBuffer>(kNativeHleTrampolineBytes);
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
    case NativeHleTrampolineStatus::kOk: return "ok";
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
