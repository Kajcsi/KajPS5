// Adapted from KytyPS5's image/cache layout model and SharpEmu guest-image
// byte-count and image-type behavior. See THIRD_PARTY_NOTICES.md.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/image_layout.h"

#include "gpu/format.h"
#include "gpu/tile_layout.h"

#include <algorithm>
#include <limits>

namespace kajps5::gpu {
namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

bool Add(uint64_t left, uint64_t right, uint64_t* result) {
  if (left > kMax - right) return false;
  *result = left + right;
  return true;
}

bool Multiply(uint64_t left, uint64_t right, uint64_t* result) {
  if (left != 0 && right > kMax / left) return false;
  *result = left * right;
  return true;
}

uint32_t MipDimension(uint32_t dimension, uint32_t level) {
  return std::max(dimension >> level, 1u);
}

GuestImageLayout Failure(GuestImageLayoutStatus status, std::string_view diagnostic,
                         bool needs_detile = false) {
  GuestImageLayout result;
  result.status = status;
  result.diagnostic = diagnostic;
  result.needs_detile = needs_detile;
  return result;
}

bool IsKnownImageType(Prospero::ImageType type) {
  using Prospero::ImageType;
  switch (type) {
    case ImageType::kColor1D:
    case ImageType::kColor2D:
    case ImageType::kColor3D:
    case ImageType::kCube:
    case ImageType::kColor1DArray:
    case ImageType::kColor2DArray: return true;
    default: return false;
  }
}

} // namespace

GuestImageLayout CalculateGuestImageLayout(const GuestImageLayoutInput& input) {
  using Prospero::ImageType;
  if (input.width == 0 || input.height == 0 || input.depth == 0 || input.layers == 0 ||
      input.mip_count == 0) {
    return Failure(GuestImageLayoutStatus::kInvalidArgument,
                   "guest image dimensions, layers, and mips must be nonzero");
  }
  if (input.mip_count > kMaxGuestImageMipLevels) {
    return Failure(GuestImageLayoutStatus::kUnsupportedLayout,
                   "guest image mip count exceeds the fixed layout bound");
  }
  if (!IsKnownImageType(input.image_type)) {
    return Failure(GuestImageLayoutStatus::kUnsupportedLayout, "unsupported guest image type");
  }
  const uint32_t bytes_per_element = Prospero::NumBytesPerElement(input.format);
  const uint32_t bytes_per_block = Prospero::BlockCompressedBytesPerBlock(input.format);
  const uint32_t storage_alias_format = Prospero::StorageAliasFormat(input.format);
  if (storage_alias_format == 0 || (bytes_per_element == 0 && bytes_per_block == 0) ||
      !Prospero::IsSupportedTextureFormat(input.format)) {
    return Failure(GuestImageLayoutStatus::kUnsupportedFormat, "unsupported guest image format");
  }
  if (!input.tightly_packed && input.mip_count != 1) {
    return Failure(GuestImageLayoutStatus::kUnsupportedLayout,
                   "explicit guest pitches require a single mip level");
  }
  if (input.tightly_packed && (input.row_pitch_bytes != 0 || input.slice_pitch_bytes != 0)) {
    return Failure(GuestImageLayoutStatus::kInvalidArgument,
                   "tightly packed guest image cannot specify pitches");
  }

  uint64_t array_layers = input.layers;
  switch (input.image_type) {
    case ImageType::kColor1D:
      if (input.height != 1 || input.depth != 1 || input.layers != 1) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument,
                       "1D guest image requires height, depth, and layers of one");
      }
      break;
    case ImageType::kColor1DArray:
      if (input.height != 1 || input.depth != 1) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument,
                       "1D array guest image requires height and depth of one");
      }
      break;
    case ImageType::kColor2D:
      if (input.depth != 1 || input.layers != 1) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument,
                       "2D guest image requires depth and layers of one");
      }
      break;
    case ImageType::kColor2DArray:
      if (input.depth != 1) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument,
                       "2D array guest image requires depth of one");
      }
      break;
    case ImageType::kColor3D:
      if (input.layers != 1) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument,
                       "3D guest image requires one array layer");
      }
      array_layers = 0;
      break;
    case ImageType::kCube:
      if (input.width != input.height || input.depth != 1) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument,
                       "cube guest image requires square faces and depth one");
      }
      if (!Multiply(input.layers, 6, &array_layers)) {
        return Failure(GuestImageLayoutStatus::kOverflow, "cube face count overflows");
      }
      if (array_layers > std::numeric_limits<uint32_t>::max()) {
        return Failure(GuestImageLayoutStatus::kUnsupportedLayout,
                       "cube face count exceeds the fixed layout bound");
      }
      break;
    default: break;
  }

  if (input.tile_mode != Prospero::TileMode::kLinear) {
    if (input.tile_mode == Prospero::TileMode::kRenderTarget &&
        input.image_type == ImageType::kColor2D && input.depth == 1 &&
        input.layers == 1 && input.mip_count == 1 && input.tightly_packed &&
        input.row_pitch_bytes == 0 && input.slice_pitch_bytes == 0 &&
        bytes_per_block == 0 &&
        (bytes_per_element == 1 || bytes_per_element == 2 ||
         bytes_per_element == 4 || bytes_per_element == 8 ||
         bytes_per_element == 16)) {
      uint32_t pitch = 0;
      const auto tile = Prospero::GpuEnumValue(Prospero::TileMode::kRenderTarget);
      TileSizeAlign tiled_total {};
      if (!TileGetTexturePitch(input.format, input.width, 1, tile, pitch) ||
          !TileGetTextureTotalSize(input.format, input.width, input.height, 1, pitch,
                                   1, tile, false, tiled_total) ||
          tiled_total.size == 0 || tiled_total.align != 65536) {
        return Failure(GuestImageLayoutStatus::kUnsupportedLayout,
                       "render-target tiled guest layout is unsupported");
      }
      uint64_t row_bytes = 0;
      uint64_t staging_bytes = 0;
      if (!Multiply(pitch, bytes_per_element, &row_bytes) ||
          !Multiply(row_bytes, input.height, &staging_bytes) ||
          input.guest_address > kMax - staging_bytes ||
          input.guest_address > kMax - tiled_total.size) {
        return Failure(GuestImageLayoutStatus::kOverflow,
                       "render-target guest layout size or address overflows");
      }
      if (staging_bytes > kMaxGuestImageLayoutBytes ||
          tiled_total.size > kMaxGuestImageLayoutBytes) {
        return Failure(GuestImageLayoutStatus::kUnsupportedLayout,
                       "render-target guest layout exceeds the fixed bound");
      }
      GuestImageLayout result;
      result.status = GuestImageLayoutStatus::kSuccess;
      result.diagnostic = "render-target tiled guest layout calculated";
      result.view_format = input.format;
      result.storage_alias_format = storage_alias_format;
      result.array_layers = 1;
      result.needs_detile = true;
      result.mips[0] = {input.width, input.height, 1, input.width, input.height, 1,
                        row_bytes, staging_bytes, staging_bytes, 0, staging_bytes};
      result.total_bytes = staging_bytes;
      result.guest_storage_bytes = tiled_total.size;
      result.storage_key = {input.guest_address, result.guest_storage_bytes,
                            storage_alias_format};
      return result;
    }
    return Failure(GuestImageLayoutStatus::kNeedsDetile,
                   "tiled guest image needs a proven detile layout", true);
  }

  GuestImageLayout result;
  result.status = GuestImageLayoutStatus::kSuccess;
  result.diagnostic = "linear guest image layout calculated";
  result.view_format = input.format;
  result.storage_alias_format = storage_alias_format;
  result.array_layers = static_cast<uint32_t>(array_layers);
  result.block_compressed = bytes_per_block != 0;

  uint64_t total_bytes = 0;
  for (uint32_t level = 0; level < input.mip_count; ++level) {
    auto& mip = result.mips[level];
    mip.width = MipDimension(input.width, level);
    mip.height = MipDimension(input.height, level);
    mip.depth = input.image_type == ImageType::kColor3D ? MipDimension(input.depth, level) : 1;
    mip.block_width = bytes_per_block != 0 ? (static_cast<uint64_t>(mip.width) + 3) / 4 : mip.width;
    mip.block_height = bytes_per_block != 0 ? (static_cast<uint64_t>(mip.height) + 3) / 4 : mip.height;
    mip.block_depth = mip.depth;
    const uint64_t unit_bytes = bytes_per_block != 0 ? bytes_per_block : bytes_per_element;
    if (!Multiply(mip.block_width, unit_bytes, &mip.row_bytes)) {
      return Failure(GuestImageLayoutStatus::kOverflow, "guest image row bytes overflow");
    }
    const uint64_t minimum_row_bytes = mip.row_bytes;
    if (!input.tightly_packed && input.row_pitch_bytes != 0) {
      if (input.row_pitch_bytes < minimum_row_bytes || input.row_pitch_bytes % unit_bytes != 0) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument, "guest image row pitch is impossible");
      }
      mip.row_bytes = input.row_pitch_bytes;
    }
    if (!Multiply(mip.row_bytes, mip.block_height, &mip.slice_bytes)) {
      return Failure(GuestImageLayoutStatus::kOverflow, "guest image slice bytes overflow");
    }
    const uint64_t minimum_slice_bytes = mip.slice_bytes;
    if (!input.tightly_packed && input.slice_pitch_bytes != 0) {
      if (input.slice_pitch_bytes < minimum_slice_bytes ||
          input.slice_pitch_bytes % mip.row_bytes != 0) {
        return Failure(GuestImageLayoutStatus::kInvalidArgument, "guest image slice pitch is impossible");
      }
      mip.slice_bytes = input.slice_pitch_bytes;
    }
    if (!Multiply(mip.slice_bytes, mip.depth, &mip.layer_bytes)) {
      return Failure(GuestImageLayoutStatus::kOverflow, "guest image layer bytes overflow");
    }
    const uint64_t level_layers = input.image_type == ImageType::kColor3D ? 1 : array_layers;
    if (!Multiply(mip.layer_bytes, level_layers, &mip.byte_count)) {
      return Failure(GuestImageLayoutStatus::kOverflow, "guest image mip bytes overflow");
    }
    mip.byte_offset = total_bytes;
    if (!Add(total_bytes, mip.byte_count, &total_bytes)) {
      return Failure(GuestImageLayoutStatus::kOverflow, "guest image total bytes overflow");
    }
    if (total_bytes > kMaxGuestImageLayoutBytes) {
      return Failure(GuestImageLayoutStatus::kUnsupportedLayout,
                     "guest image bytes exceed the fixed layout bound");
    }
  }
  if (input.guest_address > kMax - total_bytes) {
    return Failure(GuestImageLayoutStatus::kOverflow, "guest image address end overflows");
  }
  result.total_bytes = total_bytes;
  result.guest_storage_bytes = total_bytes;
  result.storage_key = {input.guest_address, result.guest_storage_bytes, storage_alias_format};
  return result;
}

} // namespace kajps5::gpu
