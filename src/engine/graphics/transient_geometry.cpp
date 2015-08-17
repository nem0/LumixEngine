#include "transient_geometry.h"
#include "graphics/material.h"
#include "graphics/shader.h"
#include <cstring>


namespace Lumix
{


struct Vertex
{
	float positions[2];
	float uv[2];
	uint8_t color[4];
};


TransientGeometry::TransientGeometry(const void* vertex_data,
									 int num_vertices,
									 const bgfx::VertexDecl& decl,
									 const void* index_data,
									 int num_indices)
{
	if (bgfx::checkAvailTransientBuffers(num_vertices, decl, num_indices))
	{
		bgfx::allocTransientVertexBuffer(&m_vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&m_index_buffer, num_indices);

		memcpy(m_vertex_buffer.data, vertex_data, num_vertices * sizeof(Vertex));
		memcpy(m_index_buffer.data, index_data, num_indices * sizeof(uint16_t));
		m_num_vertices = num_vertices;
	}
	else
	{
		m_num_vertices = -1;
	}
}


TransientGeometry::~TransientGeometry()
{
}


} // namespace Lumix
