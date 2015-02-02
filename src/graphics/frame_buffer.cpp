#include "graphics/frame_buffer.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


FrameBuffer::FrameBuffer(int width, int height, int color_buffers_count, bool is_depth_buffer, const char* name)
	: m_name(name, m_name_allocator)
{
	m_width = width;
	m_height = height;
	glGenFramebuffersEXT(1, &m_id);
	int buffers_count = color_buffers_count + (is_depth_buffer ? 1 : 0);
	glGenRenderbuffersEXT(buffers_count, m_renderbuffers);
	glGenTextures(buffers_count, m_textures);

	glBindFramebufferEXT(GL_FRAMEBUFFER, m_id);
	for (int i = 0; i < color_buffers_count; ++i)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_renderbuffers[i]);
		glRenderbufferStorageEXT(GL_RENDERBUFFER, GL_RGBA32F_ARB, width, height);
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, m_renderbuffers[i]);

		glBindTexture(GL_TEXTURE_2D, m_textures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, m_textures[i], 0);
	}

	if (is_depth_buffer)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_renderbuffers[color_buffers_count]);
		glRenderbufferStorageEXT(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_renderbuffers[color_buffers_count]);

		glBindTexture(GL_TEXTURE_2D, m_textures[color_buffers_count]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_textures[color_buffers_count], 0);

	}

	//GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
	m_color_buffers_count = color_buffers_count;
	m_is_depth_buffer = is_depth_buffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
}


FrameBuffer::~FrameBuffer()
{
	glDeleteFramebuffers(1, &m_id);
	glDeleteTextures(m_color_buffers_count + (m_is_depth_buffer ? 1 : 0), m_textures);
}


void FrameBuffer::bind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER, m_id);
	GLenum buffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
	ASSERT(m_color_buffers_count <= sizeof(buffers) / sizeof(buffers[0]));
	glDrawBuffers(m_color_buffers_count, buffers);
}


void FrameBuffer::unbind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
}


} // ~namespace Lumix
