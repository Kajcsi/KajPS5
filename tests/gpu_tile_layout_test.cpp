// Copyright (C) 2026 KajPS5 contributors
// Direct KytyPS5 tile-layout adaptation coverage at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// SPDX-License-Identifier: GPL-2.0-only

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "gpu/definitions.h"
#include "gpu/tile_layout.h"

namespace {
void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_tile_layout_test: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}
}  // namespace

int main() {
  using kajps5::gpu::TileBlockFamily;
  using kajps5::gpu::TileBlockLayout;
  const auto format = kajps5::gpu::Prospero::GpuEnumValue(
      kajps5::gpu::Prospero::BufferFormat::k32Float);
  Check(kajps5::gpu::TileIsStandard256BTextureSupported(format) &&
            kajps5::gpu::TileIsStandard4KBTextureSupported(format) &&
            kajps5::gpu::TileIsStandard64KBTextureSupported(format) &&
            !kajps5::gpu::TileIsStandard256BTextureSupported(UINT32_MAX) &&
            !kajps5::gpu::TileIsStandard4KBTextureSupported(UINT32_MAX) &&
            !kajps5::gpu::TileIsStandard64KBTextureSupported(UINT32_MAX),
        "standard texture support queries disagreed");
  constexpr std::array families = {
      TileBlockFamily::Standard256B, TileBlockFamily::Standard4KB,
      TileBlockFamily::Standard4KB3D, TileBlockFamily::Standard64KB,
      TileBlockFamily::Standard64KB3D, TileBlockFamily::Prt64KB,
      TileBlockFamily::Prt64KB3D, TileBlockFamily::RenderTarget64KB,
      TileBlockFamily::Depth64KB};
  constexpr std::array bytes_per_element = {1U, 2U, 4U, 8U, 16U};
  for (const auto family : families) {
    for (const auto bytes : bytes_per_element) {
      TileBlockLayout layout{TileBlockFamily::Count, 99, 99, 99, 99, 99};
      const auto expected = !(family == TileBlockFamily::Depth64KB && bytes == 16);
      Check(kajps5::gpu::TileGetBlockLayout(family, bytes, layout) == expected,
            "admitted block family/BPE mismatch");
      if (!expected) continue;
      Check(layout.block_size == layout.block_width * layout.block_height *
                                     layout.block_depth * layout.bytes_per_element,
            "block layout size is incoherent");
      std::uint32_t first = 99;
      std::uint32_t last = 99;
      Check(kajps5::gpu::TileGetBlockOffset(layout, 0, 0, 0, first) && first == 0 &&
                kajps5::gpu::TileGetBlockOffset(
                    layout, layout.block_width - 1, layout.block_height - 1,
                    layout.block_depth - 1, last) &&
                last < layout.block_size && last % bytes == 0,
            "block offsets are invalid");
      std::uint32_t xor_a = 99;
      std::uint32_t xor_b = 98;
      Check(kajps5::gpu::TileGetBlockXor(layout, 1, 1, xor_a) &&
                kajps5::gpu::TileGetBlockXor(layout, 1, 1, 1, xor_a) &&
                kajps5::gpu::TileGetBlockXor(layout, 1, 1, 1, xor_b) && xor_a == xor_b,
            "block XOR is not deterministic");
    }
  }
  TileBlockLayout unchanged{TileBlockFamily::Count, 9, 9, 9, 9, 9};
  Check(!kajps5::gpu::TileGetBlockLayout(TileBlockFamily::Count, 4, unchanged) &&
            unchanged.family == TileBlockFamily::Count && unchanged.bytes_per_element == 9,
        "invalid block layout partially mutated output");
  std::uint32_t unchanged_offset = 77;
  Check(!kajps5::gpu::TileGetBlockOffset(unchanged, 0, 0, 0, unchanged_offset) &&
            unchanged_offset == 77 &&
            !kajps5::gpu::TileGetBlockXor(unchanged, 1, 1, unchanged_offset) &&
            unchanged_offset == 77 &&
            !kajps5::gpu::TileGetBlockXor(unchanged, 1, 1, 1, unchanged_offset) &&
            unchanged_offset == 77,
        "block offset/XOR failure changed output");
  TileBlockLayout render_target;
  TileBlockLayout depth;
  Check(kajps5::gpu::TileGetBlockLayout(TileBlockFamily::RenderTarget64KB, 4,
                                         render_target) &&
            kajps5::gpu::TileGetBlockLayout(TileBlockFamily::Depth64KB, 8, depth),
        "surface block layouts failed");
  std::uint32_t render_xor = 0;
  std::uint32_t depth_xor = 0;
  Check(kajps5::gpu::TileGetBlockXor(render_target, 1, 2, 3, render_xor) &&
            kajps5::gpu::TileGetBlockXor(depth, 1, 2, 3, depth_xor) &&
            render_xor < render_target.block_size && depth_xor < depth.block_size,
        "surface-z XOR addressing failed");
  kajps5::gpu::TileVolumeLayout volume{};
  volume.total_size = 77;
  Check(!kajps5::gpu::TileGetTextureVolumeLayout(format, UINT32_MAX, 1, 1, 1, 9,
                                                  volume) && volume.total_size == 77,
        "volume failure changed output");
  Check(kajps5::gpu::TileGetTextureVolumeLayout(format, 32, 16, 4, 3, 9, volume) &&
            volume.family == TileBlockFamily::Standard64KB3D && volume.total_size != 0 &&
            volume.level_offsets[0] < volume.total_size,
        "volume success layout failed");
  kajps5::gpu::TileSizeAlign htile{77, 88};
  Check(!kajps5::gpu::TileGetHtileSize(UINT32_MAX, 1, htile) && htile.size == 77,
        "HTile failure changed output");
  Check(kajps5::gpu::TileGetHtileSize(1024, 512, htile) && htile.size == 32768 &&
            htile.align == 32768,
        "HTile success layout failed");
  kajps5::gpu::TileSizeAlign stencil{1, 2}, depth_size{3, 4};
  Check(!kajps5::gpu::TileGetDepthSize(1, 1, UINT32_MAX, 1, 0, false, stencil, htile,
                                        depth_size) && stencil.size == 1 && htile.size == 32768 &&
            depth_size.size == 3,
        "depth failure changed outputs");
  Check(kajps5::gpu::TileGetDepthSize(128, 128, 0, 3, 1, true, stencil, htile,
                                       depth_size) && depth_size.align == 65536 &&
            stencil.align == 65536 && htile.size != 0,
        "depth success layout failed");
  std::uint32_t render_pitch = 0;
  std::uint32_t depth_pitch = 0;
  Check(kajps5::gpu::TileGetRenderTargetPitch(129, 4, render_pitch) &&
            kajps5::gpu::TileGetDepthPitch(129, 4, depth_pitch) &&
            render_pitch == 256 && depth_pitch == 256,
        "render/depth pitch alignment failed");
  const auto rejected_pitch = render_pitch;
  Check(!kajps5::gpu::TileGetRenderTargetPitch(0, 4, render_pitch) &&
            render_pitch == rejected_pitch,
        "render-target pitch failure changed output");
  Check(!kajps5::gpu::TileGetDepthPitch(0, 4, depth_pitch) && depth_pitch == 256,
        "depth pitch failure changed output");
  kajps5::gpu::TileSizeAlign rt_size{77, 88};
  Check(!kajps5::gpu::TileGetRenderTargetSize(1, 0, 1, 4, rt_size) && rt_size.size == 77,
        "render-target size failure changed output");
  Check(kajps5::gpu::TileGetRenderTargetSize(129, 129, render_pitch, 4, rt_size) &&
            rt_size.align == 65536 && rt_size.size != 0,
        "render-target size success failed");
  kajps5::gpu::TileSizeOffset mip_levels[2]{{77}, {78}};
  kajps5::gpu::TilePaddedSize mip_padded[2]{{77, 77}, {78, 78}};
  Check(!kajps5::gpu::TileGetRenderTargetMipLayout(1, 1, 1, 4, 0, rt_size, mip_levels,
                                                    mip_padded) && rt_size.size != 0 &&
            mip_levels[0].size == 77 && mip_padded[0].width == 77,
        "render-target mip failure changed outputs");
  Check(kajps5::gpu::TileGetRenderTargetMipLayout(128, 128, 128, 4, 2, rt_size,
                                                    mip_levels, mip_padded) &&
            rt_size.size != 0 && mip_levels[0].size != 0 && mip_padded[0].width >= 128,
        "render-target mip success failed");
  kajps5::gpu::TileSizeAlign texture{};
  kajps5::gpu::TileSizeOffset levels[4]{};
  kajps5::gpu::TilePaddedSize padded[4]{};
  Check(kajps5::gpu::TileGetTextureSize(format, 129, 65, 0, 4, 9, &texture, levels,
                                         padded) &&
            texture.size != 0 && texture.align == 65536 && levels[0].size != 0 &&
            padded[0].width >= 129,
        "standard texture mip layout failed");
  const auto rejected_texture = texture;
  const auto rejected_level = levels[0];
  Check(!kajps5::gpu::TileGetTextureSize(format, 0, 65, 0, 4, 9, &texture, levels,
                                          padded) &&
            texture.size == rejected_texture.size && texture.align == rejected_texture.align &&
            levels[0].size == rejected_level.size && levels[0].offset == rejected_level.offset,
        "texture layout failure changed output");
  Check(!kajps5::gpu::TileGetTextureSize(format, UINT32_MAX, 1, UINT32_MAX, 1, 9,
                                          &texture, nullptr, nullptr) &&
            texture.size == rejected_texture.size,
        "oversized texture accepted or changed output");
  const auto format16 = kajps5::gpu::Prospero::GpuEnumValue(
      kajps5::gpu::Prospero::BufferFormat::k32_32_32_32Float);
  Check(!kajps5::gpu::TileGetTextureSize(format16, 16384, 16384, 0, 1, 0,
                                          &texture, levels, padded) &&
            texture.size == rejected_texture.size && levels[0].size == rejected_level.size &&
            !kajps5::gpu::TileGetTextureSize(format16, 16384, 16384, 0, 1, 9,
                                               &texture, levels, padded) &&
            texture.size == rejected_texture.size,
        "16-byte linear or tiled overflow changed output");
  const auto overflow_texture = texture;
  const auto overflow_level = levels[0];
  const auto overflow_padded = padded[0];
  Check(!kajps5::gpu::TileGetTextureSize(format16, 16383, 16383, 0, 2, 0,
                                          &texture, levels, padded) &&
            texture.size == overflow_texture.size && levels[0].size == overflow_level.size &&
            padded[0].width == overflow_padded.width &&
            !kajps5::gpu::TileGetTextureSize(format16, 16383, 16383, 0, 2, 9,
                                               &texture, levels, padded) &&
            texture.size == overflow_texture.size,
        "multi-level overflow changed linear or tiled output");
  Check(!kajps5::gpu::TileGetTextureSize(format16, 16370, 16384, 0, 1, 0,
                                          &texture, levels, padded) &&
            texture.size == overflow_texture.size && levels[0].size == overflow_level.size &&
            padded[0].width == overflow_padded.width,
        "padded linear level overflow changed output");
  Check(kajps5::gpu::TileGetTextureVolumeLayout(format16, 16384, 16384, 1, 1, 9,
                                                 volume) &&
            volume.total_size > UINT32_MAX,
        "64-bit volume layout was rejected");
  Check(kajps5::gpu::TileGetTextureSize(format, 1, 1, 0, 1, 1, &texture, nullptr,
                                         nullptr),
        "texture optional output pointers failed");
  kajps5::gpu::TileSizeAlign total{77, 88};
  Check(!kajps5::gpu::TileGetTextureTotalSize(format, 1, 1, 0, 0, 1, 9, false, total) &&
            total.size == 77 && total.align == 88,
        "texture total-size failure changed output");
  Check(kajps5::gpu::TileGetTextureTotalSize(format, 32, 32, 2, 0, 1, 9, false, total) &&
            total.size != 0 && total.align == 65536,
        "texture total-size success failed");
  std::uint32_t texture_pitch = 123;
  Check(kajps5::gpu::TileGetTexturePitch(format, 129, 1, 9, texture_pitch) &&
            texture_pitch == 256 &&
            !kajps5::gpu::TileGetTexturePitch(format, 0, 1, 9, texture_pitch) &&
            texture_pitch == 256,
        "texture pitch contract failed");
  return EXIT_SUCCESS;
}
