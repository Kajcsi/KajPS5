// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "kernel/object.h"

namespace kajps5::kernel {

using KernelHandle = std::uint64_t;

inline constexpr KernelHandle kInvalidKernelHandle = 0;

class HandleTable final {
public:
  [[nodiscard]] std::optional<KernelHandle>
  Insert(std::shared_ptr<KernelObject> object);
  [[nodiscard]] std::shared_ptr<KernelObject>
  Find(KernelHandle handle, KernelObjectType expected_type) const;
  [[nodiscard]] std::shared_ptr<KernelObject>
  Remove(KernelHandle handle, KernelObjectType expected_type);
  [[nodiscard]] std::size_t size() const;

private:
  mutable std::mutex mutex_;
  std::map<KernelHandle, std::shared_ptr<KernelObject>> objects_;
  KernelHandle next_handle_ = 1;
  bool exhausted_ = false;
};

} // namespace kajps5::kernel
