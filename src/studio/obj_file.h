#pragma once


#include "core/vec3.h"
#include <qstring.h>
#include <qvector.h>


class QFile;


class OBJFile
{
	public:
		class TexCoord
		{
			public:
				float x, y;
		};

		class Indices
		{
			public:
				int position;
				int normal;
				int tex_coord;
		};

		class Triangle
		{
			public:
				Indices i[3];
				Lumix::Vec3 sdir;
		};

		class Mesh
		{
			public:
				QString m_material;
				int m_index_from;
				int m_index_count;
		};

		class Material
		{
			public:
				QString m_name;
				QString m_texture;
				Lumix::Vec3 m_ambient_color;
				Lumix::Vec3 m_diffuse_color;
		};

	public:
		bool load(const QString& path);
		bool saveLumixMesh(const QString& path);
		bool saveLumixMaterials(const QString& path, bool convertToDDS);
		int getMeshCount() const { return m_meshes.size(); }
		const QString& getMaterialName(int i) const { return m_meshes[i].m_material; }

	private:
		bool loadMaterialLibrary(const QString& model_path);
		void writeMeshes(QFile& file);
		void writeGeometry(QFile& file);
		void calculateTangents();
		void calculateNormals();
		Lumix::Vec3 calculateSDir(Triangle& triangle) const;
		void pushTriangle(const OBJFile::Triangle& triangle);
		void parseTriangle(const char* tmp, int length);

	public:
		QVector<Mesh> m_meshes;
		QString m_object_name;
		QString m_material_library;
		QString m_material_library_dir;
		QVector<Lumix::Vec3> m_positions;
		QVector<Lumix::Vec3> m_normals;
		QVector<Lumix::Vec3> m_tangents;
		QVector<Material> m_materials;
		QVector<TexCoord> m_tex_coords;
		QVector<Triangle> m_triangles;
};