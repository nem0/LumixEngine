#pragma once


#include "core/aabb.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/delegate_list.h"
#include "core/hash_map.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec3.h"
#include "core/resource.h"
#include "graphics/geometry.h"
#include "graphics/ray_cast_model_hit.h"


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

// triangles with one material
class Mesh
{
	public:
		Mesh(const VertexDef& def, Material* mat, int attribute_array_offset, int attribute_array_size, int indices_offset, int index_count, const char* name, IAllocator& allocator)
			: m_name(allocator)
			, m_vertex_def(def)
		{
			m_material = mat;
			m_attribute_array_offset = attribute_array_offset;
			m_attribute_array_size = attribute_array_size;
			m_indices_offset = indices_offset;
			m_index_count = index_count;
			m_name_hash = crc32(name);
			m_name = name;
		}

		Material* getMaterial() const { return m_material; }
		void setMaterial(Material* material) { m_material = material; }
		int getIndicesOffset() const { return m_indices_offset; }
		int getIndexCount() const { return m_index_count; }
		int getTriangleCount() const { return m_index_count / 3; }
		int getAttributeArrayOffset() const { return m_attribute_array_offset; }
		int getAttributeArraySize() const { return m_attribute_array_size; }
		uint32_t getNameHash() const { return m_name_hash; }
		const char* getName() const { return m_name.c_str(); }
		void setVertexDefinition(const VertexDef& def) { m_vertex_def = def; }
		const VertexDef& getVertexDefinition() const { return m_vertex_def; }

	private:
		Mesh(const Mesh&);
		void operator =(const Mesh&);

	private:
		VertexDef m_vertex_def;
		int32_t	m_attribute_array_offset;
		int32_t m_attribute_array_size;
		int32_t m_indices_offset;
		int32_t	m_index_count;
		uint32_t m_name_hash;
		Material* m_material;
		string m_name;
};


class LODMeshIndices
{
	public:
		LODMeshIndices(int from, int to) : m_from(from), m_to(to) {}

		int getFrom() const { return m_from; }
		int getTo() const { return m_to; }

	private:
		int m_from;
		int m_to;
};


// group of meshes
class Model : public Resource
{
	public:
		typedef HashMap<uint32_t, int> BoneMap;

		#pragma pack(1)
			class FileHeader
			{
				public:
					uint32_t m_magic;
					uint32_t m_version;
			};
		#pragma pack()

		enum class FileVersion : uint32_t
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
			{ }

			string name;
			string parent;
			Vec3 position;
			Quat rotation;
			Matrix inv_bind_matrix;
			int parent_idx;
		};

	public:
		Model(const Path& path, ResourceManager& resource_manager, IAllocator& allocator) 
			: Resource(path, resource_manager, allocator) 
			, m_bounding_radius()
			, m_allocator(allocator)
			, m_bone_map(m_allocator)
			, m_meshes(m_allocator)
			, m_bones(m_allocator)
			, m_indices(m_allocator)
			, m_vertices(m_allocator)
			, m_lods(m_allocator)
		{ }

		~Model();

		LODMeshIndices getLODMeshIndices(float squared_distance) const;
		const Geometry& getGeometry() const { return m_geometry_buffer_object; }
		Mesh&		getMesh(int index) { return m_meshes[index]; }
		const Mesh&	getMesh(int index) const { return m_meshes[index]; }
		const Mesh* getMeshPtr(int index) const { return &m_meshes[index]; }
		int			getMeshCount() const { return m_meshes.size(); }
		int			getBoneCount() const	{ return m_bones.size(); }
		const Bone&	getBone(int i) const		{ return m_bones[i]; }
		int			getFirstNonrootBoneIndex() const { return m_first_nonroot_bone_index; }
		BoneMap::iterator	getBoneIndex(uint32_t hash) { return m_bone_map.find(hash); }
		void		getPose(Pose& pose);
		float		getBoundingRadius() const { return m_bounding_radius; }
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform, float scale);
		const AABB& getAABB() const { return m_aabb; } 

	public:
		static const uint32_t FILE_MAGIC = 0x5f4c4d4f; // == '_LMO'

	private:
		bool parseVertexDef(FS::IFile* file, VertexDef* vertex_definition);
		bool parseGeometry(FS::IFile* file);
		bool parseBones(FS::IFile* file);
		bool parseMeshes(FS::IFile* file);
		bool parseLODs(FS::IFile* file);
		int getBoneIdx(const char* name);

		virtual void doUnload(void) override;
		virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override;

	private:
		IAllocator& m_allocator;
		Geometry m_geometry_buffer_object;
		Array<Mesh> m_meshes;
		Array<Bone> m_bones;
		Array<int32_t> m_indices;
		Array<Vec3> m_vertices;
		Array<LOD> m_lods;
		float m_bounding_radius;
		BoneMap m_bone_map; // maps bone name hash to bone index in m_bones
		AABB m_aabb;
		int m_first_nonroot_bone_index;
};


} // ~namespace Lumix
