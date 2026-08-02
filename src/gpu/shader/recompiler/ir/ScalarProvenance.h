// Adapted from KytyPS5
// src/graphics/shader/recompiler/ir/ScalarProvenance.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SCALARPROVENANCE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SCALARPROVENANCE_H_

#include "gpu/shader/recompiler/ir/ShaderIR.h"

namespace kajps5::gpu::shader::recompiler::IR {

// Builds scalar reaching definitions and attaches descriptor sources to individual memory uses.
bool BuildScalarProvenance(Program& program, std::string* error);

uint32_t ScalarValueArgCount(ScalarValueOp op);

const DescriptorValue* GetDescriptorSource(const Program& program, uint32_t source);
bool                   DescriptorSourceResolved(const Program& program, uint32_t source);

std::string ScalarValueToString(const ScalarProvenance& provenance, uint32_t value);

} // namespace kajps5::gpu::shader::recompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SCALARPROVENANCE_H_ */
