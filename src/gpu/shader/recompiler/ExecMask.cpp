// Adapted from KytyPS5
// src/graphics/shader/recompiler/ExecMask.cpp at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/shader/recompiler/ExecMask.h"

namespace kajps5::gpu::shader::recompiler::Exec {

Mask FullMask(uint32_t wave_size) {
	return {0xffffffffu, wave_size == 64u ? 0xffffffffu : 0u};
}

Mask ApplyWaveSize(Mask mask, uint32_t wave_size) {
	const auto full = FullMask(wave_size);
	mask.low &= full.low;
	mask.high &= full.high;
	return mask;
}

Mask ReadExec(const State& state) {
	return ApplyWaveSize(state.exec, state.wave_size);
}

void WriteExec(State& state, Mask mask) {
	state.exec = ApplyWaveSize(mask, state.wave_size);
}

void AndExec(State& state, Mask mask) {
	WriteExec(state, {state.exec.low & mask.low, state.exec.high & mask.high});
}

void OrExec(State& state, Mask mask) {
	WriteExec(state, {state.exec.low | mask.low, state.exec.high | mask.high});
}

void XorExec(State& state, Mask mask) {
	WriteExec(state, {state.exec.low ^ mask.low, state.exec.high ^ mask.high});
}

void InvertExec(State& state) {
	const auto full = FullMask(state.wave_size);
	WriteExec(state, {~state.exec.low & full.low, ~state.exec.high & full.high});
}

bool IsExecZero(const State& state) {
	const auto exec = ReadExec(state);
	return exec.low == 0u && exec.high == 0u;
}

bool IsVccZero(const State& state) {
	const auto vcc = ApplyWaveSize(state.vcc, state.wave_size);
	return vcc.low == 0u && vcc.high == 0u;
}

} // namespace kajps5::gpu::shader::recompiler::Exec
