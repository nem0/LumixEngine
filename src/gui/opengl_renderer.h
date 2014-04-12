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

			virtual TextureBase* loadImage(const char* name, FS::FileSystem& file_system) override;
			virtual void loadFont(const char* path, FS::FileSystem& file_system) override;
			virtual void beginRender(float w, float h) override;
			virtual void renderImage(TextureBase* image, float* vertices, float* tex_coords, int vertex_count) override;
			virtual Block::Area getCharArea(const char* text, int pos, float max_width) override;
			virtual void measureText(const char* text, float* w, float* h, float max_width) override;
			virtual void renderText(const char* text, float x, float y, float z, float max_width) override;
			virtual void pushScissorArea(float left, float top, float right, float bottom) override;
			virtual void popScissorArea() override;
			void setWindowHeight(int height);

		private:
			struct OpenGLRendererImpl* m_impl;
	};


} // ~namespace UI
} // ~namespace Lux
