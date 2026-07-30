// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace kajps5::kernel {

enum class KernelObjectType : std::uint8_t {
  kDirectory,
  kEventQueue,
  kEventFlag,
  kFile,
  kSemaphore,
  kThread,
  kTimer,
};

class KernelObject {
public:
  explicit KernelObject(KernelObjectType type) noexcept : type_(type) {}
  virtual ~KernelObject() = default;

  KernelObject(const KernelObject &) = delete;
  KernelObject &operator=(const KernelObject &) = delete;

  [[nodiscard]] KernelObjectType type() const noexcept { return type_; }

private:
  KernelObjectType type_;
};

} // namespace kajps5::kernel
