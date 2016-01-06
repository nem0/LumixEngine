#include "transient_geometry.h"
#include "core/string.h"
#include "renderer/material.h"
#include "renderer/shader.h"
#include <cstring>


namespace Lumix
{


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

		copyMemory(m_vertex_buffer.data, vertex_data, num_vertices * decl.getStride());
		copyMemory(m_index_buffer.data, index_data, num_indices * sizeof(uint16));
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
