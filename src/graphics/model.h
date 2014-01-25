#pragma once


#include "core/array.h"
#include "core/matrix.h"
#include "core/pod_array.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec3.h"


namespace Lux
{

class Geometry;
class Material;
class Model;
class Pose;
namespace FS
{
	class FileSystem;
	class IFile;
}

// triangles with one material
class Mesh
{
	public:
		Mesh(Material* mat, int start, int count)
		{
			m_material = mat;
			m_start = start;
			m_count = count;
		}

		Material* getMaterial() const { return m_material; }
		void setMaterial(Material* material) { m_material = material; }
		int getCount() const { return m_count; }
		int getStart() const { return m_start; }

	private:
		int	m_start;
		int	m_count;
		Material* m_material;
};


// group of meshes
class Model
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
		Model() { m_geometry = NULL; }
		~Model();

		void load(const char* path, FS::FileSystem& file_system);
		Geometry* getGeometry() const { return m_geometry; }
		Mesh& getMesh(int index) { return m_meshes[index]; }
		int getMeshCount() const { return m_meshes.size(); }
		int	getBoneCount() const  { return m_bones.size(); }
		Bone& getBone(int i) { return m_bones[i]; }
		void getPose(Pose& pose);

	private:
		void loaded(FS::IFile* file, bool success);

	private:
		Geometry* m_geometry;
		PODArray<Mesh> m_meshes;
		Array<Bone> m_bones;
};


} // ~namespace Lux
