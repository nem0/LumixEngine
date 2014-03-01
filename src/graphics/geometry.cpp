#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/shader.h"

namespace Lux
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
				if(data[i] == '4')
				{
					m_attributes[index] = VertexAttributeDef::FLOAT4;
					m_vertex_size += 4 * sizeof(float);
				}
				else
				{
					ASSERT(false);
				}
				break;
			case 'i':
				++i;
				if(data[i] == '4')
				{
					m_attributes[index] = VertexAttributeDef::INT4;
					m_vertex_size += 4 * sizeof(int);
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
			case VertexAttributeDef::FLOAT4:
				offset += 4 * sizeof(float);
				break;
			case VertexAttributeDef::INT4:
				offset += 4 * sizeof(int);
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


void VertexDef::begin(Shader& shader)
{
	int offset = 0;
	int shader_attrib_idx = 0;
	for(int i = 0; i < m_attribute_count; ++i)
	{
		switch(m_attributes[i])
		{
			case VertexAttributeDef::POSITION:
				glEnableClientState(GL_VERTEX_ARRAY);
				glVertexPointer(3, GL_FLOAT, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 3;
				break;
			case VertexAttributeDef::NORMAL:
				glEnableClientState(GL_NORMAL_ARRAY);
				glNormalPointer(GL_FLOAT, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 3;
				break;
			case VertexAttributeDef::TEXTURE_COORDS:
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 2;
				break;
			case VertexAttributeDef::FLOAT4:
				glEnableVertexAttribArray(shader.getAttribId(shader_attrib_idx));
				glVertexAttribPointer(shader.getAttribId(shader_attrib_idx), 4, GL_FLOAT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLfloat) * 4;
				++shader_attrib_idx;
				break;
			case VertexAttributeDef::INT4:
				glEnableVertexAttribArray(shader.getAttribId(shader_attrib_idx));
				glVertexAttribPointer(shader.getAttribId(shader_attrib_idx), 4, GL_INT, GL_FALSE, m_vertex_size, (GLvoid*)offset);
				offset += sizeof(GLint) * 4;
				++shader_attrib_idx;
				break;
			default:
				ASSERT(false);
				break;
		}
	}
	
}


void VertexDef::end(Shader& shader)
{
	int shader_attrib_idx = 0;
	for(int i = 0; i < m_attribute_count; ++i)
	{
		switch(m_attributes[i])
		{
			case VertexAttributeDef::POSITION:
				glDisableClientState(GL_VERTEX_ARRAY);
				break;
			case VertexAttributeDef::NORMAL:
				glDisableClientState(GL_NORMAL_ARRAY);
				break;
			case VertexAttributeDef::TEXTURE_COORDS:
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				break;
			case VertexAttributeDef::FLOAT4:
				glDisableVertexAttribArray(shader.getAttribId(shader_attrib_idx));
				++shader_attrib_idx;
				break;
			case VertexAttributeDef::INT4:
				glDisableVertexAttribArray(shader.getAttribId(shader_attrib_idx));
				++shader_attrib_idx;
				break;
			default:
				ASSERT(false);
				break;
		}
	}
	
}


float Geometry::getBoundingRadius() const
{
	float d = 0;
	for(int i = 0, c = m_vertices.size(); i < c; ++i)
	{
		float l = m_vertices[i].squaredLength();
		if(l > d)
		{
			d = l;
		}
	}
	return sqrtf(d);
}


void Geometry::draw(int start, int count, Shader& shader)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_id);
	m_vertex_definition.begin(shader);
	glDrawArrays(GL_TRIANGLES, start, count);
	m_vertex_definition.end(shader);
}


Geometry::Geometry()
{
	glGenBuffers(1, &m_id);
}


Geometry::~Geometry()
{
	glDeleteBuffers(1, &m_id);
}


void Geometry::copy(const uint8_t* data, int size, VertexDef vertex_definition)
{
	m_vertex_definition = vertex_definition;
	int vertex_size = m_vertex_definition.getVertexSize();
	m_vertices.resize(size / vertex_size);
	int pos_offset = m_vertex_definition.getPositionOffset();
	for(int i = 0, c = m_vertices.size(); i < c; ++i)
	{
		m_vertices[i] = *(Vec3*)(data + vertex_size * i + pos_offset);
	}
	glBindBuffer(GL_ARRAY_BUFFER, m_id);
	glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}


} // ~namespace Lux
