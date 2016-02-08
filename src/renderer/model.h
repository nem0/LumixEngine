#pragma once


#include "core/aabb.h"
#include "core/array.h"
#include "core/hash_map.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec.h"
#include "core/resource.h"
#include <bgfx/bgfx.h>


namespace Lumix
{

class Frustum;
class Material;
class Model;
class Pose;
class ResourceManager;
struct RayCastModelHit;


namespace FS
{
class FileSystem;
class IFile;
}


class LUMIX_RENDERER_API Mesh
{
public:
	Mesh(const bgfx::VertexDecl& def,
		 Material* mat,
		 int attribute_array_offset,
		 int attribute_array_size,
		 int indices_offset,
		 int index_count,
		 const char* name,
		 IAllocator& allocator);
	void set(const bgfx::VertexDecl& def,
		int attribute_array_offset,
		int attribute_array_size,
		int indices_offset,
		int index_count);

	bgfx::VertexDecl vertex_def;
	int32 instance_idx;
	int32 attribute_array_offset;
	int32 attribute_array_size;
	int32 indices_offset;
	int32 indices_count;
	Material* material;
	string name;
};


struct LODMeshIndices
{
	int from;
	int to;
};


class LUMIX_RENDERER_API Model : public Resource
{
public:
	typedef HashMap<uint32, int> BoneMap;

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

	struct LOD
	{
		int from_mesh;
		int to_mesh;

		float distance;
	};

	struct Bone
	{
		explicit Bone(IAllocator& allocator)
			: name(allocator)
			, parent(allocator)
		{
		}

		string name;
		string parent;
		Vec3 position;
		Quat rotation;
		Matrix inv_bind_matrix;
		int parent_idx;
	};

public:
	Model(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Model();

	void create(const bgfx::VertexDecl& def,
		Material* material,
		const int* indices_data,
		int indices_size,
		const void* attributes_data,
		int attributes_size);

	LODMeshIndices getLODMeshIndices(float squared_distance) const;
	Mesh& getMesh(int index) { return m_meshes[index]; }
	bgfx::VertexBufferHandle getVerticesHandle() const { return m_vertices_handle; }
	bgfx::IndexBufferHandle getIndicesHandle() const { return m_indices_handle; }
	const Mesh& getMesh(int index) const { return m_meshes[index]; }
	const Mesh* getMeshPtr(int index) const { return &m_meshes[index]; }
	int getMeshCount() const { return m_meshes.size(); }
	int getBoneCount() const { return m_bones.size(); }
	const Bone& getBone(int i) const { return m_bones[i]; }
	int getFirstNonrootBoneIndex() const { return m_first_nonroot_bone_index; }
	BoneMap::iterator getBoneIndex(uint32 hash) { return m_bone_map.find(hash); }
	void getPose(Pose& pose);
	float getBoundingRadius() const { return m_bounding_radius; }
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform);
	const AABB& getAABB() const { return m_aabb; }
	LOD* getLODs() { return m_lods; }

public:
	static const uint32 FILE_MAGIC = 0x5f4c4d4f; // == '_LMO'
	static const int MAX_LOD_COUNT = 4;

private:
	Model(const Model&);
	void operator=(const Model&);

	bool parseVertexDef(FS::IFile& file, bgfx::VertexDecl* vertex_definition);
	bool parseGeometry(FS::IFile& file);
	bool parseBones(FS::IFile& file);
	bool parseMeshes(FS::IFile& file);
	bool parseLODs(FS::IFile& file);
	int getBoneIdx(const char* name);
	void computeRuntimeData(const uint8* vertices);

	void unload(void) override;
	bool load(FS::IFile& file) override;

private:
	IAllocator& m_allocator;
	bgfx::IndexBufferHandle m_indices_handle;
	bgfx::VertexBufferHandle m_vertices_handle;
	int m_indices_size;
	int m_vertices_size;
	Array<Mesh> m_meshes;
	Array<Bone> m_bones;
	Array<int32> m_indices;
	Array<Vec3> m_vertices;
	LOD m_lods[MAX_LOD_COUNT];
	float m_bounding_radius;
	BoneMap m_bone_map;
	AABB m_aabb;
	int m_first_nonroot_bone_index;
};


} // ~namespace Lumix
