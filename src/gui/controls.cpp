#include "gui/controls.h"
#include "gui/gui.h"
#include "gui/block.h"


namespace Lux
{

namespace UI
{


Block* createButton(const char* label, int x, int y, Block* parent, Gui* gui)
{
	Block* block = new Block;
	block->setGui(gui);
	block->create(parent, gui->getDecorator("_box"));
	block->setPosition(x, y);
	block->setSize(100, 20);

	Block* block2 = new Block;
	block2->setText(label);
	block2->create(block, gui->getDecorator("_text"));
	block2->setPosition(0, 0);
	block2->setSize(100, 20);
	return block;
}


Block* createTextBox(int x, int y, Block* parent, Gui* gui)
{
	Block* block = new Block();
	block->setGui(gui);
	block->create(parent, gui->getDecorator("_box"));
	block->setPosition(x, y);
	block->setSize(100, 20);
	
	Block* text = new Block();
	text->create(block, gui->getDecorator("_text"));
	text->setPosition(0, 0);
	text->setSize(100, 20);
	struct KeyDown {
		
	};
	text->registerEventHandler("key_down", gui->getCallback("_tb_key_down"));

	return block;
}


Block* createComboBox(int x, int y, Block* parent, Gui* gui)
{
	Block* envelope = new Block();
	envelope->setGui(gui);
	envelope->create(parent, NULL);
	envelope->setPosition(x, y);
	envelope->setSize(100, 20);

	Block* cb = new Block();
	cb->create(envelope, gui->getDecorator("_box"));
	cb->setPosition(0, 0);
	cb->setSize(100, 20);
	cb->registerEventHandler("click", gui->getCallback("_cb_click"));

	Block* popup = new Block();
	popup->create(envelope, gui->getDecorator("_box"));
	popup->hide();
	popup->setPosition(0, 20);
	popup->setSize(100, 180);
	popup->registerEventHandler("blur", gui->getCallback("_cb_blur"));
	
	return envelope;
}


Block& addComboboxItem(Block& cb, Block& item)
{
	Block* popup = cb.getChild(1);
	int y = 0;
	int width = popup->getWidth();
	for(int i = 0, c = popup->getChildCount(); i < c; ++i)
	{
		int h = popup->getChild(i)->getHeight();
		popup->getChild(i)->setPosition(0, y);
		popup->getChild(i)->setSize(width, h);
		y += popup->getChild(i)->getHeight();
	}
	item.setParent(cb.getChild(1));
	int h = item.getHeight();
	item.setPosition(0, y);
	item.setSize(width, h);
	return item;
}

} // ~namespace UI

} // ~namespace Lux