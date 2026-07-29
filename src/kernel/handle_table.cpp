// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/handle_table.h"

#include <limits>
#include <utility>

namespace kajps5::kernel {

std::optional<KernelHandle>
HandleTable::Insert(std::shared_ptr<KernelObject> object) {
  if (!object) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);
  if (exhausted_) {
    return std::nullopt;
  }

  const auto handle = next_handle_;
  if (next_handle_ == std::numeric_limits<KernelHandle>::max()) {
    exhausted_ = true;
  } else {
    ++next_handle_;
  }
  objects_.emplace(handle, std::move(object));
  return handle;
}

std::shared_ptr<KernelObject>
HandleTable::Find(KernelHandle handle, KernelObjectType expected_type) const {
  if (handle == kInvalidKernelHandle) {
    return {};
  }

  std::lock_guard lock(mutex_);
  const auto found = objects_.find(handle);
  if (found == objects_.end() || found->second->type() != expected_type) {
    return {};
  }
  return found->second;
}

std::shared_ptr<KernelObject>
HandleTable::Remove(KernelHandle handle, KernelObjectType expected_type) {
  if (handle == kInvalidKernelHandle) {
    return {};
  }

  std::lock_guard lock(mutex_);
  const auto found = objects_.find(handle);
  if (found == objects_.end() || found->second->type() != expected_type) {
    return {};
  }

  auto object = std::move(found->second);
  objects_.erase(found);
  return object;
}

std::size_t HandleTable::size() const {
  std::lock_guard lock(mutex_);
  return objects_.size();
}

} // namespace kajps5::kernel
