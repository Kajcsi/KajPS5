// Adapted from KytyPS5
// src/graphics/shader/recompiler/decompiler/ExportOps.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_EXPORTOPS_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_EXPORTOPS_H_

#include "gpu/shader/recompiler/decompiler/ShaderDecoder.h"

namespace kajps5::gpu::shader::recompiler::Decoder {

bool DecodeExp(uint32_t pc, std::span<const uint32_t> code, uint32_t word_index, Instruction& inst,
               std::string* error);

} // namespace kajps5::gpu::shader::recompiler::Decoder

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_EXPORTOPS_H_ */
