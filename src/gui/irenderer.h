#pragma once


#include "core/lux.h"
#include "gui/block.h"


namespace Lux
{


namespace FS
{
	class FileSystem;
}

namespace UI
{

	class TextureBase;


	class IRenderer LUX_ABSTRACT
	{
		public:
			virtual void loadFont(const char* path, FS::FileSystem& file_system) = 0;
			virtual TextureBase* loadImage(const char* name, FS::FileSystem& file_system) = 0;
			virtual void beginRender(float w, float h) = 0;
			virtual void renderImage(TextureBase* image, float* vertices, float* tex_coords, int vertex_count) = 0;
			virtual Block::Area getCharArea(const char* text, int pos, float max_width) = 0;
			virtual void measureText(const char* text, float* w, float* h, float max_width) = 0;
			virtual void renderText(const char* text, float x, float y, float z, float max_width) = 0;
			virtual void pushScissorArea(float left, float top, float right, float bottom) = 0;
			virtual void popScissorArea() = 0;
	};


} // ~namespace UI

} // ~namespace Lux