// Adapted from KytyPS5
// src/graphics/shader/shaderBindings.cpp at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/shader/bindings.h"

#include "gpu/definitions.h"

namespace kajps5::gpu {

ResourceDescriptorType ShaderClassifyResourceDescriptor(const uint32_t* desc) {
	if (desc == nullptr) {
		return ResourceDescriptorType::Unused;
	}

	const auto raw_type = (desc[3] >> 28u) & 0xfu;
	switch (static_cast<Prospero::ImageType>(raw_type)) {
		case Prospero::ImageType::kColor1D:
		case Prospero::ImageType::kColor2D:
		case Prospero::ImageType::kColor3D:
		case Prospero::ImageType::kCube:
		case Prospero::ImageType::kColor1DArray:
		case Prospero::ImageType::kColor2DArray:
		case Prospero::ImageType::kColor2DMsaa:
		case Prospero::ImageType::kColor2DMsaaArray: return ResourceDescriptorType::Texture;
		default: break;
	}

	// The mixed resource tag shares a byte with texture fields, so texture type wins.
	switch (static_cast<Prospero::DescriptorKind>((desc[5] >> 27u) & 0x3u)) {
		case Prospero::DescriptorKind::kBuffer: return ResourceDescriptorType::Buffer;
		case Prospero::DescriptorKind::kSampler: return ResourceDescriptorType::Sampler;
		case Prospero::DescriptorKind::kUnused: return ResourceDescriptorType::Unused;
		default: break;
	}

	return (raw_type & Prospero::GpuEnumValue(Prospero::ImageType::kColor1D)) == 0
	           ? ResourceDescriptorType::Buffer
	           : ResourceDescriptorType::Unused;
}

} // namespace kajps5::gpu
