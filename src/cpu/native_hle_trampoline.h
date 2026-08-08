// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hle/export_registry.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::cpu {

class NativeGuestExecutionContext;

enum class NativeHleTrampolineStatus {
  kOk,
  kUnsupportedHost,
  kInvalidArgument,
  kHostAllocationFailed,
  kHostProtectionFailed,
};

struct NativeHleDispatchSnapshot {
  // This state is only true while the host is executing the matching HLE
  // dispatch. The remaining fields identify that in-flight guest call.
  bool active = false;
  std::string symbol;
  std::string library;
  std::uint64_t guest_return_instruction_pointer = 0;
  std::uint64_t guest_stack_pointer = 0;
  hle::ExportRegistryStatus lookup_status = hle::ExportRegistryStatus::kOk;
  hle::HleContextStatus handler_status = hle::HleContextStatus::kOk;
  bool return_written = false;
  std::array<bool, hle::kHleVectorReturnRegisterCount> vector_return_written{};
  bool host_exception = false;
};

class HostExecutableBuffer;

class NativeHleTrampoline final {
 public:
  NativeHleTrampoline(memory::GuestMemory& memory,
                      const hle::ExportRegistry& registry, std::string symbol,
                      std::vector<std::string> library_order,
                      std::size_t stack_argument_count = 0,
                      NativeGuestExecutionContext* execution_context = nullptr);
  ~NativeHleTrampoline();

  NativeHleTrampoline(const NativeHleTrampoline&) = delete;
  NativeHleTrampoline& operator=(const NativeHleTrampoline&) = delete;

  [[nodiscard]] NativeHleTrampolineStatus status() const noexcept;
  [[nodiscard]] std::uint64_t address() const noexcept;
  [[nodiscard]] NativeHleDispatchSnapshot last_dispatch() const;
  [[nodiscard]] NativeHleDispatchSnapshot active_dispatch() const;

 private:
  struct State;

  [[nodiscard]] static std::uint64_t Dispatch(
      void* opaque_state, const std::uint64_t* arguments,
      std::byte* floating_state,
      const std::uint64_t* nonvolatile_registers) noexcept;
  void Build();

  std::unique_ptr<State> state_;
  std::unique_ptr<HostExecutableBuffer> code_;
  NativeHleTrampolineStatus status_ =
      NativeHleTrampolineStatus::kUnsupportedHost;
};

[[nodiscard]] std::string_view NativeHleTrampolineStatusName(
    NativeHleTrampolineStatus status) noexcept;

}  // namespace kajps5::cpu
