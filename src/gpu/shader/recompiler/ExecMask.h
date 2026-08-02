// Adapted from KytyPS5
// src/graphics/shader/recompiler/ExecMask.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_EXECMASK_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_EXECMASK_H_

#include "gpu/shader/recompiler/compat.h"

namespace kajps5::gpu::shader::recompiler::Exec {

struct Mask {
	uint32_t low  = 0;
	uint32_t high = 0;
};

struct State {
	uint32_t wave_size = 64;
	Mask     exec;
	Mask     vcc;
	bool     scc = false;
};

Mask FullMask(uint32_t wave_size);
Mask ApplyWaveSize(Mask mask, uint32_t wave_size);

Mask ReadExec(const State& state);
void WriteExec(State& state, Mask mask);
void AndExec(State& state, Mask mask);
void OrExec(State& state, Mask mask);
void XorExec(State& state, Mask mask);
void InvertExec(State& state);
bool IsExecZero(const State& state);
bool IsVccZero(const State& state);

} // namespace kajps5::gpu::shader::recompiler::Exec

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_EXECMASK_H_ */
