// Adapted from KytyPS5
// src/graphics/shader/recompiler/ir/ShaderInfoCollection.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERINFOCOLLECTION_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERINFOCOLLECTION_H_

#include "gpu/shader/recompiler/ir/ShaderIR.h"

namespace kajps5::gpu::shader::recompiler::IR {

struct ShaderInfoOptions {
	const ShaderVertexInputInfo*  vertex  = nullptr;
	const ShaderPixelInputInfo*   pixel   = nullptr;
	const ShaderComputeInputInfo* compute = nullptr;
};

// Completes the immutable shader interface after resource tracking. On failure Program::info and
// all completion state remain unchanged.
bool CollectShaderInfo(Program& program, const ShaderInfoOptions& options, std::string* error);

} // namespace kajps5::gpu::shader::recompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERINFOCOLLECTION_H_ */
