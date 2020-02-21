// clipping is inspired by urho 
// https://github.com/urho3d/Urho3D/blob/9c666ae6b82e21e67bfb003d84e95e26f8a3d341/Source/Urho3D/Graphics/OcclusionBuffer.cpp#L681 

#include "occlusion_buffer.h"
#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/math.h"
#include "engine/profiler.h"
#include "engine/universe.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"


namespace Lumix
{


static const int Z_SCALE = 1 << 30;
static const int XY_SCALE = 1 << 16;
static const int WIDTH = 384;
static const int HEIGHT = 192;


OcclusionBuffer::OcclusionBuffer(IAllocator& allocator)
	: m_mips(allocator)
	, m_allocator(allocator)
{
}


void OcclusionBuffer::setCamera(const DVec3& pos, const Quat& rot, const Matrix& projection)
{
	m_view_projection_matrix = projection * rot.toMatrix();
	m_camera_pos = pos;
}


static LUMIX_FORCE_INLINE Vec3 toViewport(const Vec4& v)
{
	float inv = 0.5f / v.w;
	return { v.x * inv + 0.5f, v.y * inv + 0.5f, v.z * inv + 0.5f };
}


Vec3 OcclusionBuffer::transform(const Transform& world_transform, float x, float y, float z)
{
	Vec3 tmp = world_transform.scale * Vec3(x, y, z);
	tmp = world_transform.rot.rotate(tmp);
	tmp += (world_transform.pos - m_camera_pos).toFloat();
	return m_view_projection_matrix.transformPoint(tmp);
}


bool OcclusionBuffer::isOccluded(const Transform& world_transform, const AABB& aabb)
{
	// TODO simd
	const Vec3 vertices[] = {
		transform(world_transform, aabb.min.x, aabb.min.y, aabb.min.z),
		transform(world_transform, aabb.min.x, aabb.min.y, aabb.max.z),
		transform(world_transform, aabb.min.x, aabb.max.y, aabb.min.z),
		transform(world_transform, aabb.min.x, aabb.max.y, aabb.max.z),
		transform(world_transform, aabb.max.x, aabb.min.y, aabb.min.z),
		transform(world_transform, aabb.max.x, aabb.min.y, aabb.max.z),
		transform(world_transform, aabb.max.x, aabb.max.y, aabb.min.z),
		transform(world_transform, aabb.max.x, aabb.min.y, aabb.min.z)
	};
	
	Vec3 min = vertices[0];
	Vec3 max = vertices[0];

	for (u32 i = 1; i < lengthOf(vertices); ++i)
	{
		min.x = minimum(vertices[i].x, min.x);
		min.y = minimum(vertices[i].y, min.y);
		min.z = minimum(vertices[i].z, min.z);

		max.x = maximum(vertices[i].x, max.x);
		max.y = maximum(vertices[i].y, max.y);
	}

	if (max.x < 0) return false;
	if (max.y < 0) return false;
	if (min.x >= 1) return false;
	if (min.y >= 1) return false;

	const int min_x = maximum(0, int(min.x * (WIDTH - 1) + 0.5f));
	const int max_x = minimum(WIDTH - 1, int(max.x * (WIDTH - 1) + 0.5f));
	const int min_y = maximum(0, int(min.y * (HEIGHT - 1) + 0.5f));
	const int max_y = minimum(HEIGHT - 1, int(max.y * (HEIGHT - 1) + 0.5f));

	const int z = int(min.z * Z_SCALE);

	// TODO check whole hierarchy
	const int* LUMIX_RESTRICT depth = &m_mips[0][0];
	for (int j = min_y; j <= max_y; ++j)
	{
		for (int i = min_x; i < max_x; ++i)
		{
			if (depth[i + j * WIDTH] > z) return false;
		}
	}
	return true;
}


void OcclusionBuffer::init()
{
	PROFILE_FUNCTION();
	ASSERT(m_mips.empty());

	int w = WIDTH;
	int h = HEIGHT;

	while (w % 2 != 1 && h % 2 != 1)
	{
		auto& mip = m_mips.emplace(m_allocator);
		mip.resize(w * h);
		w >>= 1;
		h >>= 1;
	}
}


void OcclusionBuffer::buildHierarchy()
{
	// TODO multithreading
	PROFILE_FUNCTION();
	for (int level = 1; level < m_mips.size(); ++level)
	{
		int prev_w = WIDTH >> (level - 1);
		int w = WIDTH >> level;
		int h = HEIGHT >> level;
		for (int j = 0; j < h; ++j)
		{
			int prev_j = j << 1;
			const int* LUMIX_RESTRICT prev_mip = &m_mips[level - 1][prev_j * prev_w];
			int* LUMIX_RESTRICT mip = &m_mips[level][j * w];
			int* end = mip + w;
			while (mip != end)
			{
				*mip = maximum(prev_mip[0], prev_mip[1], prev_mip[prev_w], prev_mip[prev_w + 1]);
				++mip;
				prev_mip += 2;
			}
		}
	}
}


LUMIX_FORCE_INLINE void rasterizeProjectedTriangle(Vec3(&v)[3], int* depth)
{
	Vec3 n = crossProduct(v[1] - v[0], v[2] - v[0]);
	bool is_backface = n.z <= 0;
	if (is_backface) return;

	if (v[0].y > v[2].y) swap(v[0], v[2]);
	if (v[0].y > v[1].y) swap(v[0], v[1]);
	if (v[1].y > v[2].y) swap(v[1], v[2]);

	int width = WIDTH;
	int height = HEIGHT;

	struct Point { int x, y; };
	Point p0 = { int(v[0].x * (width - 1) + 0.5f), int(v[0].y * (height - 1) + 0.5f) };
	Point p1 = { int(v[1].x * (width - 1) + 0.5f), int(v[1].y * (height - 1) + 0.5f) };
	Point p2 = { int(v[2].x * (width - 1) + 0.5f), int(v[2].y * (height - 1) + 0.5f) };

	if (p0.y == p2.y) return;

	float xdz = -n.x / n.z;
	int xdz_int = int(xdz * Z_SCALE / WIDTH);

	if (p1.y != p0.y)
	{
		int dl = (p1.x - p0.x) * 2 * XY_SCALE / (2*p1.y - 2*p0.y + 1);
		int dr = (p2.x - p0.x) * 2 * XY_SCALE / (2*p2.y - 2*p0.y + 1);
		Vec3 left_p = dl > dr ? v[2] : v[1];
		int dz_left = int((left_p.z - v[0].z) * Z_SCALE / (left_p.y - (v[0].y + 0.5f / HEIGHT)) / HEIGHT);
		int z_left = int(v[0].z * Z_SCALE) + (dz_left >> 1);
		if (dl > dr) swap(dl, dr);
		int left = p0.x * XY_SCALE + (dl >> 1);
		int right = p0.x * XY_SCALE + (dr >> 1);
		for (int y = p0.y; y <= p1.y; ++y)
		{
			int base = y * width + left / XY_SCALE;
			int z = z_left;
			for (int x = 0, n = right / XY_SCALE - left / XY_SCALE; x <= n; ++x)
			{
				if (z < depth[base + x]) depth[base + x] = z;
				z += xdz_int;
			}
			left += dl;
			right += dr;
			z_left += dz_left;
		}
	}
	
	if (p2.y == p1.y) return;

	int dl = -(p1.x - p2.x) * 2 * XY_SCALE / (2 * p1.y - 2 * p2.y - 1);
	int dr = -(p0.x - p2.x) * 2 * XY_SCALE / (2 * p0.y - 2 * p2.y - 1);
	Vec3 left_p = dl > dr ? v[0] : v[1];
	int dz_left = -int((left_p.z - v[2].z) * Z_SCALE / (left_p.y - (v[2].y - 0.5f / HEIGHT)) / HEIGHT);

	if (dl > dr) swap(dl, dr);
	int left = p2.x * XY_SCALE + (dl >> 1);
	int right = p2.x * XY_SCALE + (dr >> 1);
	int z_left = int(v[2].z * Z_SCALE) + (dz_left >> 1);
	for (int y = p2.y; y >= p1.y; --y)
	{
		int base = y * width + left / XY_SCALE;
		int z = z_left;
		for (int x = 0, n = right / XY_SCALE - left / XY_SCALE; x <= n; ++x)
		{
			if (z < depth[base + x]) depth[base + x] = z;
			z += xdz_int;
		}
		left += dl;
		right += dr;
		z_left += dz_left;
	}

}


static LUMIX_FORCE_INLINE Vec4 clip(const Vec4& v0, const Vec4& v1, float d0, float d1)
{
	float t = d0 / (d0 - d1);
	return v0 + t * (v1 - v0);
}


static LUMIX_FORCE_INLINE bool tryClip2Vertices(Vec4* vertices, int index, float* d, int d0, int d1, int d2)
{
	if (d[d0] < 0.0f && d[d1] < 0.0f)
	{
		vertices[index + d0] = clip(vertices[index + d0], vertices[index + d2], d[d0], d[d2]);
		vertices[index + d1] = clip(vertices[index + d1], vertices[index + d2], d[d1], d[d2]);
		return true;
	}
	return false;
}


static LUMIX_FORCE_INLINE bool tryClip1Vertex(Vec4* vertices, int index, float* d, int d0, int d1, int d2, bool* triangles, int& tringles_count)
{
	bool is_behind_plane = d[d0] < 0;
	if (!is_behind_plane) return false;

	int new_index = tringles_count * 3;
	triangles[tringles_count] = true;
	++tringles_count;

	vertices[new_index + 0] = clip(vertices[index + d0], vertices[index + d2], d[d0], d[d2]);
	vertices[new_index + 1] = clip(vertices[index + d0], vertices[index + d1], d[d0], d[d1]);
	vertices[new_index + 2] = vertices[index + d2];
	vertices[index + d0] = vertices[new_index + 1];

	return true;
}


static void clipTriangles(const Vec4& plane, Vec4* vertices, bool* triangles, int& triangles_count)
{
	int count = triangles_count;
	for (int i = 0; i < count; ++i)
	{
		if (!triangles[i]) continue;
		int index = i * 3;
		float d[3] = { dotProduct(plane, vertices[index])
			, dotProduct(plane, vertices[index + 1])
			, dotProduct(plane, vertices[index + 2])
		};
		
		bool all_behind = d[0] < 0.0f && d[1] < 0.0f && d[2] < 0.0f;
		if (all_behind)
		{
			triangles[i] = false;
			continue;
		}
		if (tryClip2Vertices(vertices, index, d, 0, 1, 2)) continue;
		if (tryClip2Vertices(vertices, index, d, 0, 2, 1)) continue;
		if (tryClip2Vertices(vertices, index, d, 1, 2, 0)) continue;
		if (tryClip1Vertex(vertices, index, d, 0, 1, 2, triangles, triangles_count)) continue;
		if (tryClip1Vertex(vertices, index, d, 1, 2, 0, triangles, triangles_count)) continue;
		if (tryClip1Vertex(vertices, index, d, 2, 0, 1, triangles, triangles_count)) continue;
	}
}


LUMIX_FORCE_INLINE void rasterizeOccludingTriangle(Vec4 (&vertices)[64 * 3], int* depth)
{
	enum ClipMask
	{
		NEGATIVE_X = 1 << 0,
		POSITIVE_X = 1 << 1,
		NEGATIVE_Y = 1 << 2,
		POSITIVE_Y = 1 << 3,
		NEGATIVE_Z = 1 << 4,
		POSITIVE_Z = 1 << 5
	};
	u32 triangle_mask = 0;
	u32 and_mask = 0;
	for (int i = 0; i < 3; ++i)
	{
		Vec4 v = vertices[i];
		u32 vertex_mask = 0;
		if (v.x < -v.w) vertex_mask |= NEGATIVE_X;
		if (v.y < -v.w) vertex_mask |= NEGATIVE_Y;
		if (v.z < -v.w) vertex_mask |= NEGATIVE_Z;

		if (v.x > v.w) vertex_mask |= POSITIVE_X;
		if (v.y > v.w) vertex_mask |= POSITIVE_Y;
		if (v.z > v.w) vertex_mask |= POSITIVE_Z;

		triangle_mask |= vertex_mask;
		if (i == 0) and_mask = vertex_mask;
		else and_mask &= vertex_mask;
	}

	bool all_vertices_outside = and_mask != 0;
	if (all_vertices_outside) return;

	if (triangle_mask == 0)
	{
		Vec3 projected[] = { toViewport(vertices[0]), toViewport(vertices[1]), toViewport(vertices[2]) };
		rasterizeProjectedTriangle(projected, depth);
	}
	else
	{
		int triangles_count = 1;
		bool triangles[64];
		if (triangle_mask & POSITIVE_X) clipTriangles(Vec4(-1.0f, 0.0f, 0.0f, 1.0f), vertices, triangles, triangles_count);
		if (triangle_mask & NEGATIVE_X) clipTriangles(Vec4(1.0f, 0.0f, 0.0f, 1.0f), vertices, triangles, triangles_count);
		if (triangle_mask & POSITIVE_Y) clipTriangles(Vec4(0.0f, -1.0f, 0.0f, 1.0f), vertices, triangles, triangles_count);
		if (triangle_mask & NEGATIVE_Y) clipTriangles(Vec4(0.0f, 1.0f, 0.0f, 1.0f), vertices, triangles, triangles_count);
		if (triangle_mask & POSITIVE_Z) clipTriangles(Vec4(0.0f, 0.0f, -1.0f, 1.0f), vertices, triangles, triangles_count);
		bool is_homogenous_depth = gpu::isHomogenousDepth();
		if (triangle_mask & NEGATIVE_Z) clipTriangles(Vec4(0.0f, 0.0f, 1.0f, is_homogenous_depth ? 1.0f : 0.0f), vertices, triangles, triangles_count);

		for (int i = 0; i < triangles_count; ++i)
		{
			if (!triangles[i]) continue;
			int index = i * 3;
			Vec3 projected[] = { toViewport(vertices[index]), toViewport(vertices[index + 1]), toViewport(vertices[index + 2]) };
			rasterizeProjectedTriangle(projected, depth);
		}
	}
}


template <typename IndexType>
static void rasterizeOccludingTriangles(const Mesh* mesh, const Matrix& mvp_mtx, int* depth)
{
	const Vec3* LUMIX_RESTRICT vertices = &mesh->vertices[0];
	const IndexType* LUMIX_RESTRICT indices = (const IndexType*)&mesh->indices[0];
	for (int i = 0, n = mesh->indices.size() / sizeof(IndexType); i < n; i += 3)
	{
		Vec4 v[64*3] = {
			mvp_mtx * Vec4(vertices[indices[i + 0]], 1),
			mvp_mtx * Vec4(vertices[indices[i + 1]], 1),
			mvp_mtx * Vec4(vertices[indices[i + 2]], 1)
		};
		rasterizeOccludingTriangle(v, depth);
	}
}


void OcclusionBuffer::rasterize(Universe* universe, const Array<MeshInstance>& meshes)
{
	PROFILE_FUNCTION();
	if (m_mips.empty()) init();
	int* depth = &m_mips[0][0];
	for (const MeshInstance& mesh_instance : meshes) {
		const Mesh* mesh = mesh_instance.mesh;
		const Matrix mtx = m_view_projection_matrix * universe->getRelativeMatrix(mesh_instance.owner, m_camera_pos);
		if (mesh->flags.isSet(Mesh::INDICES_16_BIT)) {
			rasterizeOccludingTriangles<u16>(mesh, mtx, depth);
		}
		else {
			rasterizeOccludingTriangles<u32>(mesh, mtx, depth);
		}
	}
}


void OcclusionBuffer::clear()
{
	PROFILE_FUNCTION();
	for (auto& mip : m_mips)
	{
		for (int& i : mip)
		{
			i = Z_SCALE;
		}
	}
}


} // namespace Lumix