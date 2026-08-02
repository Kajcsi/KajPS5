// Adapted from KytyPS5
// src/graphics/shader/recompiler/ShaderRecompiler.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include "gpu/shader/recompiler/ir/ResourceMaterialization.h"
#include "gpu/shader/types.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kajps5::gpu::shader::recompiler {

struct CompileOptions {
	ShaderType                    stage           = ShaderType::Compute;
	ShaderLaneMaskMode            lane_mask_mode  = ShaderLaneMaskMode::NativeWave;
	uint32_t                      wave_size       = 64;
	uint32_t                      user_data_base  = 0;
	uint32_t                      user_data_count = 64;
	uint64_t                      shader_hash     = 0;
	uint64_t                      shader_base     = 0;
	std::optional<uint64_t>       flat_memory_base;
	uint32_t                      descriptor_set       = 0;
	uint32_t                      push_constant_offset = 0;
	bool                          dump_ir              = true;
	bool                          early_dump           = false;
	const char*                   dump_label           = nullptr;
	const uint32_t*               user_data            = nullptr;
	IR::SrtMemoryReader           read_memory          = nullptr;
	void*                         read_memory_data     = nullptr;
	const IR::ResourceSnapshot*   resource_snapshot    = nullptr;
	const ShaderVertexInputInfo*  vertex_input_info    = nullptr;
	const ShaderPixelInputInfo*   pixel_input_info     = nullptr;
	const ShaderComputeInputInfo* compute_input_info   = nullptr;
};

struct CompileResult {
	std::vector<uint32_t> spirv;
	std::string           decoded_dump;
	std::string           ir_dump;
	IR::Program           program;
	IR::ResourceSnapshot  resources;
};

// Recompiles into an isolated result and publishes it only after every phase succeeds.
bool TryRecompile(std::span<const uint32_t> code, const CompileOptions& options,
                  CompileResult& result, std::string* error);

} // namespace kajps5::gpu::shader::recompiler
