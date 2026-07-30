// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/json_value.h"

namespace kajps5::kernel {

KernelStatus JsonValueService::Construct(std::uint64_t guest_address) {
  if (guest_address == 0) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  const auto found = values_.find(guest_address);
  if (found != values_.end()) {
    found->second = JsonValueKind::kNull;
    return KernelStatus::kOk;
  }
  if (values_.size() >= kMaximumJsonValueShadows) {
    return KernelStatus::kNoResources;
  }
  values_.emplace(guest_address, JsonValueKind::kNull);
  return KernelStatus::kOk;
}

KernelStatus JsonValueService::Destroy(std::uint64_t guest_address) {
  if (guest_address == 0) {
    return KernelStatus::kOk;
  }
  std::lock_guard lock(mutex_);
  values_.erase(guest_address);
  return KernelStatus::kOk;
}

bool JsonValueService::IsTracked(std::uint64_t guest_address) const {
  std::lock_guard lock(mutex_);
  return values_.contains(guest_address);
}

std::size_t JsonValueService::size() const {
  std::lock_guard lock(mutex_);
  return values_.size();
}

}  // namespace kajps5::kernel
