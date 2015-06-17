#include "graphics/frame_buffer.h"
#include "core/json_serializer.h"
#include "core/vec3.h"
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


FrameBuffer::FrameBuffer(const Declaration& decl)
{
	TODO("bgfx");
	/*
	m_declaration = decl;
	glGenFramebuffersEXT(1, &m_id);

	glBindFramebufferEXT(GL_FRAMEBUFFER, m_id);
	for (int i = 0; i < decl.m_renderbuffers_count; ++i)
	{
		const RenderBuffer& renderbuffer = decl.m_renderbuffers[i];
		if (renderbuffer.m_is_texture)
		{
			glGenTextures(1, &m_declaration.m_renderbuffers[i].m_id);
			glBindTexture(GL_TEXTURE_2D, m_declaration.m_renderbuffers[i].m_id);

			glTexStorage2D(GL_TEXTURE_2D, 1, renderbuffer.m_format, decl.m_width, decl.m_height);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			GLenum attachment = renderbuffer.isDepth() ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0 + i;
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, m_declaration.m_renderbuffers[i].m_id, 0);
		}
		else
		{
			glGenRenderbuffersEXT(1, &m_declaration.m_renderbuffers[i].m_id);
			glBindRenderbuffer(GL_RENDERBUFFER, m_declaration.m_renderbuffers[i].m_id);
			glRenderbufferStorageEXT(GL_RENDERBUFFER, renderbuffer.m_format, decl.m_width, decl.m_height);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, m_declaration.m_renderbuffers[i].m_id);
		}
	}

	//GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);*/
}


FrameBuffer::~FrameBuffer()
{
	TODO("bgfx");
	/*
	glDeleteFramebuffers(1, &m_id);
	for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
	{
		if (m_declaration.m_renderbuffers[i].m_is_texture)
		{
			glDeleteTextures(1, &m_declaration.m_renderbuffers[i].m_id);
		}
	}*/
}


bool FrameBuffer::RenderBuffer::isDepth() const
{
	TODO("bgfx");
	return false;
	/*
	return m_format == GL_DEPTH_COMPONENT16 || m_format == GL_DEPTH_COMPONENT24 || m_format == GL_DEPTH_COMPONENT32;
	*/
}


void FrameBuffer::RenderBuffer::parse(lua_State* L)
{
	TODO("bgfx");
	/*
	if (lua_getfield(L, -1, "format") == LUA_TSTRING)
	{
		m_format = getGLFormat(lua_tostring(L, -1));
	}
	lua_pop(L, 1);
	if (lua_getfield(L, -1, "is_texture") == LUA_TBOOLEAN)
	{
		m_is_texture = lua_toboolean(L, -1) != 0;
	}
	lua_pop(L, 1);*/
}


} // ~namespace Lumix
