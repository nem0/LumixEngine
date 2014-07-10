#pragma once


#include "core/lumix.h"
#include "gui/decorator_base.h"


namespace Lumix
{
namespace UI
{

	class IRenderer;

	class LUMIX_GUI_API TextDecorator : public DecoratorBase
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
			TextDecorator(const char* name);

			virtual void render(IRenderer& renderer, Block& block) override;
			void setTextCentered(bool is_centered) { m_is_text_centered = is_centered; }
		
		private:
			bool m_is_text_centered;
	};

} // ~namespace UI
} // ~namespace Lumix
