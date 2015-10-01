#pragma once


#include <bgfx/bgfx.h>


struct lua_State;


namespace Lumix
{


class JsonSerializer;

/**/
class FrameBuffer
{
	public:
		struct RenderBuffer
		{
			bgfx::TextureFormat::Enum m_format;
			bgfx::TextureHandle m_handle;

			void parse(lua_State* state);
			bool isDepth() const;
		};

		struct Declaration
		{
			Declaration()
				: m_renderbuffers_count(0)
			{ }

			static const int MAX_RENDERBUFFERS = 16;

			int32_t m_width;
			int32_t m_height;
			RenderBuffer m_renderbuffers[MAX_RENDERBUFFERS];
			int32_t m_renderbuffers_count;
			char m_name[64];
		};

	public:
		explicit FrameBuffer(const Declaration& decl);
		FrameBuffer(const char* name, int width, int height, void* window_handle);
		~FrameBuffer();
		
		bgfx::FrameBufferHandle getHandle() const { return m_handle; }
		int getWidth() const { return m_declaration.m_width; }
		int getHeight() const { return m_declaration.m_height; }
		void resize(int width, int height);
		const char* getName() { return m_declaration.m_name; }
		bgfx::TextureHandle getRenderbufferHandle(int idx) const { return m_declaration.m_renderbuffers[idx].m_handle; }

	private:
		bool m_autodestroy_handle;
		void* m_window_handle;
		bgfx::FrameBufferHandle m_handle;
		Declaration m_declaration;
};


} // ~namespace

