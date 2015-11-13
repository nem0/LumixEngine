#include "renderer/frame_buffer.h"
#include "core/json_serializer.h"
#include "core/vec.h"
#include <bgfx/bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


FrameBuffer::FrameBuffer(const Declaration& decl)
{
	m_autodestroy_handle = true;
	m_declaration = decl;
	bgfx::TextureHandle texture_handles[16];

	for (int i = 0; i < decl.m_renderbuffers_count; ++i)
	{
		const RenderBuffer& renderbuffer = decl.m_renderbuffers[i];
		texture_handles[i] = bgfx::createTexture2D(decl.m_width,
												   decl.m_height,
												   1,
												   renderbuffer.m_format,
												   BGFX_TEXTURE_RT);
		m_declaration.m_renderbuffers[i].m_handle = texture_handles[i];
	}

	m_window_handle = nullptr;
	m_handle = bgfx::createFrameBuffer(decl.m_renderbuffers_count, texture_handles);
}


FrameBuffer::FrameBuffer(const char* name, int width, int height, void* window_handle)
{
	m_autodestroy_handle = false;
	copyString(m_declaration.m_name, name);
	m_declaration.m_width = width;
	m_declaration.m_height = height;
	m_declaration.m_renderbuffers_count = 0;
	m_window_handle = window_handle;
	m_handle = bgfx::createFrameBuffer(window_handle, width, height);
}


FrameBuffer::~FrameBuffer()
{
	if (m_autodestroy_handle) bgfx::destroyFrameBuffer(m_handle);
}


void FrameBuffer::resize(int width, int height)
{
	if (bgfx::isValid(m_handle)) bgfx::destroyFrameBuffer(m_handle);

	m_declaration.m_width = width;
	m_declaration.m_height = height;
	if (m_window_handle)
	{
		m_handle = bgfx::createFrameBuffer(m_window_handle, width, height);
	}
	else
	{
		bgfx::TextureHandle texture_handles[16];

		for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
		{
			const RenderBuffer& renderbuffer = m_declaration.m_renderbuffers[i];
			texture_handles[i] =
				bgfx::createTexture2D(width, height, 1, renderbuffer.m_format, BGFX_TEXTURE_RT);
			m_declaration.m_renderbuffers[i].m_handle = texture_handles[i];
		}

		m_window_handle = nullptr;
		m_handle = bgfx::createFrameBuffer(m_declaration.m_renderbuffers_count, texture_handles);
	}
}


bool FrameBuffer::RenderBuffer::isDepth() const
{
	switch(m_format)
	{
		case bgfx::TextureFormat::D32:
		case bgfx::TextureFormat::D24:
			return true;
	}
	return false;
}


static bgfx::TextureFormat::Enum getFormat(const char* name) 
{
	if (compareString(name, "depth32") == 0)
	{
		return bgfx::TextureFormat::D32;
	}
	else if (compareString(name, "depth24") == 0)
	{
		return bgfx::TextureFormat::D24;
	}
	else if (compareString(name, "rgba8") == 0)
	{
		return bgfx::TextureFormat::RGBA8;
	}
	else if (compareString(name, "r32f") == 0)
	{
		return bgfx::TextureFormat::R32F;
	}
	else
	{
		g_log_error.log("Renderer") << "Uknown texture format " << name;
		return bgfx::TextureFormat::RGBA8;
	}
}


void FrameBuffer::RenderBuffer::parse(lua_State* L)
{
	if (lua_getfield(L, -1, "format") == LUA_TSTRING)
	{
		m_format = getFormat(lua_tostring(L, -1));
	}
	else
	{
		m_format = bgfx::TextureFormat::RGBA8;
	}
	lua_pop(L, 1);
}


} // ~namespace Lumix
