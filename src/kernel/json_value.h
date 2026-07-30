// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

#include "kernel/status.h"

namespace kajps5::kernel {

inline constexpr std::size_t kMaximumJsonValueShadows = 4096;

enum class JsonValueKind : std::uint8_t {
  kNull,
};

class JsonValueService final {
 public:
  [[nodiscard]] KernelStatus Construct(std::uint64_t guest_address);
  [[nodiscard]] KernelStatus Destroy(std::uint64_t guest_address);
  [[nodiscard]] bool IsTracked(std::uint64_t guest_address) const;
  [[nodiscard]] std::size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::map<std::uint64_t, JsonValueKind> values_;
};

}  // namespace kajps5::kernel
