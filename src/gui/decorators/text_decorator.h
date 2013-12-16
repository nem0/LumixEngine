#pragma once


#include "core/lux.h"
#include "gui/decorator_base.h"


namespace Lux
{
namespace UI
{

	class IRenderer;

	class LUX_GUI_API TextDecorator : public DecoratorBase
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
			TextDecorator(const char* name) : DecoratorBase(name) {}

			virtual void render(IRenderer& renderer, Block& block) LUX_OVERRIDE;
	};

} // ~namespace UI
} // ~namespace Lux