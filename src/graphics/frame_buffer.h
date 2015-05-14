#pragma once


#include "core/stack_allocator.h"
#include "core/string.h"
#include "graphics/gl_ext.h"


namespace Lumix
{


class JsonSerializer;


class FrameBuffer
{
	public:
		struct RenderBuffer
		{
			RenderBuffer() { m_format = 0; m_is_texture = true; m_id = 0; }

			GLint m_format;
			bool m_is_texture;
			GLuint m_id;

			void deserialize(JsonSerializer& serializer);
			bool isDepth() const;
		};

		struct Declaration
		{
			Declaration()
				: m_name(m_name_allocator)
			{ }

			static const int MAX_RENDERBUFFERS = 16;

			int32_t m_width;
			int32_t m_height;
			RenderBuffer m_renderbuffers[MAX_RENDERBUFFERS];
			int32_t m_renderbuffers_count;
			StackAllocator<64> m_name_allocator;
			string m_name;
		};

	public:
		FrameBuffer(const Declaration& decl);
		~FrameBuffer();
		
		GLuint getId() const { return m_id; }
		GLuint getTexture(int index) const { return m_declaration.m_renderbuffers[index].m_id; }
		GLuint getDepthTexture() const;
		void bind();
		int getWidth() const { return m_declaration.m_width; }
		int getHeight() const { return m_declaration.m_height; }
		const char* getName() { return m_declaration.m_name.c_str(); }
		static void unbind();

	private:

		Declaration m_declaration;
		GLuint m_id;
};


} // ~namespace

