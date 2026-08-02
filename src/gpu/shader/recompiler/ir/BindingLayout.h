// Adapted from KytyPS5
// src/graphics/shader/recompiler/ir/BindingLayout.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_BINDINGLAYOUT_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_BINDINGLAYOUT_H_

#include "gpu/shader/recompiler/ir/ShaderIR.h"

namespace kajps5::gpu::shader::recompiler::IR {

struct BindingLayoutOptions {
	uint32_t descriptor_set       = 0;
	uint32_t push_constant_offset = 0;
	uint32_t max_push_dwords      = 32;
};

bool AllocateBindings(Program& program, const BindingLayoutOptions& options, std::string* error);

const DescriptorBinding* FindBinding(const BindingLayout& layout, DescriptorBindingKind kind);

} // namespace kajps5::gpu::shader::recompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_BINDINGLAYOUT_H_ */
