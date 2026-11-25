#include "core/job_system.h"
#include "core/profiler.h"
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
	, m_voxels(allocator)
	, m_ao(allocator)
{
}

void Voxels::set(const Voxels& rhs) {
	m_grid_resolution = rhs.m_grid_resolution;
	m_voxels = rhs.m_voxels;
	m_aabb = rhs.m_aabb;
	m_voxel_size = rhs.m_voxel_size;
	m_ao.resize(rhs.m_ao.size());
	memcpy(m_ao.begin(), rhs.m_ao.begin(), m_ao.byte_size());
}

bool Voxels::sample(const Vec3& p, u8* out) const {
	IVec3 ip = IVec3((p - m_aabb.min) / (m_aabb.max - m_aabb.min) * Vec3(m_grid_resolution));
	return sample(ip.x, ip.y, ip.z, out);
}

bool Voxels::sampleAO(const Vec3& p, float* out) const {
	IVec3 ip = IVec3((p - m_aabb.min) / (m_aabb.max - m_aabb.min) * Vec3(m_grid_resolution));
	return sampleAO(ip.x, ip.y, ip.z, out);
}

bool Voxels::sampleAO(i32 x, i32 y, i32 z, float* out) const {
	if (x < 0) return false;
	if (y < 0) return false;
	if (z < 0) return false;
	if (x >= m_grid_resolution.x) return false;
	if (y >= m_grid_resolution.y) return false;
	if (z >= m_grid_resolution.z) return false;
	
	*out = m_ao[x + (y + z * m_grid_resolution.y) * m_grid_resolution.x];
	return true;
}
bool Voxels::sample(i32 x, i32 y, i32 z, u8* out) const {
	if (x < 0) return false;
	if (y < 0) return false;
	if (z < 0) return false;
	if (x >= m_grid_resolution.x) return false;
	if (y >= m_grid_resolution.y) return false;
	if (z >= m_grid_resolution.z) return false;
	
	*out = m_voxels[x + (y + z * m_grid_resolution.y) * m_grid_resolution.x];
	return true;
}

bool Voxels::castRay(Vec3 p, Vec3 d) const {
	const u8* voxels = m_voxels.data();
	const i32 rx = m_grid_resolution.x;
	const i32 ry = m_grid_resolution.y;
	const float sx = (float)rx;
	const float sy = (float)ry;
	const float sz = (float)m_grid_resolution.z;

	p += d;
	while (p.x > 0 && p.y > 0 && p.z > 0 && p.x < sx && p.y < sy && p.z < sz) {
		if (voxels[(i32)p.x + ((i32)p.y + (i32)p.z * ry) * rx]) return true;
		p += d;
	}
	return false;
}

float Voxels::computeAO(const Vec3& p, u32 ray_count) {
	if (m_ao.empty()) {
		m_ao.resize(m_grid_resolution.x * m_grid_resolution.y * m_grid_resolution.z);
		for (float& f : m_ao) f = -1;
	}
	
	IVec3 ip = IVec3((p - m_aabb.min) / (m_aabb.max - m_aabb.min) * Vec3(m_grid_resolution));

	u32 idx = ip.x + (ip.y + ip.z * m_grid_resolution.y) * m_grid_resolution.x;
	if (m_ao[idx] >= 0) return m_ao[idx];

	float ao = 1;
	for (u32 d = 0; d < ray_count; ++d) {
		Vec3 dir = Vec3(randFloat(), randFloat(), randFloat()) * 2.f - 1.f;
		dir /= maximum(fabsf(dir.x), fabsf(dir.y), fabsf(dir.z));
		Vec3 p = Vec3((float)ip.x, (float)ip.y, (float)ip.z) + Vec3(0.5f * m_voxel_size);
		p += dir;
		if (castRay(p, dir)) {
			ao -= 1.f / ray_count;
		}
	}
	m_ao[idx] = ao;
	return ao;
}

void Voxels::computeAO(u32 ray_count) {
	PROFILE_FUNCTION();
	const i32 rx = m_grid_resolution.x;
	const i32 ry = m_grid_resolution.y;
	const i32 rz = m_grid_resolution.z;
	m_ao.resize(rx * ry * rz);
	const float inv_ray_count = 1.f / ray_count;

	jobs::forEach(rz, 1, [&](i32 from, i32 to) {
		for (i32 z = from; z < to; ++z) {
			RandomGenerator rng((u32(z) + 1u) * 521288629u | 1u, (u32(z) + 1u) * 362436069u | 1u);
			for (i32 y = 0; y < ry; ++y) {
				for (i32 x = 0; x < rx; ++x) {
					float ao = 1.f;
					for (u32 d = 0; d < ray_count; ++d) {
						Vec3 dir = Vec3(rng.randFloat(), rng.randFloat(), rng.randFloat()) * 2.f - 1.f;
						const float m = maximum(fabsf(dir.x), fabsf(dir.y), fabsf(dir.z));
						if (m < 1e-6f) continue;
						dir /= m;
						Vec3 p((float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f);
						p += dir;
						if (castRay(p, dir)) {
							ao -= inv_ray_count;
						}
					}
					m_ao[x + (y + z * ry) * rx] = ao;
				}
			}
		}
	});
}

void Voxels::blurAO() {
	PROFILE_FUNCTION();
	Array<float> blurred(m_allocator);
	blurred.resize(m_ao.size());
	const i32 rx = m_grid_resolution.x;
	const i32 ry = m_grid_resolution.y;
	const i32 rz = m_grid_resolution.z;
	auto sampleAO = [&](i32 x, i32 y, i32 z){
		x = clamp(x, 0, rx - 1);
		y = clamp(y, 0, ry - 1);
		z = clamp(z, 0, rz - 1);
		return m_ao[x + (y + z * ry) * rx];
	};
	jobs::forEach(rz, 1, [&](i32 from, i32 to) {
		for (i32 z = from; z < to; ++z) {
			for (i32 y = 0; y < ry; ++y) {
				for (i32 x = 0; x < rx; ++x) {
					float v = 0;
					for (i32 c = -1; c <= 1; ++c) {
						for (i32 b = -1; b <= 1; ++b) {
							for (i32 a = -1; a <= 1; ++a) {
								v += sampleAO(x + a, y + b, z + c);
							}
						}
					}
					blurred[x + (y + z * ry) * rx] = v / 9.f;
				}
			}
		}
	});
	m_ao = blurred.move();
}

void Voxels::beginRaster(AABB aabb, u32 max_res) {

	Vec3 max = aabb.max;
	Vec3 min = aabb.min;
	const float voxel_size = maximum(max.x - min.x, max.y - min.y, max.z - min.z) / max_res;
	min -= Vec3(voxel_size * 1.5f);
	max += Vec3(voxel_size * 1.5f);
	IVec3 resolution = IVec3(i32((max.x - min.x) / voxel_size), i32((max.y - min.y) / voxel_size), i32((max.z - min.z) / voxel_size));
	m_voxels.resize(resolution.x * resolution.y * resolution.z);
	memset(m_voxels.getMutableData(), 0, m_voxels.size());
	m_grid_resolution = resolution;
	m_voxel_size = voxel_size;
	m_aabb = {min, max};
}

void Voxels::raster(const Vec3& p0, const Vec3& p1, const Vec3& p2) {
	const float inv_voxel_size = 1.f / m_voxel_size;
	const Vec3 mn = minimum(p0, minimum(p1, p2));
	const Vec3 mx = maximum(p0, maximum(p1, p2));

	i32 min_x = (i32)floorf((mn.x - m_aabb.min.x) * inv_voxel_size);
	i32 min_y = (i32)floorf((mn.y - m_aabb.min.y) * inv_voxel_size);
	i32 min_z = (i32)floorf((mn.z - m_aabb.min.z) * inv_voxel_size);
	i32 max_x = (i32)floorf((mx.x - m_aabb.min.x) * inv_voxel_size);
	i32 max_y = (i32)floorf((mx.y - m_aabb.min.y) * inv_voxel_size);
	i32 max_z = (i32)floorf((mx.z - m_aabb.min.z) * inv_voxel_size);

	const i32 rx = m_grid_resolution.x;
	const i32 ry = m_grid_resolution.y;
	const i32 rz = m_grid_resolution.z;
	if (max_x < 0 || max_y < 0 || max_z < 0 || min_x >= rx || min_y >= ry || min_z >= rz) return;

	min_x = maximum(min_x, 0);
	min_y = maximum(min_y, 0);
	min_z = maximum(min_z, 0);
	max_x = minimum(max_x, rx - 1);
	max_y = minimum(max_y, ry - 1);
	max_z = minimum(max_z, rz - 1);

	u8* voxels = m_voxels.getMutableData();
	// fast path: whole triangle lies inside a single voxel
	if (min_x == max_x && min_y == max_y && min_z == max_z) {
		voxels[min_x + (min_y + min_z * ry) * rx] = 1;
		return;
	}

	const Vec3 half(0.5f * m_voxel_size);
	for (i32 k = min_z; k <= max_z; ++k) {
		const float cz = m_aabb.min.z + (k + 0.5f) * m_voxel_size;
		for (i32 j = min_y; j <= max_y; ++j) {
			const float cy = m_aabb.min.y + (j + 0.5f) * m_voxel_size;
			const i32 row = (j + k * ry) * rx;
			for (i32 i = min_x; i <= max_x; ++i) {
				if (voxels[i + row]) continue;
				const Vec3 center(m_aabb.min.x + (i + 0.5f) * m_voxel_size, cy, cz);
				if (testAABBTriangleCollision(AABB(center - half, center + half), p0, p1, p2)) {
					voxels[i + row] = 1;
				}
			}
		}
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


	beginRaster({min, max}, max_res);

	for (u32 mesh_idx = 0; mesh_idx < (u32)model.getMeshCount(); ++mesh_idx) {
		const Mesh& mesh = model.getMesh(mesh_idx);
		forEachTriangle(mesh, [&](const Vec3& p0, const Vec3& p1, const Vec3& p2){
			raster(p0, p1, p2);
		});
	}
}

} // namespace Lumix
