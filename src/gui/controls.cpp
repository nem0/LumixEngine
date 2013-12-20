#include "gui/controls.h"
#include "gui/gui.h"
#include "gui/block.h"


namespace Lux
{

namespace UI
{


Block* createButton(const char* label, float x, float y, Block* parent, Gui& gui)
{
	Block* block = gui.createBlock(parent, "_box");
	block->setArea(0, x, 0, y, 0, x + 100, 0, y + 20);

	Block* block2 = gui.createBlock(block, "_text");
	block2->setText(label);
	block2->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	return block;
}


Block* createTextBox(float x, float y, Block* parent, Gui& gui)
{
	Block* block = gui.createBlock(parent, "_box");
	block->setArea(0, x, 0, y, 0, x + 100, 0, y + 20);
	
	Block* text = gui.createBlock(block, "_text");
	text->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	text->registerEventHandler("key_down", "_tb_key_down");

	return block;
}


Block* createComboBox(float x, float y, Block* parent, Gui& gui)
{
	Block* envelope = gui.createBlock(parent, NULL);
	envelope->setArea(0, x, 0, y, 0, x + 100, 0, y + 20);

	Block* cb = gui.createBlock(envelope, "_box");
	cb->setArea(0, 0, 0, 0, 1, 0, 0, 20);
	cb->registerEventHandler("click", "_cb_click");

	Block* popup = gui.createBlock(envelope, "_box");
	popup->hide();
	popup->setArea(0, 0, 0, 20, 1, 0, 0, 180);
	popup->registerEventHandler("blur", "_cb_blur");
	
	return envelope;
}


Block& addComboboxItem(Block& cb, Block& item)
{
	Block* popup = cb.getChild(1);
	float y = 0;
	for(int i = 0, c = popup->getChildCount(); i < c; ++i)
	{
		float h = popup->getChild(i)->getHeight();
		popup->getChild(i)->setArea(0, 0, 0, y, 1, 0, 0, h);
		y += popup->getChild(i)->getHeight();
	}
	item.setParent(cb.getChild(1));
	float h = item.getHeight();
	item.setArea(0, 0, 0, y, 1, 0, 0, h);
	return item;
}

} // ~namespace UI

} // ~namespace Lux