#include "graphics/frame_buffer.h"
#include "core/json_serializer.h"
#include "core/vec3.h"
#include <bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


FrameBuffer::FrameBuffer(const Declaration& decl)
{
	m_declaration = decl;
	bgfx::TextureHandle texture_handles[16];

	for (int i = 0; i < decl.m_renderbuffers_count; ++i)
	{
		const RenderBuffer& renderbuffer = decl.m_renderbuffers[i];
		texture_handles[i] = bgfx::createTexture2D(decl.m_width, decl.m_height, 1, renderbuffer.m_format, 0);
		m_declaration.m_renderbuffers[i].m_handle = texture_handles[i];
	}

	m_handle = bgfx::createFrameBuffer(decl.m_renderbuffers_count, texture_handles);
}


FrameBuffer::~FrameBuffer()
{
	bgfx::destroyFrameBuffer(m_handle);
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
	if (strcmp(name, "depth32") == 0)
	{
		return bgfx::TextureFormat::D32;
	}
	else if (strcmp(name, "depth24") == 0)
	{
		return bgfx::TextureFormat::D24;
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
