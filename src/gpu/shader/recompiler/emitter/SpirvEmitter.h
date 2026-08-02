// Adapted from KytyPS5
// src/graphics/shader/recompiler/emitter/SpirvEmitter.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVEMITTER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVEMITTER_H_

#include "gpu/shader/recompiler/compat.h"
#include "gpu/shader/recompiler/ir/ResourceMaterialization.h"

#include <vector>

namespace kajps5::gpu::shader::recompiler::Spirv {

bool ProgramRequiresExactSubgroupSize(const IR::Program& program);

bool EmitProgram(const IR::Program& program, const IR::ResourceSnapshot& resources,
                 const ShaderVertexInputInfo*  vertex_input_info,
                 const ShaderPixelInputInfo*   pixel_input_info,
                 const ShaderComputeInputInfo* compute_input_info, std::vector<uint32_t>& spirv,
                 std::string* error);

} // namespace kajps5::gpu::shader::recompiler::Spirv

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVEMITTER_H_ */
