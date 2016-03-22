#pragma once

#include "lumix.h"
#include "core/resource.h"
#include "core/geometry.h"


namespace Lumix
{
class Pose;
class RayCastModelHit;

class LUMIX_RENDERER_API Model : public Resource
{
public:
#pragma pack(1)
	class FileHeader
	{
	public:
		uint32 m_magic;
		uint32 m_version;
	};
#pragma pack()

	enum class FileVersion : uint32
	{
		FIRST,

		LATEST // keep this last
	};

public:
	Model(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Model();

	uint8* getData() const { return m_data; }
	float getBoundingRadius() const { return m_bounding_radius; }
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform);
	const AABB& getAABB() const { return m_aabb; }

	uint32 getSizeX() const { return m_sizeX; };
	uint32 getSizeY() const { return m_sizeY; };
	uint32 getSizeZ() const { return m_sizeZ; };

	inline void setVoxel(uint32 x, uint32 y, uint32 z, uint8 value);
	inline uint8 getVoxel(uint32 x, uint32 y, uint32 z) const;

public:
	static const uint32 FILE_MAGIC = 0x5f4c524d; // == '_LRM'

	static const float VOXEL_SIZE_X;
	static const float VOXEL_SIZE_Y;
	static const float VOXEL_SIZE_Z;

private:
	Model(const Model&) = delete;
	void operator=(const Model&) = delete;

	bool parseData(FS::IFile& file);

	void unload(void) override;
	bool load(FS::IFile& file) override;

private:
	Array<uint8> m_data;
	float m_bounding_radius;
	AABB m_aabb;

	uint32 m_sizeX;
	uint32 m_sizeY;
	uint32 m_sizeZ;
};


void Model::setVoxel(uint32 x, uint32 y, uint32 z, uint8 value)
{
	ASSERT(x < m_sizeX && y < m_sizeY && z < m_sizeZ);
	m_data[x * m_sizeX * m_sizeY + y * m_sizeY + z] = value;
}

uint8 Model::getVoxel(uint32 x, uint32 y, uint32 z) const
{
	ASSERT(x < m_sizeX && y < m_sizeY && z < m_sizeZ);
	return m_data[x * m_sizeX * m_sizeY + y * m_sizeY + z];
}


} // !namespace Lumix
