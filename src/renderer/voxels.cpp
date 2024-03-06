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
	Vec3 s = Vec3(m_grid_resolution);
	auto sample = [&](Vec3 p){
		IVec3 i(p);
		return m_voxels[i.x + (i.y + i.z * m_grid_resolution.y) * m_grid_resolution.x];
	};

	p += d;
	while (p.x > 0 && p.y > 0 && p.z > 0 && p.x < s.x  && p.y < s.y && p.z < s.z) {
		if (sample(p)) return true;
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
	m_ao.resize(m_grid_resolution.x * m_grid_resolution.y * m_grid_resolution.z);
	for (i32 z = 0; z < m_grid_resolution.z; ++z) {
		for (i32 y = 0; y < m_grid_resolution.y; ++y) {
			for (i32 x = 0; x < m_grid_resolution.x; ++x) {
				float ao = 1;
				for (u32 d = 0; d < ray_count; ++d) {
					Vec3 dir = Vec3(randFloat(), randFloat(), randFloat()) * 2.f - 1.f;
					dir /= maximum(fabsf(dir.x), fabsf(dir.y), fabsf(dir.z));
					Vec3 p((float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f);
					p += dir;
					if (castRay(p, dir)) {
						ao -= 1.f / ray_count;
					}
				}
				m_ao[x + (y + z * m_grid_resolution.y) * m_grid_resolution.x] = ao;
			}
		}
	}
}

void Voxels::blurAO() {
	Array<float> blurred(m_allocator);
	blurred.resize(m_ao.size());
	auto sampleAO = [&](i32 x, i32 y, i32 z){
		x = clamp(x, 0, m_grid_resolution.x - 1);
		y = clamp(y, 0, m_grid_resolution.y - 1);
		z = clamp(z, 0, m_grid_resolution.z - 1);
		const u32 idx = x + (y + z * m_grid_resolution.y) * m_grid_resolution.x;
		return m_ao[idx];
	};
	for (i32 z = 0; z < m_grid_resolution.z; ++z) {
		for (i32 y = 0; y < m_grid_resolution.y; ++y) {
			for (i32 x = 0; x < m_grid_resolution.x; ++x) {
				float v = 0;
				for (i32 c = -1; c <= 1; ++c) {
					for (i32 b = -1; b <= 1; ++b) {
						for (i32 a = -1; a <= 1; ++a) {
							v += sampleAO(x + a, y + b, z + c);
						}
					}
				}
				const u32 idx = x + (y + z * m_grid_resolution.y) * m_grid_resolution.x;
				blurred[idx] = v / 9.f;
			}
		}
	}
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
	auto to_grid = [&](const Vec3& p){
		return IVec3((p - m_aabb.min) / m_voxel_size + Vec3(0.5f));
	};

	auto from_grid = [&](const IVec3& p){
		return Vec3(p) * m_voxel_size + Vec3(0.5f * m_voxel_size) + m_aabb.min;
	};

	auto intersect = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, IVec3 voxel){
		Vec3 center = from_grid(voxel);
		Vec3 half(0.5f * m_voxel_size);
		return testAABBTriangleCollision(AABB(center - half, center + half), p0, p1, p2);
	};

	auto setVoxel = [&](i32 i, i32 j, i32 k, u8 value){
		if (i < 0 || j < 0 || k < 0) return;
		if (i >= m_grid_resolution.x) return;
		if (j >= m_grid_resolution.y) return;
		if (k >= m_grid_resolution.z) return;

		m_voxels[i + j * (m_grid_resolution.x) + k * (m_grid_resolution.x * m_grid_resolution.y)] = value;
	};

	AABB aabb;
	aabb.min = aabb.max = p0;
	aabb.addPoint(p1);
	aabb.addPoint(p2);

	const IVec3 ming = to_grid(aabb.min - Vec3(m_voxel_size));
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
