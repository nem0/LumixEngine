#pragma once


#include "lumix.h"
#include <bgfx.h>


namespace Lumix
{


class Material;


class LUMIX_RENDERER_API TransientGeometry
{
public:
	TransientGeometry(const void* vertex_data,
					  int vertex_num,
					  const bgfx::VertexDecl& decl,
					  const void* index_data,
					  int index_num);
	~TransientGeometry();

	bgfx::TransientVertexBuffer& getVertexBuffer() { return m_vertex_buffer; }
	bgfx::TransientIndexBuffer& getIndexBuffer() { return m_index_buffer; }
	int getNumVertices() const { return m_num_vertices; }

private:
	bgfx::TransientVertexBuffer m_vertex_buffer;
	bgfx::TransientIndexBuffer m_index_buffer;
	int m_num_vertices;
};


} // namespace Lumix
