#pragma once


#include "engine/array.h"


namespace Lumix
{


template <typename T> class Array;
struct IAllocator;
struct Matrix;
struct Mesh;
struct MeshInstance;
class Universe;


class OcclusionBuffer
{
public:
	OcclusionBuffer(IAllocator& allocator);

	void clear();
	void rasterize(Universe* universe, const Matrix& projection, const Array<MeshInstance>& meshes);
	void buildHierarchy();
	const int* getMip(int level) { return &m_mips[level][0]; }

private:
	void init();

	using Mip = Array<int>;

	IAllocator& m_allocator;
	Array<Mip> m_mips;
};


} // namespace Lumix
