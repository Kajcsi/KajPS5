// Adapted from KytyPS5's image/cache layout model and SharpEmu guest-image
// byte-count and image-type behavior. See THIRD_PARTY_NOTICES.md.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef KAJPS5_GPU_IMAGE_LAYOUT_H_
#define KAJPS5_GPU_IMAGE_LAYOUT_H_

#include "gpu/definitions.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace kajps5::gpu {

inline constexpr uint32_t kMaxGuestImageMipLevels = 16;
inline constexpr uint64_t kMaxGuestImageLayoutBytes = 512ULL * 1024ULL * 1024ULL;

enum class GuestImageLayoutStatus : uint8_t {
  kSuccess,
  kNeedsDetile,
  kInvalidArgument,
  kUnsupportedFormat,
  kUnsupportedLayout,
  kOverflow,
};

// Explicitly identifies guest storage, not a GuestMemory allocation or owner.
struct GuestImageStorageKey {
  uint64_t guest_address = 0;
  uint64_t byte_count = 0;
  uint32_t storage_alias_format = 0;

  bool operator==(const GuestImageStorageKey&) const = default;
};

struct GuestImageMipLayout {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 0;
  uint32_t block_width = 0;
  uint32_t block_height = 0;
  uint32_t block_depth = 0;
  uint64_t row_bytes = 0;       // Includes the explicit row pitch when supplied.
  uint64_t slice_bytes = 0;     // One depth slice, including the explicit slice pitch.
  uint64_t layer_bytes = 0;     // One array layer; a 3D mip uses its shrunk depth.
  uint64_t byte_offset = 0;
  uint64_t byte_count = 0;      // All layers/faces at this level.
};

// Explicit pitches describe a single-mip linear image. Multi-mip descriptors
// must use tightly_packed mode until per-level guest pitches are decoded.
struct GuestImageLayoutInput {
  uint64_t guest_address = 0;
  uint32_t format = 0; // Prospero::BufferFormat raw value.
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 0;
  uint32_t layers = 1; // Cubes, not faces, for ImageType::kCube.
  uint32_t mip_count = 1;
  uint64_t row_pitch_bytes = 0;
  uint64_t slice_pitch_bytes = 0;
  Prospero::ImageType image_type = Prospero::ImageType::kColor2D;
  Prospero::TileMode tile_mode = Prospero::TileMode::kLinear;
  bool tightly_packed = true;
};

struct GuestImageLayout {
  GuestImageLayoutStatus status = GuestImageLayoutStatus::kInvalidArgument;
  std::string_view diagnostic = "guest image layout was not calculated";
  uint32_t view_format = 0;
  uint32_t storage_alias_format = 0;
  uint32_t array_layers = 0; // Cube faces included; zero for volumes.
  bool block_compressed = false;
  bool needs_detile = false;
  std::array<GuestImageMipLayout, kMaxGuestImageMipLevels> mips {};
  uint64_t total_bytes = 0;
  uint64_t guest_storage_bytes = 0;
  GuestImageStorageKey storage_key {};

  [[nodiscard]] bool ok() const { return status == GuestImageLayoutStatus::kSuccess; }
};

// Returns a fully initialized layout on success. Every failure returns the
// default, non-published result with an explanatory diagnostic.
[[nodiscard]] GuestImageLayout CalculateGuestImageLayout(const GuestImageLayoutInput& input);

} // namespace kajps5::gpu

#endif // KAJPS5_GPU_IMAGE_LAYOUT_H_
