#pragma once


#include <Windows.h>
#include <gl/GL.h>
#include "shader.h"


namespace Lux
{


class FrameBuffer
{
	public:
		enum RenderBuffers
		{
			DIFFUSE,
			POSITION,
			NORMAL,
			DEPTH,
			RENDERBUFFERS_COUNT
		};

		enum RenderBufferBits
		{
			DIFFUSE_BIT = 1 << DIFFUSE,
			POSITION_BIT = 1 << POSITION,
			NORMAL_BIT = 1 << NORMAL,
			DEPTH_BIT = 1 << DEPTH
		};

	public:
		FrameBuffer(int width, int height, int render_buffers);
		~FrameBuffer();
		
		GLuint getId() const { return m_id; }
		GLuint getDiffuseTexture() const { return m_textures[DIFFUSE]; }
		GLuint getPositionTexture() const { return m_textures[POSITION]; }
		GLuint getNormalTexture() const { return m_textures[NORMAL]; }
		GLuint getDepthTexture() const { return m_textures[DEPTH]; }
		GLuint getTexture(RenderBuffers render_buffer) { return m_textures[render_buffer]; }
		void bind();
		
		static void unbind();

	private:
		GLuint m_textures[RENDERBUFFERS_COUNT];
		GLuint m_renderbuffers[RENDERBUFFERS_COUNT];
		GLuint m_id;
		int m_render_buffers;
};


} // ~namespace

