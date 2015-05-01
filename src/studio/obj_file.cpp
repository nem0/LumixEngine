#include "obj_file.h"
#include "core/string.h"
#include "graphics/model.h"
#include <qfile.h>


enum class VertexAttributeDef : uint32_t
{
	POSITION,
	FLOAT1,
	FLOAT2,
	FLOAT3,
	FLOAT4,
	INT1,
	INT2,
	INT3,
	INT4,
	SHORT2,
	SHORT4,
	BYTE4,
	NONE
};



static const int VERTEX_SIZE = 24;


static OBJFile::TexCoord parseTexCoord(const char* tmp)
{
	OBJFile::TexCoord v;
	v.x = atof(tmp);
	const char* next = strstr(tmp, " ") + 1;
	v.y = atof(next);
	return v;
}


static Lumix::Vec3 parseVec3(const char* tmp)
{
	Lumix::Vec3 v;
	v.x = atof(tmp);
	const char* next = strstr(tmp, " ") + 1;
	v.y = atof(next);
	next = strstr(next, " ") + 1;
	v.z = atof(next);
	return v;
}


struct IndicesParsed
{
	OBJFile::Indices indices;
	const char* end;
};


static OBJFile::Triangle parseTriangle(const char* tmp, int length)
{
	OBJFile::Triangle v;
	const char* next = tmp;
	for (int i = 0; i < 3; ++i)
	{
		Lumix::fromCString(next, length - (next - tmp), &v.i[i].position);
		--v.i[i].position;
		next = strstr(next, "/") + 1;
		Lumix::fromCString(next, length - (next - tmp), &v.i[i].tex_coord);
		--v.i[i].tex_coord;
		next = strstr(next, "/") + 1;
		Lumix::fromCString(next, length - (next - tmp), &v.i[i].normal);
		--v.i[i].normal;
		next = strstr(next, " ") + 1;
	}
	return v;
}


bool OBJFile::load(const QString& path)
{
	if (path.isEmpty())
		return false;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		return false;
	}
	while (!file.atEnd())
	{
		char tmp[256];
		file.readLine(tmp, sizeof(tmp));
		if (tmp[0] == 'o' && tmp[1] == ' ')
		{
			m_mesh_name = tmp + 2;
		}
		else if (tmp[0] == 'u' && strncmp(tmp, "usemtl", strlen("usemtl")) == 0)
		{
			m_material_name = tmp + 7;
			m_material_name = m_material_name.mid(0, m_material_name.length() - 1);
		}
		else if (tmp[0] == 'v')
		{
			if (tmp[1] == ' ')
			{
				auto v = parseVec3(tmp + 2);
				m_positions.push_back(v);
			}
			else if (tmp[1] == 'n' && tmp[2] == ' ')
			{
				auto v = parseVec3(tmp + 3);
				m_normals.push_back(v);
			}
			else if (tmp[1] == 't' && tmp[2] == ' ')
			{
				auto t = parseTexCoord(tmp + 3);
				m_tex_coords.push_back(t);
			}
		}
		else if (tmp[0] == 'f' && tmp[1] == ' ')
		{ 
			m_triangles.push_back(parseTriangle(tmp + 2, sizeof(tmp) - 2));
		}
	}
	return true;
}


void writeAttribute(const char* attribute_name, VertexAttributeDef attribute_type, QFile& file)
{
	uint32_t length = strlen(attribute_name);
	file.write((const char*)&length, sizeof(length));
	file.write(attribute_name, length);

	uint32_t type = (uint32_t)attribute_type;
	file.write((const char*)&type, sizeof(type));
}


void OBJFile::writeMeshes(QFile& file)
{
	int32_t mesh_count = 1;
	file.write((const char*)&mesh_count, sizeof(mesh_count));

	auto material_name = m_material_name.toLatin1();
	int32_t length = strlen(material_name.data());
	file.write((const char*)&length, sizeof(length));
	file.write((const char*)material_name.data(), length);

	int32_t attribute_array_offset = 0;
	file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
	int32_t attribute_array_size = m_triangles.size() * 3 * VERTEX_SIZE;

	file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));
	int32_t indices_offset = 0;
	file.write((const char*)&indices_offset, sizeof(indices_offset));
	int32_t mesh_tri_count = m_triangles.size();
	file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

	auto mesh_name = m_mesh_name.toLatin1();
	length = strlen(mesh_name.data());
	file.write((const char*)&length, sizeof(length));
	file.write((const char*)mesh_name.data(), length);

	int32_t attribute_count = 4;
	file.write((const char*)&attribute_count, sizeof(attribute_count));

	writeAttribute("in_position", VertexAttributeDef::POSITION, file);
	writeAttribute("in_normal", VertexAttributeDef::BYTE4, file);
	writeAttribute("in_tangents", VertexAttributeDef::BYTE4, file);
	writeAttribute("in_tex_coords", VertexAttributeDef::SHORT2, file);

}


void OBJFile::writeGeometry(QFile& file)
{
	calculateTangents();

	int32_t indices_count = m_triangles.size() * 3;
	file.write((const char*)&indices_count, sizeof(indices_count));
	for (int polygon_idx = 0; polygon_idx < m_triangles.size(); ++polygon_idx)
	{
		for (int triangle_vertex_idx = 0; triangle_vertex_idx < 3; ++triangle_vertex_idx)
		{
			int32_t index = polygon_idx * 3 + triangle_vertex_idx;
			file.write((const char*)&index, sizeof(index));
		}
	}

	int32_t vertices_size = m_triangles.size() * 3 * VERTEX_SIZE;
	file.write((const char*)&vertices_size, sizeof(vertices_size));

	for (int i = 0, c = m_triangles.size(); i < c; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			file.write((const char*)&m_positions[m_triangles[i].i[j].position], sizeof(m_positions[0]));
			
			auto normal = m_normals[m_triangles[i].i[j].normal];
			uint8_t byte_normal[4] = { (int8_t)(normal.x * 127), (int8_t)(normal.z * 127), (int8_t)(normal.y * 127), 0 };
			file.write((const char*)byte_normal, sizeof(byte_normal));

			auto tangent = m_tangents[i * 3 + j];
			uint8_t byte_tangent[4] = { (int8_t)(tangent.x * 127), (int8_t)(tangent.z * 127), (int8_t)(tangent.y * 127), 0 };
			file.write((const char*)byte_tangent, sizeof(byte_tangent));

			auto uv = m_tex_coords[m_triangles[i].i[j].tex_coord];
			short short_uv[2] = { (short)(uv.x * 2048), (short)(uv.y * 2048) };
			file.write((const char*)short_uv, sizeof(short_uv));
		}
	}
}


void OBJFile::calculateTangents()
{
	m_tangents.clear();
	int vertex_count = m_triangles.size() * 3;
	m_tangents.reserve(vertex_count);

    for (long a = 0; a < m_triangles.size(); a++)
    {
		auto& triangle = m_triangles[a];
       
		const Lumix::Vec3& v1 = m_positions[triangle.i[0].position];
		const Lumix::Vec3& v2 = m_positions[triangle.i[1].position];
		const Lumix::Vec3& v3 = m_positions[triangle.i[2].position];
        
		const TexCoord& w1 = m_tex_coords[triangle.i[0].tex_coord];
		const TexCoord& w2 = m_tex_coords[triangle.i[1].tex_coord];
		const TexCoord& w3 = m_tex_coords[triangle.i[2].tex_coord];
        
        float x1 = v2.x - v1.x;
        float x2 = v3.x - v1.x;
        float y1 = v2.y - v1.y;
        float y2 = v3.y - v1.y;
        float z1 = v2.z - v1.z;
        float z2 = v3.z - v1.z;
        
        float s1 = w2.x - w1.x;
        float s2 = w3.x - w1.x;
        float t1 = w2.y - w1.y;
        float t2 = w3.y - w1.y;
        
        float r = 1.0f / (s1 * t2 - s2 * t1);
		triangle.sdir = Lumix::Vec3((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
		//triangle.tdir = Lumix::Vec3((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);
    }
    
    for (long a = 0; a < m_triangles.size(); a++)
    {
		const Lumix::Vec3& t = m_triangles[a].sdir;
		const Lumix::Vec3& n3 = m_normals[m_triangles[a].i[0].normal];
		m_tangents.push_back((t - n3 * Lumix::dotProduct(n3, t)).normalized());
		const Lumix::Vec3& n2 = m_normals[m_triangles[a].i[1].normal];
		m_tangents.push_back((t - n2 * Lumix::dotProduct(n2, t)).normalized());
		const Lumix::Vec3& n = m_normals[m_triangles[a].i[2].normal];
		m_tangents.push_back((t - n * Lumix::dotProduct(n, t)).normalized());

        // Calculate handedness
//        tangent[a].w = (Dot(Cross(n, t), tan2[a]) < 0.0F) ? -1.0F : 1.0F;
    }
}


bool OBJFile::saveLumixMesh(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		return false;
	}
	Lumix::Model::FileHeader header;
	header.m_magic = Lumix::Model::FILE_MAGIC;
	header.m_version = (uint32_t)Lumix::Model::FileVersion::LATEST;

	file.write((const char*)&header, sizeof(header));
	
	writeMeshes(file);
	writeGeometry(file);

	int32_t bone_count = 0;
	file.write((const char*)&bone_count, sizeof(bone_count));

	int32_t lod_count = 1;
	file.write((const char*)&lod_count, sizeof(lod_count));
	int32_t to_mesh = 0;
	file.write((const char*)&to_mesh, sizeof(to_mesh));
	float distance = FLT_MAX;
	file.write((const char*)&distance, sizeof(distance));

	file.close();
	return true;
}
