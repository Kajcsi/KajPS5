// Directly adapted from KytyPS5 src/graphics/guest_gpu/tile.{h,cpp} at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <array>
#include <cstdint>

namespace kajps5::gpu {

struct TileSizeAlign { std::uint32_t size = 0; std::uint32_t align = 0; };
struct TileSizeOffset {
  std::uint32_t size = 0;
  std::uint32_t offset = 0;
  std::uint32_t src_size = 0;
  std::uint32_t src_offset = 0;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
};
struct TilePaddedSize { std::uint32_t width = 0; std::uint32_t height = 0; };
enum class TileBlockFamily : std::uint32_t {
  Standard256B, Standard4KB, Standard4KB3D, Standard64KB, Standard64KB3D,
  Prt64KB, Prt64KB3D, RenderTarget64KB, Depth64KB, Count,
};
struct TileBlockLayout {
  TileBlockFamily family = TileBlockFamily::Standard256B;
  std::uint32_t bytes_per_element = 0, block_size = 0, block_width = 0,
                block_height = 0, block_depth = 0;
};
struct TileVolumeLayout {
  TileBlockFamily family = TileBlockFamily::Count;
  std::uint32_t bytes_per_element = 0, texel_width = 1, texel_height = 1,
                first_tail_level = 0, block_depth = 1;
  std::uint64_t block_slice_size = 0, total_size = 0;
  std::array<std::uint64_t, 16> level_offsets{}, level_sizes{};
  std::array<std::uint32_t, 16> tail_x{}, tail_y{}, level_widths{}, level_heights{};
};

bool TileGetBlockLayout(TileBlockFamily family, std::uint32_t bytes_per_element,
                        TileBlockLayout& layout);
bool TileGetBlockOffset(const TileBlockLayout& layout, std::uint32_t x,
                        std::uint32_t y, std::uint32_t z,
                        std::uint32_t& byte_offset);
bool TileGetBlockXor(const TileBlockLayout& layout, std::uint32_t block_x,
                     std::uint32_t block_y, std::uint32_t& byte_offset);
bool TileGetBlockXor(const TileBlockLayout& layout, std::uint32_t block_x,
                     std::uint32_t block_y, std::uint32_t block_z,
                     std::uint32_t& byte_offset);
bool TileIsStandard256BTextureSupported(std::uint32_t format);
bool TileIsStandard4KBTextureSupported(std::uint32_t format);
bool TileIsStandard64KBTextureSupported(std::uint32_t format);
bool TileGetTextureVolumeLayout(std::uint32_t format, std::uint32_t width,
                                std::uint32_t height, std::uint32_t depth,
                                std::uint32_t levels, std::uint32_t tile,
                                TileVolumeLayout& layout);
bool TileGetHtileSize(std::uint32_t width, std::uint32_t height, TileSizeAlign& htile_size);
bool TileGetDepthSize(std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
                      std::uint32_t z_format, std::uint32_t stencil_format, bool htile,
                      TileSizeAlign& stencil_size, TileSizeAlign& htile_size,
                      TileSizeAlign& depth_size, std::uint32_t num_fragments_log2 = 0);
bool TileGetRenderTargetPitch(std::uint32_t width,
                              std::uint32_t bytes_per_element,
                              std::uint32_t& pitch,
                              std::uint32_t num_fragments_log2 = 0);
bool TileGetDepthPitch(std::uint32_t width, std::uint32_t bytes_per_element,
                       std::uint32_t& pitch,
                       std::uint32_t num_fragments_log2 = 0);
bool TileGetRenderTargetSize(std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
                             std::uint32_t bytes_per_element, TileSizeAlign& total_size,
                             std::uint32_t num_fragments_log2 = 0);
bool TileGetRenderTargetMipLayout(std::uint32_t width, std::uint32_t height,
                                  std::uint32_t pitch, std::uint32_t bytes_per_element,
                                  std::uint32_t levels, TileSizeAlign& total_size,
                                  TileSizeOffset* level_sizes, TilePaddedSize* padded_size);
bool TileGetTextureSize(std::uint32_t format, std::uint32_t width,
                        std::uint32_t height, std::uint32_t pitch,
                        std::uint32_t levels, std::uint32_t tile,
                        TileSizeAlign* total_size, TileSizeOffset* level_sizes,
                        TilePaddedSize* padded_size);
bool TileGetTextureTotalSize(std::uint32_t format, std::uint32_t width,
                             std::uint32_t height, std::uint32_t depth,
                             std::uint32_t pitch, std::uint32_t levels,
                             std::uint32_t tile, bool volume_texture,
                             TileSizeAlign& total_size);
bool TileGetTexturePitch(std::uint32_t format, std::uint32_t width,
                         std::uint32_t levels, std::uint32_t tile,
                         std::uint32_t& pitch);

}  // namespace kajps5::gpu
