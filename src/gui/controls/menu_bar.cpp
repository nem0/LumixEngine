#include "gui/controls/menu_bar.h"
#include "core/crc32.h"
#include "gui/controls/menu_item.h"


namespace Lux
{
namespace UI
{


MenuBar::MenuBar(Gui& gui, Block* parent)
	: Block(gui, parent, "_box")
{
	setArea(0, 0, 0, 0, 1, 0, 0, 20);
}


MenuBar::~MenuBar()
{
}


void MenuBar::addItem(class MenuItem* item)
{
	item->setParent(this);
	item->setArea(0, getChildCount() * 75.0f - 75.0f, 0, 0, 0, getChildCount() * 75.0f, 0, 20);
}


uint32_t MenuBar::getType() const
{
	static const uint32_t hash = crc32("menu_bar");
	return hash;
}


} // ~namespace UI
} // ~namespace Lux