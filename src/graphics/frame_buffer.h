#pragma once


#include "core/stack_allocator.h"
#include "core/string.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


class FrameBuffer
{
	public:
		FrameBuffer(int width, int height, int color_buffers_count, bool is_depth_buffer, const char* name);
		~FrameBuffer();
		
		GLuint getId() const { return m_id; }
		GLuint getTexture(int index) const { return m_textures[index]; }
		GLuint getColorTexture(int index) const { return m_textures[index]; }
		GLuint getDepthTexture() const { ASSERT(m_is_depth_buffer); return m_textures[m_color_buffers_count]; }
		void bind();
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		const char* getName() { return m_name.c_str(); }
		static void unbind();

	private:
		static const int MAX_RENDERBUFFERS = 16;

		StackAllocator<64> m_name_allocator;
		string m_name;
		GLuint m_textures[MAX_RENDERBUFFERS];
		GLuint m_renderbuffers[MAX_RENDERBUFFERS];
		GLuint m_id;
		int m_color_buffers_count;
		bool m_is_depth_buffer;
		int m_width;
		int m_height;
};


} // ~namespace

