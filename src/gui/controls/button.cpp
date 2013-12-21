#include "gui/controls/button.h"
#include "core/crc32.h"
#include "core/iserializer.h"


namespace Lux
{
namespace UI
{


Button::Button(const char* label, Gui& gui, Block* parent)
	: Block(gui, parent, "_box")
{
	setArea(0, 0, 0, 0, 0, 100, 0, 20);
	Lux::UI::Block* label_ui = new Block(gui, this, "_text_centered");
	label_ui->setBlockText(label);
	label_ui->setArea(0, 0, 0, 0, 1, 0, 1, 0);
}


Button::~Button()
{
}


uint32_t Button::getType() const
{
	static const uint32_t hash = crc32("button");
	return hash;
}


void Button::serialize(ISerializer& serializer)
{
	Block::serializeWOChild(serializer);
	serializer.serialize("text", getChild(0)->getBlockText().c_str());
}


void Button::deserialize(ISerializer& serializer)
{
	char tmp[256];
	Block::deserializeWOChild(serializer);
	serializer.deserialize("text", tmp, 256);
	getChild(0)->setBlockText(tmp);
}


} // ~namespace UI
} // ~namespace Lux