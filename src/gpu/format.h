// Adapted from KytyPS5
// src/graphics/guest_gpu/gpu_format.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_GUEST_GPU_GPU_FORMAT_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_GUEST_GPU_GPU_FORMAT_H_

#include "gpu/shader/recompiler/compat.h"

namespace kajps5::gpu::Prospero {

uint32_t NumBytesPerElement(uint32_t format);
uint32_t BlockCompressedBytesPerBlock(uint32_t format);
uint32_t RenderTargetBytesPerElement(uint32_t format);
bool     IsSupportedTextureFormat(uint32_t format);
bool     IsUintTextureFormat(uint32_t format);
bool     IsFmaskTextureFormat(uint32_t format);

// Returns a canonical format identifier for texture storage aliases. sRGB and
// UNORM spellings that have identical guest bytes share this value, while the
// original format remains the view format.
uint32_t StorageAliasFormat(uint32_t format);

} // namespace kajps5::gpu::Prospero

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_GUEST_GPU_GPU_FORMAT_H_ */
