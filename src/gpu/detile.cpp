// Adapted from KytyPS5 tests/ShaderRecompilerComputeTests.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203. See THIRD_PARTY_NOTICES.md.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/detile.h"

#include "gpu/format.h"
#include "gpu/tile_layout.h"

#include <functional>
#include <limits>

namespace kajps5::gpu {
namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

[[nodiscard]] bool Add(std::uint64_t left, std::uint64_t right, std::uint64_t* result) {
  if (left > kMax - right) return false;
  *result = left + right;
  return true;
}

[[nodiscard]] bool Multiply(std::uint64_t left, std::uint64_t right,
                            std::uint64_t* result) {
  if (left != 0 && right > kMax / left) return false;
  *result = left * right;
  return true;
}

[[nodiscard]] bool IsSupportedBytesPerElement(std::uint32_t bytes_per_element) {
  return bytes_per_element == 1 || bytes_per_element == 2 || bytes_per_element == 4 ||
         bytes_per_element == 8 || bytes_per_element == 16;
}

[[nodiscard]] DetileResult Failure(DetileStatus status, std::string_view diagnostic) {
  return {status, diagnostic};
}

struct DetilePlan {
  TileBlockLayout block {};
  std::uint64_t bytes_per_element = 0;
  std::uint64_t columns = 0;
  std::uint64_t rows = 0;
  std::uint64_t row_bytes = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

[[nodiscard]] bool SpansOverlap(std::span<const std::byte> tiled, std::span<std::byte> linear) {
  const auto* tiled_begin = tiled.data();
  const auto* tiled_end = tiled_begin + tiled.size();
  const auto* linear_begin = linear.data();
  const auto* linear_end = linear_begin + linear.size();
  std::less<const std::byte*> less {};
  return less(tiled_begin, linear_end) && less(linear_begin, tiled_end);
}

[[nodiscard]] DetileResult MakePlan(const GuestImageLayout& layout,
                                    std::span<const std::byte> tiled,
                                    std::span<std::byte> linear, DetilePlan* plan) {
  if (!layout.ok() || !layout.needs_detile) {
    return Failure(DetileStatus::kUnsupportedLayout, "layout is not a detile-ready image");
  }
  if (layout.array_layers != 1 || layout.block_compressed) {
    return Failure(DetileStatus::kUnsupportedLayout, "layout is not one uncompressed layer");
  }

  const auto bytes_per_element = Prospero::NumBytesPerElement(layout.view_format);
  if (Prospero::BlockCompressedBytesPerBlock(layout.view_format) != 0 ||
      !IsSupportedBytesPerElement(bytes_per_element)) {
    return Failure(DetileStatus::kUnsupportedLayout, "layout has an unsupported element format");
  }

  const auto& mip = layout.mips[0];
  if (mip.width == 0 || mip.height == 0 || mip.depth != 1 || mip.block_width != mip.width ||
      mip.block_height != mip.height || mip.block_depth != 1 || mip.byte_offset != 0) {
    return Failure(DetileStatus::kUnsupportedLayout, "layout mip zero is not a 2D base level");
  }
  if (mip.row_bytes == 0 || mip.row_bytes % bytes_per_element != 0) {
    return Failure(DetileStatus::kInvalidArgument, "layout row bytes are not element aligned");
  }

  std::uint64_t minimum_row_bytes = 0;
  std::uint64_t expected_slice_bytes = 0;
  if (!Multiply(mip.width, bytes_per_element, &minimum_row_bytes) ||
      mip.row_bytes < minimum_row_bytes ||
      !Multiply(mip.row_bytes, mip.height, &expected_slice_bytes)) {
    return Failure(DetileStatus::kOverflow, "layout linear byte counts overflow");
  }
  if (mip.slice_bytes != expected_slice_bytes || mip.layer_bytes != mip.slice_bytes ||
      mip.byte_count != mip.layer_bytes || layout.total_bytes != mip.byte_count) {
    return Failure(DetileStatus::kInvalidArgument, "layout linear byte counts are inconsistent");
  }
  if (layout.guest_storage_bytes == 0 ||
      layout.storage_key.byte_count != layout.guest_storage_bytes) {
    return Failure(DetileStatus::kInvalidArgument, "layout guest storage bytes are inconsistent");
  }
  if (tiled.size() != layout.guest_storage_bytes || linear.size() != layout.total_bytes) {
    return Failure(DetileStatus::kSizeMismatch, "source or destination size does not match layout");
  }
  if (SpansOverlap(tiled, linear)) {
    return Failure(DetileStatus::kInvalidArgument, "source and destination spans overlap");
  }

  TileBlockLayout block {};
  if (!TileGetBlockLayout(TileBlockFamily::RenderTarget64KB, bytes_per_element, block) ||
      block.block_size == 0 || block.block_width == 0 || block.block_height == 0 ||
      block.block_depth != 1) {
    return Failure(DetileStatus::kTileAddressFailure, "render-target tile layout is unavailable");
  }

  const std::uint64_t pitch = mip.row_bytes / bytes_per_element;
  std::uint64_t columns = pitch / block.block_width;
  if (pitch % block.block_width != 0 && !Add(columns, 1, &columns)) {
    return Failure(DetileStatus::kOverflow, "tile column count overflows");
  }
  std::uint64_t block_row_bytes = 0;
  std::uint64_t rows = 0;
  std::uint64_t covered_height = 0;
  if (columns == 0 || !Multiply(columns, block.block_size, &block_row_bytes) ||
      block_row_bytes == 0 || layout.guest_storage_bytes % block_row_bytes != 0) {
    return Failure(DetileStatus::kInvalidArgument, "guest storage does not contain complete tile rows");
  }
  rows = layout.guest_storage_bytes / block_row_bytes;
  if (rows == 0 || !Multiply(rows, block.block_height, &covered_height) ||
      mip.height > covered_height) {
    return Failure(DetileStatus::kInvalidArgument, "guest storage does not cover active image rows");
  }

  plan->block = block;
  plan->bytes_per_element = bytes_per_element;
  plan->columns = columns;
  plan->rows = rows;
  plan->row_bytes = mip.row_bytes;
  plan->width = mip.width;
  plan->height = mip.height;
  return {DetileStatus::kSuccess, "render-target detile plan calculated"};
}

[[nodiscard]] bool CalculateOffsets(const DetilePlan& plan, std::uint32_t x, std::uint32_t y,
                                    std::uint64_t* tiled_offset,
                                    std::uint64_t* linear_offset) {
  const std::uint32_t block_x = x / plan.block.block_width;
  const std::uint32_t block_y = y / plan.block.block_height;
  const std::uint32_t local_x = x % plan.block.block_width;
  const std::uint32_t local_y = y % plan.block.block_height;
  std::uint32_t local_offset = 0;
  std::uint32_t block_xor = 0;
  if (!TileGetBlockOffset(plan.block, local_x, local_y, 0, local_offset) ||
      !TileGetBlockXor(plan.block, block_x, block_y, 0, block_xor)) {
    return false;
  }

  const std::uint64_t intra_block_offset = local_offset ^ block_xor;
  std::uint64_t block_index = 0;
  std::uint64_t block_offset = 0;
  std::uint64_t source_offset = 0;
  std::uint64_t row_offset = 0;
  std::uint64_t destination_offset = 0;
  if (intra_block_offset >= plan.block.block_size ||
      !Multiply(block_y, plan.columns, &block_index) || !Add(block_index, block_x, &block_index) ||
      !Multiply(block_index, plan.block.block_size, &block_offset) ||
      !Add(block_offset, intra_block_offset, &source_offset) ||
      !Multiply(y, plan.row_bytes, &row_offset) ||
      !Multiply(x, plan.bytes_per_element, &destination_offset) ||
      !Add(row_offset, destination_offset, &destination_offset)) {
    return false;
  }
  *tiled_offset = source_offset;
  *linear_offset = destination_offset;
  return true;
}

[[nodiscard]] DetileResult ValidateTexelOffsets(const DetilePlan& plan,
                                                std::uint64_t tiled_size,
                                                std::uint64_t linear_size) {
  for (std::uint32_t y = 0; y < plan.height; ++y) {
    for (std::uint32_t x = 0; x < plan.width; ++x) {
      std::uint64_t tiled_offset = 0;
      std::uint64_t linear_offset = 0;
      std::uint64_t tiled_end = 0;
      std::uint64_t linear_end = 0;
      if (!CalculateOffsets(plan, x, y, &tiled_offset, &linear_offset) ||
          !Add(tiled_offset, plan.bytes_per_element, &tiled_end) ||
          !Add(linear_offset, plan.bytes_per_element, &linear_end) || tiled_end > tiled_size ||
          linear_end > linear_size) {
        return Failure(DetileStatus::kTileAddressFailure, "active texel tile address is invalid");
      }
    }
  }
  return {DetileStatus::kSuccess, "render-target tile addresses validated"};
}

} // namespace

DetileResult DetileRenderTarget64KB(const GuestImageLayout& layout,
                                    std::span<const std::byte> tiled,
                                    std::span<std::byte> linear) {
  DetilePlan plan {};
  const auto plan_result = MakePlan(layout, tiled, linear, &plan);
  if (!plan_result.ok()) return plan_result;

  const auto validation = ValidateTexelOffsets(plan, tiled.size(), linear.size());
  if (!validation.ok()) return validation;

  for (std::uint32_t y = 0; y < plan.height; ++y) {
    for (std::uint32_t x = 0; x < plan.width; ++x) {
      std::uint64_t tiled_offset = 0;
      std::uint64_t linear_offset = 0;
      if (!CalculateOffsets(plan, x, y, &tiled_offset, &linear_offset)) {
        return Failure(DetileStatus::kTileAddressFailure, "validated tile address changed");
      }
      for (std::uint64_t byte = 0; byte < plan.bytes_per_element; ++byte) {
        linear[linear_offset + byte] = tiled[tiled_offset + byte];
      }
    }
  }
  return {DetileStatus::kSuccess, "render-target image detiled"};
}

} // namespace kajps5::gpu
