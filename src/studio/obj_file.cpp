#include "obj_file.h"
#include "debug/floating_points.h"
#include "core/string.h"
#include "graphics/model.h"
#include <QDir>
#include <qfile.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qimagewriter.h>


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


static const char* skipWhitespace(const char* value)
{
	while (*value && (*value == ' ' || *value == '\t'))
		++value;
	return value;
}


static OBJFile::TexCoord parseTexCoord(const char* tmp)
{
	OBJFile::TexCoord v;
	const char* next = skipWhitespace(tmp);
	v.x = atof(next);
	next = strstr(next, " ") + 1;
	v.y = atof(next);
	return v;
}


static Lumix::Vec3 parseVec3(const char* tmp)
{
	Lumix::Vec3 v;
	const char* next = skipWhitespace(tmp);
	v.x = atof(next);
	next = strstr(next, " ") + 1;
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


void OBJFile::pushTriangle(const OBJFile::Triangle& input)
{
	OBJFile::Triangle triangle = input;
	int dp = -1;
	int dn = -1;
	int dt = -1;
	if (triangle.i[0].position < 0)
	{
		dp = m_positions.size();
		dn = m_normals.size();
		dt = m_tex_coords.size();
	}
	triangle.i[0].position += dp;
	triangle.i[1].position += dp;
	triangle.i[2].position += dp;

	triangle.i[0].normal += dn;
	triangle.i[1].normal += dn;
	triangle.i[2].normal += dn;

	triangle.i[0].tex_coord += dt;
	triangle.i[1].tex_coord += dt;
	triangle.i[2].tex_coord += dt;

	m_triangles.push_back(triangle);
}


void OBJFile::parseTriangle(const char* tmp, int length)
{
	OBJFile::Triangle v;
	const char* next = skipWhitespace(tmp);
	for (int i = 0; i < 3; ++i)
	{
		next = Lumix::fromCString(next, length - (next - tmp), &v.i[i].position);
		++next;
		next = Lumix::fromCString(next, length - (next - tmp), &v.i[i].tex_coord);
		if (*next == '/')
		{
			++next;
			next = Lumix::fromCString(next, length - (next - tmp), &v.i[i].normal);
		}
		else
		{
			v.i[i].normal = 1;
		}
		++next;
	}
	pushTriangle(v);
	if ((*next >= '0' && *next <= '9') || *next == '-')
	{
		v.i[1] = v.i[2];

		next = Lumix::fromCString(next, length - (next - tmp), &v.i[2].position);
		++next;
		next = Lumix::fromCString(next, length - (next - tmp), &v.i[2].tex_coord);
		if (*next == '/')
		{
			++next;
			next = Lumix::fromCString(next, length - (next - tmp), &v.i[2].normal);
		}
		else
		{
			v.i[2].normal = 1;
		}
		++next;
		pushTriangle(v);
	}
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
	Mesh* last_mesh = nullptr;
	while (!file.atEnd())
	{
		char tmp[256];
		file.readLine(tmp, sizeof(tmp));
		if (tmp[0] == 'o' && tmp[1] == ' ')
		{
			m_object_name = tmp + 2;
		}
		else if (tmp[0] == 'm' && strncmp(tmp, "mtllib", strlen("mtllib")) == 0)
		{
			m_material_library = QString(skipWhitespace(tmp + 7)).trimmed();
		}
		else if (tmp[0] == 'u' && strncmp(tmp, "usemtl", strlen("usemtl")) == 0)
		{
			QString material = tmp + 7;
			m_meshes.push_back(Mesh());
			last_mesh = &m_meshes[m_meshes.size() - 1];
			last_mesh->m_material = material.trimmed();
			last_mesh->m_index_from = m_triangles.size() * 3;
			last_mesh->m_index_count = 0;
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
			parseTriangle(tmp + 2, sizeof(tmp) - 2);
			last_mesh->m_index_count = 3 * m_triangles.size() - last_mesh->m_index_from;
		}
	}
	calculateTangents();
	qSort(m_meshes.begin(), m_meshes.end(), [](const Mesh& a, const Mesh& b) { return a.m_material < b.m_material; });
	return loadMaterialLibrary(path);
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
	int32_t mesh_count = 0;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		if (i == 0 || m_meshes[i].m_material != m_meshes[i - 1].m_material)
		{
			++mesh_count;
		}
	}

	file.write((const char*)&mesh_count, sizeof(mesh_count));
	int32_t attribute_array_offset = 0;
	int32_t indices_offset = 0;
	int i = 0;
	while (i < m_meshes.size())
	{
		const Mesh& mesh = m_meshes[i];
		auto material_name = mesh.m_material.toLatin1();
		int32_t length = strlen(material_name.data());
		file.write((const char*)&length, sizeof(length));
		file.write((const char*)material_name.data(), length);

		file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
		int32_t attribute_array_size = 0;
		while (i < m_meshes.size() && m_meshes[i].m_material == mesh.m_material)
		{
			attribute_array_size += m_meshes[i].m_index_count * VERTEX_SIZE;
			++i;
		}
		attribute_array_offset += attribute_array_size;
		file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));

		file.write((const char*)&indices_offset, sizeof(indices_offset));
		int32_t mesh_tri_count = attribute_array_size / VERTEX_SIZE / 3;
		indices_offset += mesh_tri_count * 3;
		file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

		auto mesh_name = mesh.m_material.toLatin1();
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
}


void OBJFile::writeGeometry(QFile& file)
{
	int32_t indices_count = m_triangles.size() * 3;
	file.write((const char*)&indices_count, sizeof(indices_count));
	int32_t polygon_idx = 0;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		const Mesh& mesh = m_meshes[i];
		if (i > 0 && m_meshes[i].m_material != m_meshes[i - 1].m_material)
		{
			polygon_idx = 0;
		}
		for (int i = 0; i < mesh.m_index_count; ++i, ++polygon_idx)
		{
			file.write((const char*)&polygon_idx, sizeof(polygon_idx));
		}
	}

	int32_t vertices_size = m_triangles.size() * 3 * VERTEX_SIZE;
	file.write((const char*)&vertices_size, sizeof(vertices_size));

	for (auto mesh : m_meshes)
	{
		for (int i = 0; i < mesh.m_index_count / 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				int tri_index = mesh.m_index_from / 3 + i;
				file.write((const char*)&m_positions[m_triangles[tri_index].i[j].position], sizeof(m_positions[0]));

				auto normal = m_normals[m_triangles[tri_index].i[j].normal];
				uint8_t byte_normal[4] = { (int8_t)(normal.x * 127), (int8_t)(normal.z * 127), (int8_t)(normal.y * 127), 0 };
				file.write((const char*)byte_normal, sizeof(byte_normal));

				auto tangent = m_tangents[tri_index * 3 + j];
				uint8_t byte_tangent[4] = { (int8_t)(tangent.x * 127), (int8_t)(tangent.z * 127), (int8_t)(tangent.y * 127), 0 };
				file.write((const char*)byte_tangent, sizeof(byte_tangent));

				auto uv = m_tex_coords[m_triangles[tri_index].i[j].tex_coord];
				short short_uv[2] = { (short)(uv.x * 2048), (short)(uv.y * 2048) };
				file.write((const char*)short_uv, sizeof(short_uv));
			}
		}
	}
}


void OBJFile::calculateNormals()
{
	for (int i = 0, c = m_triangles.size(); i < c; ++i)
	{
		Triangle& t = m_triangles[i];
		Lumix::Vec3 p[] = {
			m_positions[t.i[0].position],
			m_positions[t.i[1].position],
			m_positions[t.i[2].position]
		};
		Lumix::Vec3 n = Lumix::crossProduct(p[2] - p[0], p[2] - p[1]);
		t.i[0].normal = t.i[1].normal = t.i[2].normal = m_normals.size();
		float squared_length = n.squaredLength();
		if (squared_length > FLT_EPSILON)
		{
			m_normals.push_back(n / sqrt(squared_length));
		}
		else
		{
			Lumix::Vec3 sdir = calculateSDir(t);
			m_normals.push_back((fabs(sdir.x) > 0 ? Lumix::Vec3(-sdir.y, sdir.x, 0) : Lumix::Vec3(0, sdir.z, -sdir.y)).normalized());
		}
	}
}


Lumix::Vec3 OBJFile::calculateSDir(Triangle& triangle) const
{
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

	float inv_r = (s1 * t2 - s2 * t1);
	if (inv_r != 0)
	{
		float r = 1.0f / inv_r;
		return Lumix::Vec3((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
	}
	return Lumix::Vec3(1, 0, 0);
}


void OBJFile::calculateTangents()
{
	Lumix::enableFloatingPointTraps(false);
	if (m_normals.empty())
	{
		calculateNormals();
	}
	m_tangents.clear();
	int vertex_count = m_triangles.size() * 3;
	m_tangents.reserve(vertex_count);

    for (long a = 0; a < m_triangles.size(); a++)
    {
		auto& triangle = m_triangles[a];
		triangle.sdir = calculateSDir(triangle);
    }
    
    for (long a = 0; a < m_triangles.size(); a++)
    {
		const Lumix::Vec3& t = m_triangles[a].sdir;
		const Lumix::Vec3& n3 = m_normals[m_triangles[a].i[0].normal];
		auto safeNormalize = [](const Lumix::Vec3& v) {
			float l = v.squaredLength();
			if (l < FLT_EPSILON)
			{
				return Lumix::Vec3(1, 0, 0);
			}
			return v / sqrt(l);
		};

		m_tangents.push_back(safeNormalize(t - n3 * Lumix::dotProduct(n3, t)));
		const Lumix::Vec3& n2 = m_normals[m_triangles[a].i[1].normal];
		m_tangents.push_back(safeNormalize(t - n2 * Lumix::dotProduct(n2, t)));
		const Lumix::Vec3& n = m_normals[m_triangles[a].i[2].normal];
		m_tangents.push_back(safeNormalize(t - n * Lumix::dotProduct(n, t)));

        // Calculate handedness
//        tangent[a].w = (Dot(Cross(n, t), tan2[a]) < 0.0F) ? -1.0F : 1.0F;
    }
	Lumix::enableFloatingPointTraps(true);
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
	int32_t to_mesh = -1;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		if (i == 0 || m_meshes[i].m_material != m_meshes[i - 1].m_material)
		{
			++to_mesh;
		}
	}
	file.write((const char*)&to_mesh, sizeof(to_mesh));
	float distance = FLT_MAX;
	file.write((const char*)&distance, sizeof(distance));

	file.close();
	return true;
}


bool OBJFile::saveLumixMaterials(const QString& path, bool convertToDDS)
{
	QFileInfo info(path);
	bool success = true;
	for (int i = 0; i < m_materials.size(); ++i)
	{
		const Material& material = m_materials[i];
		QFile file(info.dir().path() + "/" + material.m_name + ".mat");
		if (file.open(QIODevice::WriteOnly))
		{
			QFileInfo texture_info(m_material_library_dir + "/" + material.m_texture);
			file.write(
				QString(
					"{	\"texture\" : { \"source\" : \"%1\" }, \"shader\" : \"shaders/rigid.shd\" }"
				)
				.arg(convertToDDS ? texture_info.baseName() + ".dds" :  material.m_texture)
				.toLatin1().data()
				);
			file.close();
			auto s = info.dir().path();
			QFile::remove(info.dir().path() + "/" + material.m_texture);
			
			if (convertToDDS && texture_info.suffix() != "dds")
			{
				QImage img(texture_info.absoluteFilePath());
				QImageWriter writer(info.dir().path() + "/" + texture_info.baseName() + ".dds");
				if (!writer.write(img.mirrored()))
				{
					success = false;
				}
			}
			else
			{
				if (!material.m_texture.isEmpty() && !QFile::copy(m_material_library_dir + "/" + material.m_texture, info.dir().path() + "/" + material.m_texture))
				{
					success = false;
				}
			}
		}
		else
		{
			success = false;
		}
	}
	return success;
}


bool OBJFile::loadMaterialLibrary(const QString& model_path)
{
	if (m_material_library.isEmpty())
	{
		return true;
	}
	QFileInfo info(model_path);
	m_material_library_dir = info.dir().path();
	QFile file(info.dir().path() + "/" + m_material_library);
	if (!file.open(QFileDevice::ReadOnly))
	{
		return false;
	}

	char line[256];
	Material* material = nullptr;
	while (!file.atEnd())
	{
		file.readLine(line, sizeof(line));
		const char* next = skipWhitespace(line);
		if (next[0] == 'n' && strncmp(next, "newmtl", strlen("newmtl")) == 0)
		{
			m_materials.push_back(Material());
			material = &m_materials.back();
			material->m_name = QString(skipWhitespace(next + 7)).trimmed();
		}
		else if (next[0] == 'K' && next[1] == 'a' && next[2] == ' ')
		{
			material->m_ambient_color = parseVec3(next + 3);
		}
		else if (next[0] == 'K' && next[1] == 'd' && next[2] == ' ')
		{
			material->m_diffuse_color = parseVec3(next + 3);
		}
		else if (next[0] == 'm' && strncmp(next, "map_Ka", strlen("map_Ka")) == 0)
		{
			material->m_texture = QString(skipWhitespace(next + 7)).trimmed();
		}
	}

	file.close();
	return true;
}

