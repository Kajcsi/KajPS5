// Adapted from KytyPS5
// src/graphics/shader/recompiler/ir/ReadLaneElimination.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_READLANEELIMINATION_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_READLANEELIMINATION_H_

#include "gpu/shader/recompiler/ir/ShaderIR.h"

namespace kajps5::gpu::shader::recompiler::IR {

struct ReadLaneEliminationStats {
	uint32_t rewritten_reads = 0;
	uint32_t shadow_writes   = 0;
};

// Replaces fixed-lane ReadLane operations that are reached by a matching WriteLane on every
// control-flow path. A synthetic scalar register snapshots the value at WriteLane execution time,
// so the rewrite remains valid when the source SGPR is subsequently overwritten.
[[nodiscard]] ReadLaneEliminationStats EliminateReadLane(Program& program);

} // namespace kajps5::gpu::shader::recompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_READLANEELIMINATION_H_ */
