#include "frame_buffer.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/string.h"
#include "core/vec.h"
#include <bgfx/bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


FrameBuffer::FrameBuffer(const char* name, int width, int height, void* window_handle)
	: m_width(width)
	, m_height(height)
{
	copyString(m_name, name);

	m_window_handle = window_handle;
	m_handle = bgfx::createFrameBuffer(window_handle, (uint16_t)width, (uint16_t)height);
	ASSERT(bgfx::isValid(m_handle));
}


FrameBuffer::~FrameBuffer()
{
	if(bgfx::isValid(m_handle))
		bgfx::destroyFrameBuffer(m_handle);
}


void FrameBuffer::resize(int width, int height)
{
	if (bgfx::isValid(m_handle))
		bgfx::destroyFrameBuffer(m_handle);

	m_width = width;
	m_height = height;
	if (m_window_handle)
	{
		m_handle = bgfx::createFrameBuffer(m_window_handle, (uint16_t)width, (uint16_t)height);
	}
	else
	{
		ASSERT(false);
	}
}


} // ~namespace Lumix
