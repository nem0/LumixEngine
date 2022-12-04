#pragma once

#include "engine/array.h"
#include "engine/math.h"
#include "engine/stream.h"

namespace Lumix {

struct Voxels {
	Voxels(IAllocator& allocator);
	void createMips();
	void voxelize(struct Model& model, u32 max_res);

	struct Mip {
		Mip(IAllocator& allocator) : coverage(allocator) {}
		Array<float> coverage;
		IVec3 size;
	};

	IAllocator& m_allocator;
	IVec3 m_grid_resolution;
	OutputMemoryStream m_voxels;
	Array<Mip> m_mips;
	float m_voxel_size;
};

} // namespace Lumix