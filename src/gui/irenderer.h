#pragma once


#include "core/lux.h"


namespace Lux
{

namespace UI
{

	class TextureBase;


	class IRenderer LUX_ABSTRACT
	{
		public:
			virtual bool loadFont(const char* path) = 0;
			virtual TextureBase* loadImage(const char* name) = 0;
			virtual void beginRender() = 0;
			virtual void renderImage(TextureBase* image, float* vertices, float* tex_coords, int vertex_count) = 0;
			virtual void measureText(const char* text, float* w, float* h) = 0;
			virtual void renderText(const char* text, float x, float y, float z) = 0;
			virtual void setScissorArea(int left, int top, int right, int bottom) = 0;
	};


} // ~namespace UI

} // ~namespace Lux