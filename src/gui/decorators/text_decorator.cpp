#include "gui/decorators/text_decorator.h"
#include "gui/irenderer.h"
#include "gui/block.h"


namespace Lux
{
namespace UI
{

	
	void TextDecorator::render(IRenderer& renderer, Block& block)
	{
		float w, h;
		renderer.measureText(block.getText().c_str(), &w, &h);
		renderer.setScissorArea((int)block.getGlobalLeft(), (int)block.getGlobalTop(), (int)block.getGlobalRight(), (int)block.getGlobalBottom());
		renderer.renderText(block.getText().c_str(), (block.getGlobalLeft() + block.getGlobalRight() - w) / 2.0f, (float)block.getGlobalTop(), block.getZ());
	}


} // ~namespace UI
} // ~namespace Lux