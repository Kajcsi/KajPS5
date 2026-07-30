// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

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

enum class NativeHleTrampolineStatus {
  kOk,
  kUnsupportedHost,
  kInvalidArgument,
  kHostAllocationFailed,
  kHostProtectionFailed,
};

struct NativeHleDispatchSnapshot {
  hle::ExportRegistryStatus lookup_status = hle::ExportRegistryStatus::kOk;
  hle::HleContextStatus handler_status = hle::HleContextStatus::kOk;
  bool return_written = false;
  bool host_exception = false;
  std::string library;
};

class HostExecutableBuffer;

class NativeHleTrampoline final {
 public:
  NativeHleTrampoline(memory::GuestMemory& memory,
                      const hle::ExportRegistry& registry,
                      std::string symbol,
                      std::vector<std::string> library_order);
  ~NativeHleTrampoline();

  NativeHleTrampoline(const NativeHleTrampoline&) = delete;
  NativeHleTrampoline& operator=(const NativeHleTrampoline&) = delete;

  [[nodiscard]] NativeHleTrampolineStatus status() const noexcept;
  [[nodiscard]] std::uint64_t address() const noexcept;
  [[nodiscard]] NativeHleDispatchSnapshot last_dispatch() const;

 private:
  struct State;

  [[nodiscard]] static std::uint64_t Dispatch(
      void* opaque_state, const std::uint64_t* arguments) noexcept;
  void Build();

  std::unique_ptr<State> state_;
  std::unique_ptr<HostExecutableBuffer> code_;
  NativeHleTrampolineStatus status_ =
      NativeHleTrampolineStatus::kUnsupportedHost;
};

[[nodiscard]] std::string_view NativeHleTrampolineStatusName(
    NativeHleTrampolineStatus status) noexcept;

}  // namespace kajps5::cpu
