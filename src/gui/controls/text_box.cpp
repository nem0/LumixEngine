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
	label_ui->onEvent("key_down").bind<TextBox, &TextBox::keyDown>(this);
	label_ui->setIsClipping(true);
}


void TextBox::keyDown(Block& block, void* user_data)
{
	Lux::string s = block.getBlockText();
	char c[2];
	switch((int32_t)user_data)
	{
		case '\r':
			block.emitEvent("text_accepted");
			break;
		case '\b':
			s = s.substr(0, s.length() - 1);
			break;
		default:                        
			c[0] = (char)user_data;
			c[1] = '\0';
			s += c;
			break;
	}
	block.setBlockText(s.c_str());
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