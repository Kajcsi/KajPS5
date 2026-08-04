// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/detile.h"
#include "gpu/format.h"
#include "gpu/image_layout.h"
#include "gpu/tile_layout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace {

using kajps5::gpu::CalculateGuestImageLayout;
using kajps5::gpu::DetileRenderTarget64KB;
using kajps5::gpu::GuestImageLayout;
using kajps5::gpu::GuestImageLayoutInput;
using kajps5::gpu::TileBlockFamily;
using kajps5::gpu::TileBlockLayout;
using kajps5::gpu::TileGetBlockLayout;
using kajps5::gpu::TileGetBlockOffset;
using kajps5::gpu::TileGetBlockXor;
namespace P = kajps5::gpu::Prospero;

constexpr std::size_t kGuardBytes = 23;
constexpr std::byte kSourceGuard {0x9d};
constexpr std::byte kDestinationGuard {0x4e};
constexpr std::byte kTilePadding {0x61};
constexpr std::byte kLinearPadding {0xb2};

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::fputs("gpu_detile_test: ", stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::exit(1);
  }
}

GuestImageLayout MakeLayout(P::BufferFormat format) {
  GuestImageLayoutInput input {};
  input.guest_address = 0x1'0000;
  input.format = P::GpuEnumValue(format);
  input.width = 17;
  input.height = 9;
  input.depth = 1;
  input.tile_mode = P::TileMode::kRenderTarget;
  const auto layout = CalculateGuestImageLayout(input);
  Check(layout.ok() && layout.needs_detile, "render-target layout was not calculated");
  return layout;
}

std::byte ActiveByte(std::uint32_t bytes_per_element, std::uint32_t x, std::uint32_t y,
                     std::uint32_t byte) {
  const auto value = bytes_per_element * 29u + x * 17u + y * 43u + byte * 71u;
  return static_cast<std::byte>(value & 0xffu);
}

bool FillReferenceTiled(const GuestImageLayout& layout,
                        std::span<const std::byte> expected_linear,
                        std::span<std::byte> tiled) {
  const auto bytes_per_element = P::NumBytesPerElement(layout.view_format);
  const auto& mip = layout.mips[0];
  TileBlockLayout block {};
  if (!TileGetBlockLayout(TileBlockFamily::RenderTarget64KB, bytes_per_element, block) ||
      expected_linear.size() != layout.total_bytes || tiled.size() != layout.guest_storage_bytes) {
    return false;
  }

  std::fill(tiled.begin(), tiled.end(), kTilePadding);
  const std::uint64_t pitch = mip.row_bytes / bytes_per_element;
  const std::uint64_t columns = pitch / block.block_width +
                                (pitch % block.block_width == 0 ? 0 : 1);
  bool non_linear = false;
  for (std::uint32_t y = 0; y < mip.height; ++y) {
    for (std::uint32_t x = 0; x < mip.width; ++x) {
      const std::uint32_t block_x = x / block.block_width;
      const std::uint32_t block_y = y / block.block_height;
      const std::uint32_t local_x = x % block.block_width;
      const std::uint32_t local_y = y % block.block_height;
      std::uint32_t local_offset = 0;
      std::uint32_t block_xor = 0;
      if (!TileGetBlockOffset(block, local_x, local_y, 0, local_offset) ||
          !TileGetBlockXor(block, block_x, block_y, 0, block_xor)) {
        return false;
      }
      const std::uint64_t tiled_offset =
          (static_cast<std::uint64_t>(block_y) * columns + block_x) * block.block_size +
          (local_offset ^ block_xor);
      const std::uint64_t linear_offset =
          static_cast<std::uint64_t>(y) * mip.row_bytes + x * bytes_per_element;
      if (tiled_offset + bytes_per_element > tiled.size() ||
          linear_offset + bytes_per_element > expected_linear.size()) {
        return false;
      }
      non_linear = non_linear || tiled_offset != linear_offset;
      for (std::uint32_t byte = 0; byte < bytes_per_element; ++byte) {
        tiled[tiled_offset + byte] = expected_linear[linear_offset + byte];
      }
    }
  }
  return non_linear;
}

void CheckFailure(const GuestImageLayout& layout, std::span<const std::byte> tiled,
                  std::vector<std::byte>* destination, std::span<std::byte> linear,
                  std::string_view message) {
  const auto before = *destination;
  Check(!DetileRenderTarget64KB(layout, tiled, linear).ok(), message);
  Check(*destination == before, "failed detile changed the destination");
}

void RunCase(P::BufferFormat format, std::uint32_t bytes_per_element) {
  const auto layout = MakeLayout(format);
  Check(P::NumBytesPerElement(layout.view_format) == bytes_per_element,
        "layout has an unexpected element size");

  std::vector<std::byte> expected(layout.total_bytes, kLinearPadding);
  const auto& mip = layout.mips[0];
  for (std::uint32_t y = 0; y < mip.height; ++y) {
    for (std::uint32_t x = 0; x < mip.width; ++x) {
      const std::uint64_t offset = static_cast<std::uint64_t>(y) * mip.row_bytes +
                                   x * bytes_per_element;
      for (std::uint32_t byte = 0; byte < bytes_per_element; ++byte) {
        expected[offset + byte] = ActiveByte(bytes_per_element, x, y, byte);
      }
    }
  }

  std::vector<std::byte> source(kGuardBytes + layout.guest_storage_bytes + kGuardBytes,
                                kSourceGuard);
  auto tiled = std::span(source).subspan(kGuardBytes, layout.guest_storage_bytes);
  Check(FillReferenceTiled(layout, expected, tiled),
        "reference tiled layout did not differ from linear bytes");
  const auto source_before = source;

  std::vector<std::byte> destination(kGuardBytes + layout.total_bytes + kGuardBytes,
                                     kDestinationGuard);
  auto linear = std::span(destination).subspan(kGuardBytes, layout.total_bytes);
  std::fill(linear.begin(), linear.end(), kLinearPadding);

  Check(DetileRenderTarget64KB(layout, tiled, linear).ok(), "detile rejected a valid layout");
  Check(source == source_before, "detile changed source bytes or source guards");
  Check(std::all_of(source.begin(), source.begin() + kGuardBytes,
                    [](std::byte value) { return value == kSourceGuard; }) &&
            std::all_of(source.end() - kGuardBytes, source.end(),
                        [](std::byte value) { return value == kSourceGuard; }),
        "source guard bytes changed");
  Check(std::all_of(destination.begin(), destination.begin() + kGuardBytes,
                    [](std::byte value) { return value == kDestinationGuard; }) &&
            std::all_of(destination.end() - kGuardBytes, destination.end(),
                        [](std::byte value) { return value == kDestinationGuard; }),
        "destination guard bytes changed");

  for (std::uint32_t y = 0; y < mip.height; ++y) {
    const auto row_offset = static_cast<std::uint64_t>(y) * mip.row_bytes;
    for (std::uint64_t byte = 0; byte < mip.row_bytes; ++byte) {
      const bool active = byte < static_cast<std::uint64_t>(mip.width) * bytes_per_element;
      Check(linear[row_offset + byte] == (active ? expected[row_offset + byte] : kLinearPadding),
            "detile bytes or row padding differ");
    }
  }

  CheckFailure(layout, tiled.first(tiled.size() - 1), &destination, linear,
               "short tiled source was accepted");
  CheckFailure(layout, tiled, &destination, linear.first(linear.size() - 1),
               "short linear destination was accepted");

  std::vector<std::byte> overlap(kGuardBytes + layout.guest_storage_bytes + layout.total_bytes +
                                     kGuardBytes,
                                 kTilePadding);
  auto overlap_tiled = std::span<const std::byte>(overlap).subspan(kGuardBytes,
                                                                     layout.guest_storage_bytes);
  auto overlap_linear = std::span(overlap).subspan(kGuardBytes + 1, layout.total_bytes);
  const auto overlap_before = overlap;
  Check(!DetileRenderTarget64KB(layout, overlap_tiled, overlap_linear).ok(),
        "overlapping spans were accepted");
  Check(overlap == overlap_before, "overlapping spans changed the destination");

  auto rejected = layout;
  rejected.needs_detile = false;
  CheckFailure(rejected, tiled, &destination, linear, "non-detile layout was accepted");
  rejected = layout;
  --rejected.storage_key.byte_count;
  CheckFailure(rejected, tiled, &destination, linear, "inconsistent guest storage key was accepted");
  rejected = layout;
  rejected.mips[0].row_bytes += bytes_per_element;
  CheckFailure(rejected, tiled, &destination, linear, "inconsistent row bytes were accepted");
  rejected = layout;
  --rejected.mips[0].slice_bytes;
  CheckFailure(rejected, tiled, &destination, linear, "inconsistent slice bytes were accepted");
  rejected = layout;
  --rejected.mips[0].byte_count;
  CheckFailure(rejected, tiled, &destination, linear, "inconsistent byte count was accepted");
  rejected = layout;
  rejected.view_format = P::GpuEnumValue(P::BufferFormat::kInvalid);
  CheckFailure(rejected, tiled, &destination, linear, "unsupported format was accepted");
}

} // namespace

int main() {
  RunCase(P::BufferFormat::k8UNorm, 1);
  RunCase(P::BufferFormat::k8_8UNorm, 2);
  RunCase(P::BufferFormat::k8_8_8_8UNorm, 4);
  RunCase(P::BufferFormat::k16_16_16_16UNorm, 8);
  RunCase(P::BufferFormat::k32_32_32_32Float, 16);
  std::puts("gpu_detile_test: all cases passed");
  return 0;
}
