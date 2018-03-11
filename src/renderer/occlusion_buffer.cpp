#include "occlusion_buffer.h"
#include "engine/array.h"
#include "engine/matrix.h"
#include "engine/math_utils.h"
#include "engine/profiler.h"
#include "engine/universe/universe.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"


namespace Lumix
{


static const int Z_SCALE = 1 << 24;
static const int XY_SCALE = 1 << 16;
static const int WIDTH = 384;
static const int HEIGHT = 192;


OcclusionBuffer::OcclusionBuffer(IAllocator& allocator)
	: m_mips(allocator)
	, m_allocator(allocator)
{
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
				*mip = Math::maximum(prev_mip[0], prev_mip[1], prev_mip[prev_w], prev_mip[prev_w + 1]);
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

	if (v[0].y > v[2].y) Math::swap(v[0], v[2]);
	if (v[0].y > v[1].y) Math::swap(v[0], v[1]);
	if (v[1].y > v[2].y) Math::swap(v[1], v[2]);

	int width = WIDTH;
	int height = HEIGHT;

	struct Point { int x, y; };
	Point p0 = { int(v[0].x * width + 0.5f), int(v[0].y * height + 0.5f) };
	Point p1 = { int(v[1].x * width + 0.5f), int(v[1].y * height + 0.5f) };
	Point p2 = { int(v[2].x * width + 0.5f), int(v[2].y * height + 0.5f) };

	if (p0.y == p2.y) return;

	float xdz = n.x / n.z;
	float ydz = n.y / n.z;
	int xdz_int = int(xdz * Z_SCALE);

	if (p1.y != p0.y)
	{
		int dl = (p1.x - p0.x) * XY_SCALE / (p1.y - p0.y);
		int dr = (p2.x - p0.x) * XY_SCALE / (p2.y - p0.y);
		Vec3 left_p = dl > dr ? v[2] : v[1];
		int dz_left = int((left_p.z - v[0].z) * XY_SCALE / (left_p.y - v[0].y));
		int left = p0.x * XY_SCALE;
		int right = p0.x * XY_SCALE;
		int z_left = int(v[0].z * Z_SCALE);
		if (dl > dr) Math::swap(dl, dr);
		for (int y = p0.y; y <= p1.y; ++y)
		{
			int base = y * width + left / XY_SCALE;
			int z = z_left;
			for (int x = 0, n = right / XY_SCALE - left / XY_SCALE; x <= n; ++x)
			{
				if (z < depth[base + x]) depth[base + x] = z; // TODO
				depth[base + x] = 0xfff00FFF;
				z += xdz_int;
			}
			left += dl;
			right += dr;
			z_left += dz_left;
		}
	}
	
	if (p2.y == p1.y) return;

	int left = p2.x * XY_SCALE;
	int right = p2.x * XY_SCALE;
	int z_left = int(v[2].z * Z_SCALE);

	int dl = -(p1.x - p2.x) * XY_SCALE / (p1.y - p2.y);
	int dr = -(p0.x - p2.x) * XY_SCALE / (p0.y - p2.y);
	Vec3 left_p = dl > dr ? v[0] : v[1];
	int dz_left = -int((left_p.z - v[2].z) * XY_SCALE / (left_p.y - v[2].y));
	if (dl > dr) Math::swap(dl, dr);
	for (int y = p2.y; y > p1.y; --y)
	{
		int base = y * width + left / XY_SCALE;
		int z = z_left;
		for (int x = 0, n = right / XY_SCALE - left / XY_SCALE; x <= n; ++x)
		{
			if (z < depth[base + x]) depth[base + x] = z; // TODO
			depth[base + x] = 0xfff00FFF;
			z += xdz_int;
		}
		left += dl;
		right += dr;
		z_left += dz_left;
	}

}


LUMIX_FORCE_INLINE Vec3 toViewport(const Vec4& v)
{
	float inv = 0.5f / v.w;
	return { v.x * inv + 0.5f, v.y * inv + 0.5f, v.z * inv + 0.5f };
}


LUMIX_FORCE_INLINE void rasterizeOccludingTriangle(Vec4 (&vertices)[3], int* depth)
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
		//ASSERT(false);
		// TODO
	}
}


template <typename IndexType>
static void rasterizeOccludingTriangles(const Mesh* mesh, const Matrix& mvp_mtx, int* depth)
{
	const Vec3* LUMIX_RESTRICT vertices = &mesh->vertices[0];
	const IndexType* LUMIX_RESTRICT indices = (const IndexType*)&mesh->indices[0];
	for (int i = 0, n = mesh->indices.size() / sizeof(IndexType); i < n; i += 3)
	{
		Vec4 v[3] = {
			mvp_mtx * Vec4(vertices[indices[i + 0]], 1),
			mvp_mtx * Vec4(vertices[indices[i + 1]], 1),
			mvp_mtx * Vec4(vertices[indices[i + 2]], 1)
		};
		rasterizeOccludingTriangle(v, depth);
	}
}


void OcclusionBuffer::rasterize(Universe* universe, const Matrix& view_projection, const Array<MeshInstance>& meshes)
{
	PROFILE_FUNCTION();
	if (m_mips.empty()) init();
	int* depth = &m_mips[0][0];
	for (const MeshInstance& mesh_instance : meshes)
	{
		const Mesh* mesh = mesh_instance.mesh;
		Matrix mtx = view_projection * universe->getMatrix(mesh_instance.owner);
		if (mesh->flags.isSet(Mesh::INDICES_16_BIT))
		{
			rasterizeOccludingTriangles<u16>(mesh, mtx, depth);
		}
		else
		{
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