#include "gui/decorators/text_decorator.h"
#include "gui/irenderer.h"
#include "gui/block.h"


namespace Lux
{
namespace UI
{


	TextDecorator::TextDecorator(const char* name)
		: DecoratorBase(name)
	{
		m_is_text_centered = false;
	}

	
	void TextDecorator::render(IRenderer& renderer, Block& block)
	{
		float w, h;
		renderer.measureText(block.getBlockText().c_str(), &w, &h);
		renderer.setScissorArea((int)block.getGlobalLeft(), (int)block.getGlobalTop(), (int)block.getGlobalRight(), (int)block.getGlobalBottom());
		if(m_is_text_centered)
		{
			renderer.renderText(block.getBlockText().c_str(), (block.getGlobalRight() + block.getGlobalLeft() - w) / 2, (float)block.getGlobalTop(), block.getZ());
		}
		else
		{
			renderer.renderText(block.getBlockText().c_str(), block.getGlobalLeft(), (float)block.getGlobalTop(), block.getZ());
		}
	}


} // ~namespace UI
} // ~namespace Lux