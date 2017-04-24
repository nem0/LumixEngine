#include "renderer/frame_buffer.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/string.h"
#include "engine/vec.h"
#include <bgfx/bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


FrameBuffer::FrameBuffer(const Declaration& decl)
	: m_declaration(decl)
{
	m_autodestroy_handle = true;
	bgfx::TextureHandle texture_handles[16];

	for (int i = 0; i < decl.m_renderbuffers_count; ++i)
	{
		const RenderBuffer& renderbuffer = decl.m_renderbuffers[i];
		texture_handles[i] = bgfx::createTexture2D((uint16_t)decl.m_width,
			(uint16_t)decl.m_height,
			false, 
			1,
			renderbuffer.m_format,
			BGFX_TEXTURE_RT);
		m_declaration.m_renderbuffers[i].m_handle = texture_handles[i];
	}

	m_window_handle = nullptr;
	m_handle = bgfx::createFrameBuffer((uint8_t)decl.m_renderbuffers_count, texture_handles);
	ASSERT(bgfx::isValid(m_handle));
}


FrameBuffer::FrameBuffer(const char* name, int width, int height, void* window_handle)
{
	m_autodestroy_handle = false;
	copyString(m_declaration.m_name, name);
	m_declaration.m_width = width;
	m_declaration.m_height = height;
	m_declaration.m_renderbuffers_count = 0;
	m_window_handle = window_handle;
	m_handle = bgfx::createFrameBuffer(window_handle, (uint16_t)width, (uint16_t)height);
	ASSERT(bgfx::isValid(m_handle));
}


FrameBuffer::~FrameBuffer()
{
	if (m_autodestroy_handle)
	{
		destroyRenderbuffers();
		bgfx::destroyFrameBuffer(m_handle);
	}
}


void FrameBuffer::destroyRenderbuffers()
{
	for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
	{
		bgfx::destroyTexture(m_declaration.m_renderbuffers[i].m_handle);
	}
}


void FrameBuffer::resize(int width, int height)
{
	if (bgfx::isValid(m_handle))
	{
		destroyRenderbuffers();
		bgfx::destroyFrameBuffer(m_handle);
	}

	m_declaration.m_width = width;
	m_declaration.m_height = height;
	if (m_window_handle)
	{
		m_handle = bgfx::createFrameBuffer(m_window_handle, (uint16_t)width, (uint16_t)height);
	}
	else
	{
		bgfx::TextureHandle texture_handles[16];

		for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
		{
			const RenderBuffer& renderbuffer = m_declaration.m_renderbuffers[i];
			texture_handles[i] = bgfx::createTexture2D(
				(uint16_t)width, (uint16_t)height, false, 1, renderbuffer.m_format, BGFX_TEXTURE_RT);
			m_declaration.m_renderbuffers[i].m_handle = texture_handles[i];
		}

		m_window_handle = nullptr;
		m_handle = bgfx::createFrameBuffer((uint8_t)m_declaration.m_renderbuffers_count, texture_handles);
	}
}


static bgfx::TextureFormat::Enum getFormat(const char* name) 
{
	static const struct { const char* name; bgfx::TextureFormat::Enum value; } FORMATS[] = {
		{ "depth32", bgfx::TextureFormat::D32 },
		{ "depth24", bgfx::TextureFormat::D24 },
		{ "depth24stencil8", bgfx::TextureFormat::D24S8 },
		{ "rgba8", bgfx::TextureFormat::RGBA8 },
		{ "rgba16f", bgfx::TextureFormat::RGBA16F },
		{ "r32f", bgfx::TextureFormat::R32F },
	};

	for (auto& i : FORMATS)
	{
		if (equalStrings(i.name, name)) return i.value;
	}
	g_log_error.log("Renderer") << "Uknown texture format " << name;
	return bgfx::TextureFormat::RGBA8;
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


} // namespace Lumix
