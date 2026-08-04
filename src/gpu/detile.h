// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#ifndef KAJPS5_GPU_DETILE_H_
#define KAJPS5_GPU_DETILE_H_

#include "gpu/image_layout.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace kajps5::gpu {

enum class DetileStatus : std::uint8_t {
  kSuccess,
  kInvalidArgument,
  kUnsupportedLayout,
  kSizeMismatch,
  kOverflow,
  kTileAddressFailure,
};

struct DetileResult {
  DetileStatus status = DetileStatus::kInvalidArgument;
  std::string_view diagnostic = "detile was not attempted";

  [[nodiscard]] bool ok() const { return status == DetileStatus::kSuccess; }
};

// Converts one proven RenderTarget64KB guest image into its mip-zero linear
// staging layout. On failure, linear is left unchanged; successful calls write
// active texels only and retain linear row padding.
[[nodiscard]] DetileResult DetileRenderTarget64KB(const GuestImageLayout& layout,
                                                  std::span<const std::byte> tiled,
                                                  std::span<std::byte> linear);

} // namespace kajps5::gpu

#endif // KAJPS5_GPU_DETILE_H_
