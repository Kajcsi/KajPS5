// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <span>

namespace kajps5::cpu {

class HostExecutableBuffer final {
 public:
  explicit HostExecutableBuffer(std::size_t size) noexcept;
  ~HostExecutableBuffer();

  HostExecutableBuffer(const HostExecutableBuffer&) = delete;
  HostExecutableBuffer& operator=(const HostExecutableBuffer&) = delete;

  [[nodiscard]] bool allocated() const noexcept;
  [[nodiscard]] bool Write(std::span<const std::byte> bytes,
                           std::size_t offset = 0) noexcept;
  [[nodiscard]] bool Seal() noexcept;
  [[nodiscard]] void* address() const noexcept;

 private:
  void* address_ = nullptr;
  std::size_t size_ = 0;
  bool sealed_ = false;
};

}  // namespace kajps5::cpu
