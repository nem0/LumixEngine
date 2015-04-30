#pragma once


#include "core/vec3.h"
#include <qlist.h>
#include <qstring.h>


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

	public:
		bool load(const QString& path);
		bool saveLumixMesh(const QString& path);

	private:
		void writeMeshes(QFile& file);
		void writeGeometry(QFile& file);
		void calculateTangents();

	public:
		QString m_material_name;
		QString m_mesh_name;
		QList<Lumix::Vec3> m_positions;
		QList<Lumix::Vec3> m_normals;
		QList<Lumix::Vec3> m_tangents;
		QList<TexCoord> m_tex_coords;
		QList<Triangle> m_triangles;
};