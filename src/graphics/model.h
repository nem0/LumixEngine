#pragma once


#include "core/array.h"
#include "core/crc32.h"
#include "core/delegate_list.h"
#include "core/hash_map.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec3.h"
#include "core/resource.h"
#include "graphics/ray_cast_model_hit.h"


namespace Lumix
{

class Geometry;
class Material;
class Model;
class Pose;
class ResourceManager;
struct VertexDef;

namespace FS
{
	class FileSystem;
	class IFile;
}

// triangles with one material
class Mesh
{
	public:
		Mesh(Material* mat, int start, int count, const char* name)
		{
			m_material = mat;
			m_start = start;
			m_count = count;
			m_name_hash = crc32(name);
		}

		Material* getMaterial() const { return m_material; }
		void setMaterial(Material* material) { m_material = material; }
		int getCount() const { return m_count; }
		int getStart() const { return m_start; }
		uint32_t getNameHash() const { return m_name_hash; }

	private:
		int32_t	m_start;
		int32_t	m_count;
		uint32_t m_name_hash;
		Material* m_material;
};


// group of meshes
class Model : public Resource
{
	public:
		typedef HashMap<uint32_t, int> BoneMap;

		struct Bone
		{
			string name;
			string parent;
			Vec3 position;
			Quat rotation;
			Matrix inv_bind_matrix;
			int parent_idx;
		};

	public:
		Model(const Path& path, ResourceManager& resource_manager) 
			: Resource(path, resource_manager) 
			, m_geometry()
			, m_bounding_radius()
		{ }

		~Model();

		Geometry*	getGeometry() const		{ return m_geometry; }
		Mesh&		getMesh(int index) { return m_meshes[index]; }
		const Mesh&	getMesh(int index) const { return m_meshes[index]; }
		int			getMeshCount() const { return m_meshes.size(); }
		int			getBoneCount() const	{ return m_bones.size(); }
		const Bone&	getBone(int i) const		{ return m_bones[i]; }
		BoneMap::iterator	getBoneIndex(uint32_t hash) { return m_bone_map.find(hash); }
		void		getPose(Pose& pose);
		float		getBoundingRadius() const { return m_bounding_radius; }
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform, float scale);

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
		bool parseVertexDef(FS::IFile* file, VertexDef* vertex_definition);
		bool parseGeometry(FS::IFile* file, const VertexDef& vertex_definition);
		bool parseBones(FS::IFile* file);
		bool parseMeshes(FS::IFile* file);
		int getBoneIdx(const char* name);

		virtual void doUnload(void) override;
		virtual FS::ReadCallback getReadCallback() override;
		
	private:
		Geometry* m_geometry;
		Array<Mesh> m_meshes;
		Array<Bone> m_bones;
		float m_bounding_radius;
		BoneMap m_bone_map; // maps bone name hash to bone index in m_bones
};


} // ~namespace Lumix
