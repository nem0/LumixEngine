#pragma once


#include "engine/array.h"
#include "engine/matrix.h"


namespace Lumix
{


template <typename T> class Array;
struct IAllocator;
struct Mesh;
struct MeshInstance;
struct AABB;
class Universe;


class OcclusionBuffer
{
public:
	OcclusionBuffer(IAllocator& allocator);

	bool isOccluded(const Matrix& world_transform, const AABB& aabb);
	void clear();
	void setCamera(const Matrix& view, const Matrix& projection);
	void rasterize(Universe* universe, const Array<MeshInstance>& meshes);
	void buildHierarchy();
	const int* getMip(int level) { return &m_mips[level][0]; }

private:
	void init();

	using Mip = Array<int>;

	IAllocator& m_allocator;
	Array<Mip> m_mips;
	Matrix m_view_projection_matrix;
};


} // namespace Lumix
