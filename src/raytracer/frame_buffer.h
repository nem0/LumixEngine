#pragma once


#include "lumix.h"
#include "core/vec.h"
#include <bgfx/bgfx.h>


struct lua_State;


namespace Lumix
{


class JsonSerializer;


class FrameBuffer
{
	public:
		struct RenderBuffer
		{
			bgfx::TextureFormat::Enum m_format;
			bgfx::TextureHandle m_handle;
		};

	public:
		FrameBuffer(const char* name, int width, int height, void* window_handle);
		~FrameBuffer();

		bgfx::FrameBufferHandle getHandle() const { return m_handle; }
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		void resize(int width, int height);
		Vec2 getSizeRatio() const { return m_size_ratio; }
		const char* getName() const { return m_name; }

	private:
		void* m_window_handle;
		bgfx::FrameBufferHandle m_handle;
		
		char m_name[64];
		int32 m_width;
		int32 m_height;
		Vec2 m_size_ratio;
};


} // namespace Lumix

