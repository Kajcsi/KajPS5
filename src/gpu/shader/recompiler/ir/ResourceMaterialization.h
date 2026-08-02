// Adapted from KytyPS5
// src/graphics/shader/recompiler/ir/ResourceMaterialization.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_

#include "gpu/shader/recompiler/ir/SrtWalker.h"

namespace kajps5::gpu::shader::recompiler::IR {

constexpr uint64_t FlatAddressWindowSize = 0x10000;

struct ResourceSnapshot {
	struct Address {
		uint64_t guest_base   = 0;
		uint64_t binding_base = 0;

		bool operator==(const Address& other) const = default;
	};

	std::vector<DescriptorValue> buffers;
	std::vector<DescriptorValue> images;
	std::vector<DescriptorValue> samplers;
	std::vector<Address>         addresses;
	std::vector<uint32_t>        flattened_srt;
	std::vector<uint32_t>        user_data;
};

bool ValidateResourceSnapshot(const Program& program, const ResourceSnapshot& snapshot,
                              std::string* error);
bool ValidateResourceSpecialization(const Program& program, const ResourceSnapshot& snapshot,
                                    std::string* error);

// Resolves the immutable dense resource topology against one runtime user-data/SRT snapshot.
// On failure the destination is unchanged.
bool MaterializeResources(const Program& program, const SrtRuntime& runtime,
                          ResourceSnapshot& snapshot, std::string* error);

// Applies runtime descriptor shape/format facts to a copied dense topology before layout and
// emission. On failure the program is unchanged.
bool SpecializeResources(Program& program, const ResourceSnapshot& snapshot, std::string* error);

} // namespace kajps5::gpu::shader::recompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_ */
