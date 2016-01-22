#pragma once


#include "core/aabb.h"
#include "core/array.h"
#include "core/hash_map.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec.h"
#include "core/resource.h"
#include "renderer/ray_cast_model_hit.h"
#include <bgfx/bgfx.h>


namespace Lumix
{

class Frustum;
class Material;
class Model;
class Pose;
class ResourceManager;


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
	Material* getMaterial() const { return m_material; }
	void setMaterial(Material* material) { m_material = material; }
	int getIndicesOffset() const { return m_indices_offset; }
	int getIndexCount() const { return m_index_count; }
	int getTriangleCount() const { return m_index_count / 3; }
	int getAttributeArrayOffset() const { return m_attribute_array_offset; }
	int getAttributeArraySize() const { return m_attribute_array_size; }
	uint32 getNameHash() const { return m_name_hash; }
	const char* getName() const { return m_name.c_str(); }
	void setVertexDefinition(const bgfx::VertexDecl& def) { m_vertex_def = def; }
	const bgfx::VertexDecl& getVertexDefinition() const { return m_vertex_def; }
	int getInstanceIdx() const { return m_instance_idx; }
	void setInstanceIdx(int value) { m_instance_idx = value; }

private:
	Mesh(const Mesh&);
	void operator=(const Mesh&);

private:
	bgfx::VertexDecl m_vertex_def;
	int32 m_instance_idx;
	int32 m_attribute_array_offset;
	int32 m_attribute_array_size;
	int32 m_indices_offset;
	int32 m_index_count;
	uint32 m_name_hash;
	Material* m_material;
	string m_name;
};


class LUMIX_RENDERER_API LODMeshIndices
{
public:
	LODMeshIndices(int from, int to)
		: m_from(from)
		, m_to(to)
	{
	}

	int getFrom() const { return m_from; }
	int getTo() const { return m_to; }

private:
	int m_from;
	int m_to;
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

	class LOD
	{
	public:
		int m_from_mesh;
		int m_to_mesh;

		float m_distance;
	};

	struct Bone
	{
		Bone(IAllocator& allocator)
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
	Array<LOD>& getLODs() { return m_lods; }

public:
	static const uint32 FILE_MAGIC = 0x5f4c4d4f; // == '_LMO'

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
	Array<LOD> m_lods;
	float m_bounding_radius;
	BoneMap m_bone_map;
	AABB m_aabb;
	int m_first_nonroot_bone_index;
};


} // ~namespace Lumix
