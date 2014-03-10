#pragma once


#include "core/array.h"
#include "core/crc32.h"
#include "core/delegate_list.h"
#include "core/matrix.h"
#include "core/pod_array.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec3.h"
#include "core/resource.h"
#include "graphics/ray_cast_model_hit.h"


namespace Lux
{

class Geometry;
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
		struct Bone
		{
			string name;
			string parent;
			Vec3 position;
			Quat rotation;
			Matrix inv_bind_matrix;
		};

	public:
		Model(const Path& path, ResourceManager& resource_manager) 
			: Resource(path, resource_manager) 
			, m_geometry()
			, m_bounding_radius()
		{ }

		~Model();

		Geometry*	getGeometry() const		{ return m_geometry; }
		Mesh&		getMesh(int index)		{ return m_meshes[index]; }
		int			getMeshCount() const	{ return m_meshes.size(); }
		int			getBoneCount() const	{ return m_bones.size(); }
		Bone&		getBone(int i)			{ return m_bones[i]; }
		void		getPose(Pose& pose);
		float		getBoundingRadius() const { return m_bounding_radius; }
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform);

	private:
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);

		virtual void doUnload(void) LUX_OVERRIDE;
		virtual void doReload(void) LUX_OVERRIDE;
		virtual FS::ReadCallback getReadCallback() LUX_OVERRIDE;

	private:
		Geometry* m_geometry;
		PODArray<Mesh> m_meshes;
		Array<Bone> m_bones;
		float m_bounding_radius;
//		DelegateList<void ()> m_on_loaded;

};


} // ~namespace Lux
