#pragma once

#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/math.h"
#include "engine/stream.h"

namespace Lumix {

struct Voxels {
	Voxels(IAllocator& allocator);
	
	void set(const Voxels& rhs);
	void beginRaster(struct AABB aabb, u32 max_res);
	void raster(const Vec3& a, const Vec3& b, const Vec3& c);
	void voxelize(struct Model& model, u32 max_res);
	void computeAO(u32 ray_count);
	float computeAO(const Vec3& p, u32 ray_count);
	void blurAO();
	bool castRay(Vec3 p, Vec3 d) const;
	bool sample(i32 x, i32 y, i32 z, u8* out) const;
	bool sampleAO(i32 x, i32 y, i32 z, float* out) const;
	bool sample(const Vec3& p, u8* out) const;
	bool sampleAO(const Vec3& p, float* out) const;

	IAllocator& m_allocator;
	IVec3 m_grid_resolution;
	OutputMemoryStream m_voxels;
	AABB m_aabb;
	Array<float> m_ao;
	float m_voxel_size;
};

} // namespace Lumix