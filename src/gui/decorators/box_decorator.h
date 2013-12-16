#pragma once


#include "core/lux.h"
#include "gui/decorator_base.h"
#include "core/vec3.h"


namespace Lux
{
namespace UI
{

	class IRenderer;

	class LUX_GUI_API BoxDecorator : public DecoratorBase
	{
		public:
			struct Part
			{
				float m_x;
				float m_y;
				float m_w;
				float m_h;
			};

		public:
			BoxDecorator(const char* name) : DecoratorBase(name) {}

			bool create(IRenderer& renderer, const char* image);
			void setPart(int part, float x, float y, float w, float h);
			virtual void render(IRenderer& renderer, Block& block) LUX_OVERRIDE;

		private:
			int m_image;
			Part m_parts[3];
			Vec3 m_vertices[108];
			float m_uvs[108];
	};

} // ~namespace UI
} // ~namespace Lux