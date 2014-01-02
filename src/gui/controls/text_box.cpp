#include "gui/controls/text_box.h"
#include "core/crc32.h"
#include "core/iserializer.h"


namespace Lux
{
namespace UI
{


TextBox::TextBox(const char* text, Gui& gui, Block* parent)
	: Block(gui, parent, "_box")
{
	setArea(0, 0, 0, 0, 0, 100, 0, 20);
	Lux::UI::Block* label_ui = new Block(gui, this, "_text");
	label_ui->setBlockText(text);
	label_ui->setArea(0, 3, 0, 0, 1, 0, 1, 0);
	label_ui->registerEventHandler("key_down", "_tb_key_down");
	label_ui->setIsClipping(true);
}


void TextBox::setOnTextAccepted(const char* callback)
{
	getChild(0)->registerEventHandler("text_accepted", callback);
}


void TextBox::setText(const string& text)
{
	setText(text.c_str());
}


void TextBox::setText(const char* text)
{
	getChild(0)->setBlockText(text);
}


const string& TextBox::getText() const
{
	return getChild(0)->getBlockText();
}


TextBox::~TextBox()
{
}


uint32_t TextBox::getType() const
{
	static const uint32_t hash = crc32("text_box");
	return hash;
}



void TextBox::serialize(ISerializer& serializer)
{
	Block::serializeWOChild(serializer);
	serializer.serialize("label", getChild(0)->getBlockText().c_str());
}


void TextBox::deserialize(ISerializer& serializer)
{
	char tmp[256];
	Block::deserializeWOChild(serializer);
	serializer.deserialize("label", tmp, 256);
	getChild(0)->setBlockText(tmp);
}


} // ~namespace UI
} // ~namespace Lux