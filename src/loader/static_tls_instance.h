// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "core/memory/guest_memory.h"
#include "loader/static_tls_layout.h"

namespace kajps5::loader {

inline constexpr std::uint64_t kStaticTlsThreadControlBlockBytes = 0x80;
inline constexpr std::uint64_t kStaticTlsThreadPointerAlignment = 0x20;
inline constexpr std::uint64_t kStaticTlsStackGuard = 0xC0DEC0DECAFEBA00ULL;
inline constexpr std::uint64_t kStaticTlsDtvHeaderBytes = 16;
inline constexpr std::uint64_t kStaticTlsDtvGeneration = 1;

struct StaticTlsTemplateModule {
  std::uint64_t module_id = 0;
  std::uint64_t image_address = 0;
  std::uint64_t initial_size = 0;
  std::uint64_t memory_size = 0;
  std::uint64_t static_offset = 0;
};

struct StaticTlsInstance {
  std::uint64_t allocation_address = 0;
  std::uint64_t allocation_size = 0;
  std::uint64_t thread_pointer = 0;
  std::uint64_t dtv_address = 0;
};

enum class StaticTlsInstanceStatus {
  kOk,
  kInvalidArgument,
  kOverflow,
  kAllocationFailed,
  kCopyFailed,
  kWriteFailed,
};

struct StaticTlsInstanceResult {
  StaticTlsInstanceStatus status = StaticTlsInstanceStatus::kOk;
  StaticTlsInstance instance;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == StaticTlsInstanceStatus::kOk;
  }
};

[[nodiscard]] StaticTlsInstanceResult CreateStaticTlsInstance(
    memory::GuestMemory& memory, const StaticTlsLayout& layout,
    std::span<const StaticTlsTemplateModule> modules,
    std::uint64_t search_start);
[[nodiscard]] bool DestroyStaticTlsInstance(
    memory::GuestMemory& memory, const StaticTlsInstance& instance) noexcept;
[[nodiscard]] std::string_view StaticTlsInstanceStatusName(
    StaticTlsInstanceStatus status) noexcept;

}  // namespace kajps5::loader
