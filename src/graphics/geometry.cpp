#include "graphics/geometry.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "graphics/gl_ext.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"

namespace Lumix
{

	
static int getAttributeTypeSize(VertexAttributeDef type)
{
	switch (type)
	{
		case VertexAttributeDef::FLOAT4:
			return 4 * sizeof(GLfloat);
		case VertexAttributeDef::POSITION:
		case VertexAttributeDef::FLOAT3:
			return 3 * sizeof(GLfloat);
		case VertexAttributeDef::FLOAT2:
			return 2 * sizeof(GLfloat);
		case VertexAttributeDef::FLOAT1:
			return 1 * sizeof(GLfloat);
		case VertexAttributeDef::INT4:
			return 4 * sizeof(GLint);
		case VertexAttributeDef::INT3:
			return 3 * sizeof(GLint);
		case VertexAttributeDef::INT2:
			return 2 * sizeof(GLint);
		case VertexAttributeDef::INT1:
			return sizeof(GLint);
		case VertexAttributeDef::BYTE4:
			return 4 * sizeof(GLbyte);
		case VertexAttributeDef::SHORT4:
			return 4 * sizeof(GLshort);
		case VertexAttributeDef::SHORT2:
			return 2 * sizeof(GLshort);
		default:
			ASSERT(false);
			return -1;
	}
}


void VertexDef::addAttribute(Renderer& renderer, const char* name, VertexAttributeDef type)
{
	m_attributes[m_attribute_count].m_name_index = renderer.getAttributeNameIndex(name);
	m_attributes[m_attribute_count].m_type = type;
	++m_attribute_count;
	m_vertex_size += getAttributeTypeSize(type);
}


bool VertexDef::parse(Renderer& renderer, FS::IFile* file)
{
	uint32_t attribute_count;
	file->read(&attribute_count, sizeof(attribute_count));
	
	m_vertex_size = 0;
	for (uint32_t i = 0; i < attribute_count; ++i)
	{
		char tmp[50];
		uint32_t len;
		file->read(&len, sizeof(len));
		if (len > sizeof(tmp) - 1)
		{
			return false;
		}
		file->read(tmp, len);
		tmp[len] = '\0';

		m_attributes[i].m_name_index = renderer.getAttributeNameIndex(tmp);

		file->read(&m_attributes[i].m_type, sizeof(m_attributes[i].m_type));

		m_vertex_size += getAttributeTypeSize(m_attributes[i].m_type);
	}
	m_attribute_count = attribute_count;
	return true;

}


int VertexDef::getPositionOffset() const
{
	int offset = 0;
	for(int i = 0; i < m_attribute_count; ++i)
	{
		switch (m_attributes[i].m_type)
		{
			case VertexAttributeDef::FLOAT4:
				offset += 4 * sizeof(float);
				break;
			case VertexAttributeDef::POSITION:
				return offset;
				break;
			case VertexAttributeDef::FLOAT3:
				offset += 3 * sizeof(float);
				break;
			case VertexAttributeDef::FLOAT2:
				offset += 2 * sizeof(float);
				break;
			case VertexAttributeDef::FLOAT1:
				offset += 1 * sizeof(float);
				break;
			case VertexAttributeDef::INT4:
				offset += 4 * sizeof(int);
				break;
			case VertexAttributeDef::INT3:
				offset += 3 * sizeof(int);
				break;
			case VertexAttributeDef::INT2:
				offset += 2 * sizeof(int);
				break;
			case VertexAttributeDef::INT1:
				offset += 1 * sizeof(int);
				break;
			case VertexAttributeDef::BYTE4:
				offset += 4 * sizeof(char);
				break;
			case VertexAttributeDef::SHORT4:
				offset += 4 * sizeof(short);
				break;
			case VertexAttributeDef::SHORT2:
				offset += 2 * sizeof(short);
				break;
			default:
				ASSERT(false);
				break;
		}
	}
	return -1;
}


void VertexDef::begin(Shader& shader, int start_offset) const 
{
	PROFILE_FUNCTION();
	int offset = start_offset;
	int attribute_count = Math::minValue(m_attribute_count, shader.getAttributeCount());
	for(int i = 0; i < attribute_count; ++i)
	{
		GLint attrib_id = shader.getAttribId(m_attributes[i].m_name_index);
		glVertexAttribDivisor(attrib_id, 0);
		switch (m_attributes[i].m_type)
		{
			case VertexAttributeDef::FLOAT4:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 4, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 4;
				break;
			case VertexAttributeDef::POSITION:
			case VertexAttributeDef::FLOAT3:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 3, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 3;
				break;
			case VertexAttributeDef::FLOAT2:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 2, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 2;
				break;
			case VertexAttributeDef::FLOAT1:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 1, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 1;
				break;
			case VertexAttributeDef::SHORT4:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 4, GL_SHORT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLshort) * 4;
				break;
			case VertexAttributeDef::SHORT2:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 2, GL_SHORT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLshort) * 2;
				break;
			case VertexAttributeDef::INT4:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 4, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 4;
				break;
			case VertexAttributeDef::INT3:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 3, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 3;
				break;
			case VertexAttributeDef::INT2:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 2, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 2;
				break;
			case VertexAttributeDef::INT1:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 1, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 1;
				break;
			case VertexAttributeDef::BYTE4:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 4, GL_BYTE, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(char) * 4;
				break;
			default:
				ASSERT(false);
				break;
		}
	}
	
}


void VertexDef::end(Shader& shader) const
{
	PROFILE_FUNCTION();
	for(int i = 0; i < m_attribute_count; ++i)
	{
		glDisableVertexAttribArray(shader.getAttribId(m_attributes[i].m_name_index));
	}
	
}


Geometry::Geometry()
{
	glGenBuffers(1, &m_attributes_array_id);
	glGenBuffers(1, &m_indices_array_id);
	m_indices_data_size = 0;
	m_attributes_data_size = 0;
}


Geometry::~Geometry()
{
	glDeleteBuffers(1, &m_attributes_array_id);
	glDeleteBuffers(1, &m_indices_array_id);
}


void Geometry::copy(const Geometry& source, int copy_count, IndexCallback index_callback, VertexCallback vertex_callback, IAllocator& allocator)
{
	ASSERT(source.m_indices_data_size > 0);
	ASSERT(m_indices_data_size == 0);

	glBindBuffer(GL_ARRAY_BUFFER, source.m_attributes_array_id);
	uint8_t* data = (uint8_t*)glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);
	Array<uint8_t> data_copy(allocator);
	data_copy.resize(source.m_attributes_data_size * copy_count);
	for (int i = 0; i < copy_count; ++i)
	{
		memcpy(&data_copy[i * source.m_attributes_data_size], data, source.m_attributes_data_size);
	}
	vertex_callback.invoke(&data_copy[0], source.m_attributes_data_size, copy_count);
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, source.m_indices_array_id);
	data = (uint8_t*)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_READ_ONLY);
	Array<uint8_t> indices_data_copy(allocator);
	indices_data_copy.resize(source.m_indices_data_size * copy_count);
	for (int i = 0; i < copy_count; ++i)
	{
		memcpy(&indices_data_copy[i * source.m_indices_data_size], data, source.m_indices_data_size);
	}
	index_callback.invoke(&indices_data_copy[0], source.m_indices_data_size, copy_count);
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glBindBuffer(GL_ARRAY_BUFFER, m_attributes_array_id);
	glBufferData(GL_ARRAY_BUFFER, data_copy.size() * sizeof(data_copy[0]), &data_copy[0], GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indices_array_id);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_data_copy.size() * sizeof(indices_data_copy[0]), &indices_data_copy[0], GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	m_indices_data_size = source.m_indices_data_size * copy_count;
	m_attributes_data_size = source.m_attributes_data_size * copy_count;
}


void Geometry::clear()
{
	glDeleteBuffers(1, &m_attributes_array_id);
	glDeleteBuffers(1, &m_indices_array_id);

	glGenBuffers(1, &m_attributes_array_id);
	glGenBuffers(1, &m_indices_array_id);

	m_indices_data_size = 0;
	m_attributes_data_size = 0;
}


void Geometry::setAttributesData(const void* data, int size)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_attributes_array_id);
	glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	m_attributes_data_size = size;
}


void Geometry::setIndicesData(const void* data, int size)
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indices_array_id);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	m_indices_data_size = size;
}


void Geometry::bindBuffers() const
{
	glBindBuffer(GL_ARRAY_BUFFER, m_attributes_array_id);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indices_array_id);
}



} // ~namespace Lumix
