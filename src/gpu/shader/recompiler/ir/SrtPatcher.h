// Adapted from KytyPS5
// src/graphics/shader/recompiler/ir/SrtPatcher.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTPATCHER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTPATCHER_H_

#include "gpu/shader/recompiler/ir/ShaderIR.h"

namespace kajps5::gpu::shader::recompiler::IR {

// Replaces each immediate ReadConst producer selected by the SRT plan with a dense flat-buffer
// dword load. Dynamic-offset reads remain explicit. On failure no instruction is changed.
bool PatchSrtReads(Program& program, std::string* error);

} // namespace kajps5::gpu::shader::recompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTPATCHER_H_ */
