// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/native_hle_trampoline.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <utility>

#include "core/memory/guest_memory.h"
#include "cpu/host_executable_buffer.h"
#include "hle/call_context.h"

namespace kajps5::cpu {
namespace {

constexpr std::size_t kNativeHleTrampolineBytes = 128;

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

}  // namespace

struct NativeHleTrampoline::State {
  memory::GuestMemory* memory = nullptr;
  const hle::ExportRegistry* registry = nullptr;
  std::string symbol;
  std::vector<std::string> library_order;
  mutable std::mutex mutex;
  NativeHleDispatchSnapshot last_dispatch;
};

NativeHleTrampoline::NativeHleTrampoline(
    memory::GuestMemory& memory, const hle::ExportRegistry& registry,
    std::string symbol, std::vector<std::string> library_order)
    : state_(std::make_unique<State>()) {
  state_->memory = &memory;
  state_->registry = &registry;
  state_->symbol = std::move(symbol);
  state_->library_order = std::move(library_order);
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
    void* opaque_state, const std::uint64_t* arguments) noexcept {
  auto* state = static_cast<State*>(opaque_state);
  if (state == nullptr || arguments == nullptr || state->memory == nullptr ||
      state->registry == nullptr) {
    return 0;
  }

  try {
    hle::HleCallContext context(*state->memory);
    constexpr std::array registers = {
        hle::HleRegister::kRdi, hle::HleRegister::kRsi,
        hle::HleRegister::kRdx, hle::HleRegister::kRcx,
        hle::HleRegister::kR8,  hle::HleRegister::kR9};
    for (std::size_t index = 0; index < registers.size(); ++index) {
      (void)context.SetRegister(registers[index], arguments[index]);
    }
    const auto result = state->registry->Dispatch(
        state->symbol, state->library_order, context);
    const auto return_value =
        context.GetRegister(hle::HleRegister::kRax).value_or(0);
    {
      std::lock_guard lock(state->mutex);
      state->last_dispatch = {result.status, result.handler_status,
                              context.return_written(), false,
                              result.library};
    }
    return return_value;
  } catch (...) {
    std::lock_guard lock(state->mutex);
    state->last_dispatch.host_exception = true;
    return 0;
  }
}

void NativeHleTrampoline::Build() {
  if (state_->symbol.empty() || state_->library_order.empty() ||
      !state_->registry->Lookup(state_->symbol, state_->library_order)) {
    status_ = NativeHleTrampolineStatus::kInvalidArgument;
    return;
  }

#if !defined(_M_X64) && !defined(__x86_64__)
  status_ = NativeHleTrampolineStatus::kUnsupportedHost;
  return;
#else
  std::vector<std::byte> code;
  code.reserve(kNativeHleTrampolineBytes);
  // Build an argument pack in System V register order.
  Emit(code, {0x41, 0x51, 0x41, 0x50, 0x51, 0x52, 0x56, 0x57});
#if defined(_WIN32)
  // Windows passes the state in RCX and the argument pack in RDX.
  Emit(code, {0x48, 0x89, 0xe2, 0x48, 0xb9});
#else
  // System V passes the state in RDI and the argument pack in RSI.
  Emit(code, {0x48, 0x89, 0xe6, 0x48, 0xbf});
#endif
  EmitUInt64(code, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(state_.get())));
#if defined(_WIN32)
  Emit(code, {0x48, 0x83, 0xec, 0x28});
#else
  Emit(code, {0x48, 0x83, 0xec, 0x08});
#endif
  Emit(code, {0x48, 0xb8});
  EmitUInt64(code, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(&Dispatch)));
  Emit(code, {0xff, 0xd0});
#if defined(_WIN32)
  Emit(code, {0x48, 0x83, 0xc4, 0x28});
#else
  Emit(code, {0x48, 0x83, 0xc4, 0x08});
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
