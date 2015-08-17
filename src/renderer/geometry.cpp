#include "renderer/geometry.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "renderer/shader.h"
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


void Geometry::setIndicesData(const short* data, int size)
{
	ASSERT(!bgfx::isValid(m_indices_array_id));
	const bgfx::Memory* mem = bgfx::alloc(size);
	memcpy(mem->data, data, size);
	m_indices_array_id = bgfx::createIndexBuffer(mem);
	m_indices_data_size = size;
}



void Geometry::setIndicesData(const int* data, int size)
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
