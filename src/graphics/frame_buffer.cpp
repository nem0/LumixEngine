#include "graphics/frame_buffer.h"
#include "core/json_serializer.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


FrameBuffer::FrameBuffer(const Declaration& decl)
{
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
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
}


FrameBuffer::~FrameBuffer()
{
	glDeleteFramebuffers(1, &m_id);
	for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
	{
		if (m_declaration.m_renderbuffers[i].m_is_texture)
		{
			glDeleteTextures(1, &m_declaration.m_renderbuffers[i].m_id);
		}
	}
}


void FrameBuffer::bind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER, m_id);
	GLenum buffers[Declaration::MAX_RENDERBUFFERS];
	ASSERT(m_declaration.m_renderbuffers_count <= sizeof(buffers) / sizeof(buffers[0]));
	int color_index = 0;
	for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
	{
		if (m_declaration.m_renderbuffers[i].isDepth())
		{
			buffers[i] = GL_DEPTH_ATTACHMENT;
		}
		else
		{
			buffers[i] = GL_COLOR_ATTACHMENT0 + color_index;
			++color_index;
		}
	}
	glDrawBuffers(m_declaration.m_renderbuffers_count, buffers);
}


GLuint FrameBuffer::getDepthTexture() const
{
	for (int i = 0; i < m_declaration.m_renderbuffers_count; ++i)
	{
		if (m_declaration.m_renderbuffers[i].isDepth())
		{
			return m_declaration.m_renderbuffers[i].m_id;
		}
	}
	return 0;
}


void FrameBuffer::unbind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
}


bool FrameBuffer::RenderBuffer::isDepth() const
{
	return m_format == GL_DEPTH_COMPONENT16 || m_format == GL_DEPTH_COMPONENT24 || m_format == GL_DEPTH_COMPONENT32;
}


static GLint getGLFormat(const char* format) 
{
	if (stricmp(format, "rg16") == 0)
	{
		return GL_RG16F;
	}
	else if (stricmp(format, "rgb32") == 0)
	{
		return GL_RGB32F;
	}
	else if (stricmp(format, "rgba32") == 0)
	{
		return GL_RGBA32F;
	}
	else if (stricmp(format, "rgb") == 0)
	{
		return GL_RGB8;
	}
	else if (stricmp(format, "rgba") == 0)
	{
		return GL_RGBA8;
	}
	else if (stricmp(format, "depth16") == 0)
	{
		return GL_DEPTH_COMPONENT16;
	}
	else if (stricmp(format, "depth24") == 0)
	{
		return GL_DEPTH_COMPONENT24;
	}
	else if (stricmp(format, "depth32") == 0)
	{
		return GL_DEPTH_COMPONENT32;
	}
	else
	{
		return 0;
	}
}


void FrameBuffer::RenderBuffer::deserialize(JsonSerializer& serializer)
{
	serializer.deserializeObjectBegin();
	while (!serializer.isObjectEnd())
	{
		char tmp[50];
		serializer.deserializeLabel(tmp, sizeof(tmp));
		if (strcmp(tmp, "format") == 0)
		{
			serializer.deserialize(tmp, sizeof(tmp), "rgb");
			m_format = getGLFormat(tmp);
		}
		else if (strcmp(tmp, "is_texture") == 0)
		{
			serializer.deserialize(m_is_texture, "false");
		}
		else
		{
			g_log_error.log("deserialize") << "Unknown renderbuffer format " << tmp;
		}
	}
	serializer.deserializeObjectEnd();
}


} // ~namespace Lumix
