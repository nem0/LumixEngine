#include "graphics/geometry.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "graphics/gl_ext.h"
#include "graphics/shader.h"

namespace Lumix
{


void VertexDef::parse(const char* data, int size)
{
	m_vertex_size = 0;
	int index = 0;
	for(int i = 0; i < size; ++i)
	{
		ASSERT(index < 15);
		switch(data[i])
		{
			case 'f':
				++i;
				if (data[i] == '4')
				{
					m_attributes[index] = VertexAttributeDef::FLOAT4;
					m_vertex_size += 4 * sizeof(float);
				}
				else if (data[i] == '2')
				{
					m_attributes[index] = VertexAttributeDef::FLOAT2;
					m_vertex_size += 2 * sizeof(float);
				}
				else
				{
					ASSERT(false);
				}
				break;
			case 'i':
				++i;
				if (data[i] == '4')
				{
					m_attributes[index] = VertexAttributeDef::INT4;
					m_vertex_size += 4 * sizeof(int);
				}
				else if (data[i] == '1')
				{
					m_attributes[index] = VertexAttributeDef::INT1;
					m_vertex_size += sizeof(int);
				}
				else
				{
					ASSERT(false);
				}
				break;
			case 'p':
				m_attributes[index] = VertexAttributeDef::POSITION;
				m_vertex_size += 3 * sizeof(float);
				break;
			case 'n':
				m_attributes[index] = VertexAttributeDef::NORMAL;
				m_vertex_size += 3 * sizeof(float);
				break;
			case 't':
				m_attributes[index] = VertexAttributeDef::TEXTURE_COORDS;
				m_vertex_size += 2 * sizeof(float);
				break;
			default:
				ASSERT(false);
				break;
		}
		++index;
	}
	m_attributes[index] = VertexAttributeDef::NONE;
	m_attribute_count = index;
}


int VertexDef::getPositionOffset() const
{
	int offset = 0;
	for(int i = 0; i < m_attribute_count; ++i)
	{
		switch(m_attributes[i])
		{
			case VertexAttributeDef::FLOAT2:
				offset += 2 * sizeof(float);
				break;
			case VertexAttributeDef::FLOAT4:
				offset += 4 * sizeof(float);
				break;
			case VertexAttributeDef::INT4:
				offset += 4 * sizeof(int);
				break;
			case VertexAttributeDef::INT1:
				offset += sizeof(int);
				break;
			case VertexAttributeDef::POSITION:
				return offset;
				break;
			case VertexAttributeDef::NORMAL:
				offset += 3 * sizeof(float);
				break;
			case VertexAttributeDef::TEXTURE_COORDS:
				offset += 2 * sizeof(float);
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
	int shader_attrib_idx = 0;
	int attribute_count = Math::minValue(m_attribute_count, shader.getAttributeCount());
	for(int i = 0; i < attribute_count; ++i)
	{
		GLint attrib_id = shader.getAttribId(shader_attrib_idx);
		switch(m_attributes[i])
		{
			case VertexAttributeDef::POSITION:
			case VertexAttributeDef::NORMAL:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 3, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 3;
				++shader_attrib_idx;
				break;
			case VertexAttributeDef::TEXTURE_COORDS:
			case VertexAttributeDef::FLOAT2:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 2, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 2;
				++shader_attrib_idx;
				break;
			case VertexAttributeDef::FLOAT4:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 4, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 4;
				++shader_attrib_idx;
				break;
			case VertexAttributeDef::INT4:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 4, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 4;
				++shader_attrib_idx;
				break;
			case VertexAttributeDef::INT1:
				glEnableVertexAttribArray(attrib_id);
				glVertexAttribPointer(attrib_id, 1, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 1;
				++shader_attrib_idx;
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
	int shader_attrib_idx = 0;
	for(int i = 0; i < m_attribute_count; ++i)
	{
		switch(m_attributes[i])
		{
			case VertexAttributeDef::POSITION:
			case VertexAttributeDef::NORMAL:
			case VertexAttributeDef::TEXTURE_COORDS:
			case VertexAttributeDef::INT1:
			case VertexAttributeDef::INT4:
			case VertexAttributeDef::FLOAT4:
			case VertexAttributeDef::FLOAT2:
				glDisableVertexAttribArray(shader.getAttribId(shader_attrib_idx));
				++shader_attrib_idx;
				break;
			default:
				ASSERT(false);
				break;
		}
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


void Geometry::copy(IAllocator& allocator, const Geometry& source, int copy_count, IndexCallback index_callback, VertexCallback vertex_callback)
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
