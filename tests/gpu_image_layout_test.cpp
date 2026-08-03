// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/image_layout.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using kajps5::gpu::CalculateGuestImageLayout;
using kajps5::gpu::GuestImageLayoutInput;
using kajps5::gpu::GuestImageLayoutStatus;
namespace P = kajps5::gpu::Prospero;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_image_layout_test: " << message << '\n';
    std::exit(1);
  }
}

GuestImageLayoutInput Input(P::BufferFormat format, uint32_t width, uint32_t height = 1) {
  GuestImageLayoutInput input;
  input.guest_address = 0x100000;
  input.format = P::GpuEnumValue(format);
  input.width = width;
  input.height = height;
  input.depth = 1;
  return input;
}

void CheckBytes(P::BufferFormat format, uint64_t expected) {
  const auto result = CalculateGuestImageLayout(Input(format, 1));
  Check(result.ok(), "ordinary texel format was rejected");
  Check(result.total_bytes == expected, "ordinary texel byte count differs");
}

void CheckFailure(const GuestImageLayoutInput& input, GuestImageLayoutStatus expected,
                  std::string_view message) {
  const auto result = CalculateGuestImageLayout(input);
  Check(result.status == expected, message);
  Check(result.total_bytes == 0 && result.view_format == 0 &&
            result.storage_key.byte_count == 0,
        "failed layout published partial state");
}

} // namespace

int main() {
  // SharpEmu's byte-count behavior, expanded to every byte size the cache needs.
  CheckBytes(P::BufferFormat::k8UNorm, 1);
  CheckBytes(P::BufferFormat::k8_8UNorm, 2);
  CheckBytes(P::BufferFormat::k8_8_8_8UNorm, 4);
  CheckBytes(P::BufferFormat::k16UNorm, 2);
  CheckBytes(P::BufferFormat::k16_16UNorm, 4);
  CheckBytes(P::BufferFormat::k16_16_16_16UNorm, 8);
  CheckBytes(P::BufferFormat::k32Float, 4);
  CheckBytes(P::BufferFormat::k32_32Float, 8);
  CheckBytes(P::BufferFormat::k32_32_32Float, 12);
  CheckBytes(P::BufferFormat::k32_32_32_32Float, 16);

  const P::BufferFormat compressed[] = {
      P::BufferFormat::kBc1UNorm, P::BufferFormat::kBc2UNorm,
      P::BufferFormat::kBc3UNorm,
      P::BufferFormat::kBc4UNorm, P::BufferFormat::kBc5UNorm,
      P::BufferFormat::kBc6UFloat, P::BufferFormat::kBc7UNorm,
  };
  for (const auto format : compressed) {
    const auto result = CalculateGuestImageLayout(Input(format, 1, 1));
    const uint64_t expected = format == P::BufferFormat::kBc1UNorm ||
                                      format == P::BufferFormat::kBc4UNorm
                                  ? 8
                                  : 16;
    Check(result.ok() && result.total_bytes == expected && result.mips[0].block_width == 1 &&
              result.mips[0].block_height == 1,
          "small BC image did not occupy one compressed block");
  }
  const auto odd_bc = CalculateGuestImageLayout(Input(P::BufferFormat::kBc3UNorm, 5, 7));
  Check(odd_bc.ok() && odd_bc.total_bytes == 64 && odd_bc.mips[0].block_width == 2 &&
            odd_bc.mips[0].block_height == 2,
        "odd BC dimensions have the wrong block footprint");

  auto mip_chain = Input(P::BufferFormat::k8_8_8_8UNorm, 8, 4);
  mip_chain.mip_count = 3;
  const auto mips = CalculateGuestImageLayout(mip_chain);
  Check(mips.ok() && mips.total_bytes == 168 && mips.mips[0].byte_offset == 0 &&
            mips.mips[1].byte_offset == 128 && mips.mips[2].byte_offset == 160 &&
            mips.mips[2].width == 2 && mips.mips[2].height == 1,
        "mip chain has the wrong dimensions or packed offsets");

  auto one_dimensional = Input(P::BufferFormat::k8UNorm, 9);
  one_dimensional.image_type = P::ImageType::kColor1D;
  const auto one_dimensional_result = CalculateGuestImageLayout(one_dimensional);
  Check(one_dimensional_result.ok() && one_dimensional_result.total_bytes == 9,
        "1D image did not retain its one-dimensional footprint");

  auto array = Input(P::BufferFormat::k8UNorm, 2, 2);
  array.image_type = P::ImageType::kColor2DArray;
  array.layers = 3;
  const auto array_result = CalculateGuestImageLayout(array);
  Check(array_result.ok() && array_result.array_layers == 3 && array_result.total_bytes == 12 &&
            array_result.mips[0].layer_bytes == 4,
        "array layers were confused with image depth");

  auto cube = Input(P::BufferFormat::k8UNorm, 2, 2);
  cube.image_type = P::ImageType::kCube;
  cube.layers = 2;
  const auto cube_result = CalculateGuestImageLayout(cube);
  Check(cube_result.ok() && cube_result.array_layers == 12 && cube_result.total_bytes == 48,
        "cube faces were not multiplied safely");

  auto volume = Input(P::BufferFormat::k8UNorm, 8, 4);
  volume.image_type = P::ImageType::kColor3D;
  volume.depth = 4;
  volume.mip_count = 3;
  const auto volume_result = CalculateGuestImageLayout(volume);
  Check(volume_result.ok() && volume_result.array_layers == 0 && volume_result.total_bytes == 146 &&
            volume_result.mips[1].depth == 2 && volume_result.mips[2].depth == 1,
        "3D depth did not shrink independently per mip");

  auto pitched = Input(P::BufferFormat::k8_8_8_8UNorm, 3, 2);
  pitched.tightly_packed = false;
  pitched.row_pitch_bytes = 16;
  pitched.slice_pitch_bytes = 48;
  const auto pitched_result = CalculateGuestImageLayout(pitched);
  Check(pitched_result.ok() && pitched_result.total_bytes == 48 &&
            pitched_result.mips[0].row_bytes == 16 && pitched_result.mips[0].slice_bytes == 48,
        "explicit row or slice pitch was not honored");

  auto unorm = Input(P::BufferFormat::k8_8_8_8UNorm, 7, 3);
  auto srgb = unorm;
  srgb.format = P::GpuEnumValue(P::BufferFormat::k8_8_8_8Srgb);
  const auto unorm_result = CalculateGuestImageLayout(unorm);
  const auto srgb_result = CalculateGuestImageLayout(srgb);
  Check(unorm_result.ok() && srgb_result.ok() && unorm_result.view_format != srgb_result.view_format &&
            unorm_result.storage_key == srgb_result.storage_key,
        "sRGB and UNORM views did not share a storage alias key");

  auto invalid = Input(P::BufferFormat::kInvalid, 1);
  CheckFailure(invalid, GuestImageLayoutStatus::kUnsupportedFormat, "invalid format was accepted");
  invalid = Input(P::BufferFormat::k8UNorm, 0);
  CheckFailure(invalid, GuestImageLayoutStatus::kInvalidArgument, "zero width was accepted");
  invalid = Input(P::BufferFormat::k8UNorm, 2, 2);
  invalid.image_type = P::ImageType::kColor2D;
  invalid.depth = 2;
  CheckFailure(invalid, GuestImageLayoutStatus::kInvalidArgument, "invalid 2D depth was accepted");
  invalid = Input(P::BufferFormat::kBc1UNorm, 5, 5);
  invalid.tightly_packed = false;
  invalid.row_pitch_bytes = 15;
  CheckFailure(invalid, GuestImageLayoutStatus::kInvalidArgument, "impossible BC row pitch was accepted");
  invalid = Input(P::BufferFormat::k8_8_8_8UNorm, 3, 2);
  invalid.tightly_packed = false;
  invalid.slice_pitch_bytes = 25;
  CheckFailure(invalid, GuestImageLayoutStatus::kInvalidArgument,
               "misaligned guest image slice pitch was accepted");
  invalid = Input(P::BufferFormat::k8UNorm, 2, 2);
  invalid.mip_count = 2;
  invalid.tightly_packed = false;
  CheckFailure(invalid, GuestImageLayoutStatus::kUnsupportedLayout,
               "explicit pitches with a mip chain were accepted");
  invalid = Input(P::BufferFormat::k8UNorm, 2, 2);
  invalid.tile_mode = P::TileMode::kStandard4KB;
  CheckFailure(invalid, GuestImageLayoutStatus::kNeedsDetile, "tiled image was treated as linear");
  invalid = Input(P::BufferFormat::k8UNorm, 1);
  invalid.guest_address = std::numeric_limits<uint64_t>::max();
  CheckFailure(invalid, GuestImageLayoutStatus::kOverflow, "address end overflow was accepted");
  invalid = Input(P::BufferFormat::k8_8_8_8UNorm, 65536, 65536);
  CheckFailure(invalid, GuestImageLayoutStatus::kUnsupportedLayout,
               "excessive fixed layout bound was accepted");
  invalid = Input(P::BufferFormat::k8UNorm, 1);
  invalid.image_type = P::ImageType::kCube;
  invalid.layers = std::numeric_limits<uint32_t>::max();
  CheckFailure(invalid, GuestImageLayoutStatus::kUnsupportedLayout,
               "excessive cube face count was accepted");

  std::cout << "gpu_image_layout_test: all cases passed\n";
  return 0;
}
