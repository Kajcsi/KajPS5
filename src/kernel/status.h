// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace kajps5::kernel {

enum class KernelStatus {
  kOk,
  kInvalidArgument,
  kNotFound,
  kBusy,
  kWouldBlock,
  kPermissionDenied,
  kNoResources,
};

} // namespace kajps5::kernel
