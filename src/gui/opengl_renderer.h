#pragma once


#include "core/lux.h"
#include "gui/irenderer.h"


namespace Lux
{
namespace UI
{


	class LUX_GUI_API OpenGLRenderer : public IRenderer
	{
		public:
			OpenGLRenderer() { m_impl = 0; }

			bool create();
			void destroy();

			virtual int loadImage(const char* name) LUX_OVERRIDE;
			virtual bool loadFont(const char* path) LUX_OVERRIDE;
			virtual void beginRender() LUX_OVERRIDE;
			virtual void renderImage(int image, float* vertices, float* tex_coords, int vertex_count) LUX_OVERRIDE;
			virtual void measureText(const char* text, float* w, float* h) LUX_OVERRIDE;
			virtual void renderText(const char* text, float x, float y) LUX_OVERRIDE;
			virtual void setScissorArea(int left, int top, int right, int bottom) LUX_OVERRIDE;
			void setWindowHeight(int height);

		private:
			struct OpenGLRendererImpl* m_impl;
	};


} // ~namespace UI
} // ~namespace Lux
