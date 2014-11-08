#include "graphics/frame_buffer.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


FrameBuffer::FrameBuffer(int width, int height, int render_buffers, const char* name)
	: m_name(name, m_name_allocator)
{
	m_width = width;
	m_height = height;
	glGenFramebuffersEXT(1, &m_id);
	glGenRenderbuffersEXT(RENDERBUFFERS_COUNT, m_renderbuffers);
	glGenTextures(RENDERBUFFERS_COUNT, m_textures);

	glBindFramebufferEXT(GL_FRAMEBUFFER, m_id);
	for(int i = 0; i < RENDERBUFFERS_COUNT; ++i)
	{
		if(render_buffers & (1 << i))
		{
			if(i == DEPTH)
			{
				glRenderbufferStorageEXT(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
				glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_renderbuffers[DEPTH]);

				glBindTexture(GL_TEXTURE_2D, m_textures[DEPTH]);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_textures[DEPTH], 0);
			}
			else
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
		}
	}

	//GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
	m_render_buffers = render_buffers;
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
}


FrameBuffer::~FrameBuffer()
{
	glDeleteFramebuffers(1, &m_id);
	glDeleteTextures(RENDERBUFFERS_COUNT, m_textures);
}


void FrameBuffer::bind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER, m_id);
}


void FrameBuffer::unbind()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
}


} // ~namespace Lumix
