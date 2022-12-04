#include "engine/profiler.h"
#include "renderer/model.h"
#include "voxels.h"

namespace Lumix {

template <typename F>
static void forEachTriangle(const Mesh& mesh, const F& f) {
	const u16* indices16 = (u16*)mesh.indices.data();
	const u32* indices32 = (u32*)mesh.indices.data();
	const bool areindices16 = mesh.areIndices16();
	
	for (u32 i = 0; i < (u32)mesh.indices_count; i += 3) {
		u32 indices[3];
		if (areindices16) {
			indices[0] = indices16[i];
			indices[1] = indices16[i + 1];
			indices[2] = indices16[i + 2];
		}
		else {
			indices[0] = indices32[i];
			indices[1] = indices32[i + 1];
			indices[2] = indices32[i + 2];
		}

		const Vec3 p0 = mesh.vertices[indices[0]];
		const Vec3 p1 = mesh.vertices[indices[1]];
		const Vec3 p2 = mesh.vertices[indices[2]];
		f(p0, p1, p2);
	}
}

Voxels::Voxels(IAllocator& allocator)
	: m_allocator(allocator)
	, m_mips(allocator)
	, m_voxels(allocator)
{
}

void Voxels::createMips() {
	IVec3 size = m_grid_resolution;
	ASSERT(isPowOfTwo(size.x));
	ASSERT(isPowOfTwo(size.y));
	ASSERT(isPowOfTwo(size.z));

	m_mips.clear();
	for (u32 i = 0; i < 6; ++i) {
		Mip& mip = m_mips.emplace(m_allocator);
		mip.size.x = maximum(1, size.x >> 1);
		mip.size.y = maximum(1, size.y >> 1);
		mip.size.z = maximum(1, size.z >> 1);
		mip.coverage.resize(size.x * size.y * size.z);

		if (i == 0) {
			memset(mip.coverage.begin(), 0, mip.coverage.byte_size());
			for (i32 z = 0; z < size.z; ++z) {
				for (i32 y = 0; y < size.y; ++y) {
					for (i32 x = 0; x < size.x; ++x) {
						const u8 v = m_voxels[x + (y + z * m_grid_resolution.y) * m_grid_resolution.x];
						const u32 mip_idx = (x >> 1) + ((y >> 1) + (z >> 1) * mip.size.y) * mip.size.x;
						mip.coverage[mip_idx] += v / 8.f;
					}
				}
			}
		}
		else {
			Mip& prev = m_mips[i - 1];
			memset(mip.coverage.begin(), 0, mip.coverage.byte_size());
			for (i32 z = 0; z < size.z; ++z) {
				for (i32 y = 0; y < size.y; ++y) {
					for (i32 x = 0; x < size.x; ++x) {
						const float coverage = prev.coverage[x + (y + z * size.y) * size.x];
						const u32 mip_idx = (x >> 1) + ((y >> 1) + (z >> 1) * mip.size.y) * mip.size.x;
						mip.coverage[mip_idx] += coverage / 8.f;
					}
				}
			}
		}

		size = mip.size;
	}
}

void Voxels::voxelize(Model& model, u32 max_res) {
	PROFILE_FUNCTION();
	ASSERT(model.isReady());

	Vec3 min = Vec3(FLT_MAX);
	Vec3 max = Vec3(-FLT_MAX);
	for (u32 mesh_idx = 0; mesh_idx < (u32)model.getMeshCount(); ++mesh_idx) {
		const Mesh& mesh = model.getMesh(mesh_idx);
		forEachTriangle(mesh, [&](const Vec3& p0, const Vec3& p1, const Vec3& p2){
			min = minimum(min, p0);
			min = minimum(min, p1);
			min = minimum(min, p2);

			max = maximum(max, p0);
			max = maximum(max, p1);
			max = maximum(max, p2);
		});
	}

	const float voxel_size = maximum(max.x - min.x, max.y - min.y, max.z - min.z) / max_res;
	min -= Vec3(voxel_size * 1.5f);
	max += Vec3(voxel_size * 1.5f);
	IVec3 resolution = IVec3(i32((max.x - min.x) / voxel_size), i32((max.y - min.y) / voxel_size), i32((max.z - min.z) / voxel_size));
	resolution.x = nextPow2(resolution.x);
	resolution.y = nextPow2(resolution.y);
	resolution.z = nextPow2(resolution.z);
	m_voxels.resize(resolution.x * resolution.y * resolution.z);
	memset(m_voxels.getMutableData(), 0, m_voxels.size());

	auto to_grid = [&](const Vec3& p){
		return IVec3((p - min) / voxel_size + Vec3(0.5f));
	};

	auto from_grid = [&](const IVec3& p){
		return Vec3(p) * voxel_size + Vec3(0.5f * voxel_size) + min;
	};

	auto intersect = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, IVec3 voxel){
		Vec3 center = from_grid(voxel);
		Vec3 half(0.5f * voxel_size);
		return testAABBTriangleCollision(AABB(center - half, center + half), p0, p1, p2);
	};

	auto setVoxel = [&](i32 i, i32 j, i32 k, u8 value){
		if (i < 0 || j < 0 || k < 0) return;
		if (i >= resolution.x) return;
		if (j >= resolution.y) return;
		if (k >= resolution.z) return;

		m_voxels[i + j * (resolution.x) + k * (resolution.x * resolution.y)] = value;
	};

	for (u32 mesh_idx = 0; mesh_idx < (u32)model.getMeshCount(); ++mesh_idx) {
		const Mesh& mesh = model.getMesh(mesh_idx);
		forEachTriangle(mesh, [&](const Vec3& p0, const Vec3& p1, const Vec3& p2){
			AABB aabb;
			aabb.min = aabb.max = p0;
			aabb.addPoint(p1);
			aabb.addPoint(p2);

			const IVec3 ming = to_grid(aabb.min - Vec3(voxel_size));
			const IVec3 maxg = to_grid(aabb.max);

			for (i32 k = ming.z; k <= maxg.z; ++k) {
				for (i32 j = ming.y; j <= maxg.y; ++j) {
					for (i32 i = ming.x; i <= maxg.x; ++i) {
						if (intersect(p0, p1, p2, IVec3(i, j, k))) {
							setVoxel(i, j, k, 1);
						}
					}
				}
			}
		});
	}

	m_grid_resolution = resolution;
	m_voxel_size = voxel_size;
	createMips();
}

} // namespace Lumix
