#pragma once


#include "engine/array.h"
#include "engine/math.h"


namespace Lumix
{


template <typename T> struct Array;
struct IAllocator;
struct Mesh;
struct MeshInstance;
struct AABB;
struct Universe;


struct OcclusionBuffer
{
public:
	OcclusionBuffer(IAllocator& allocator);

	bool isOccluded(const Transform& world_transform, const AABB& aabb);
	void clear();
	void setCamera(const DVec3& pos, const Quat& rot, const Matrix& projection);
	void rasterize(Universe* universe, const Array<MeshInstance>& meshes);
	void buildHierarchy();
	const int* getMip(int level) { return &m_mips[level][0]; }

private:
	void init();
	Vec3 transform(const Transform& world_transform, float x, float y, float z);

	using Mip = Array<int>;

	IAllocator& m_allocator;
	Array<Mip> m_mips;
	Matrix m_view_projection_matrix;
	DVec3 m_camera_pos;
};


} // namespace Lumix
