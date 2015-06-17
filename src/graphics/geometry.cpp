#include "graphics/geometry.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "graphics/shader.h"
#include <bgfx.h>


namespace Lumix
{
	

Geometry::Geometry()
{
	m_indices_array_id = BGFX_INVALID_HANDLE;
	m_attributes_array_id = BGFX_INVALID_HANDLE;
}


Geometry::~Geometry()
{
	clear();
}


void Geometry::copy(const Geometry& source, int copy_count, IndexCallback index_callback, VertexCallback vertex_callback, IAllocator& allocator)
{
	/*
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
	m_attributes_data_size = source.m_attributes_data_size * copy_count;*/
	ASSERT(false);
	TODO("todo");
}


void Geometry::clear()
{
	if (bgfx::isValid(m_indices_array_id))
	{
		bgfx::destroyIndexBuffer(m_indices_array_id);
	}
	if (bgfx::isValid(m_attributes_array_id))
	{
		bgfx::destroyVertexBuffer(m_attributes_array_id);
	}

	m_indices_array_id = BGFX_INVALID_HANDLE;
	m_attributes_array_id = BGFX_INVALID_HANDLE;

	m_indices_data_size = 0;
	m_attributes_data_size = 0;
}


void Geometry::setAttributesData(const void* data, int size, const bgfx::VertexDecl& decl)
{
	ASSERT(!bgfx::isValid(m_attributes_array_id));
	const bgfx::Memory* mem = bgfx::alloc(size);
	memcpy(mem->data, data, size);
	m_attributes_array_id = bgfx::createVertexBuffer(mem, decl);
	m_attributes_data_size = size;
}


void Geometry::setIndicesData(const void* data, int size)
{
	ASSERT(!bgfx::isValid(m_indices_array_id));
	const bgfx::Memory* mem = bgfx::alloc(size);
	memcpy(mem->data, data, size);
	m_indices_array_id = bgfx::createIndexBuffer(mem, BGFX_BUFFER_INDEX32);
	m_indices_data_size = size;
}


void Geometry::bindBuffers() const
{
	bgfx::setIndexBuffer(m_indices_array_id);
	bgfx::setVertexBuffer(m_attributes_array_id);
}



} // ~namespace Lumix
