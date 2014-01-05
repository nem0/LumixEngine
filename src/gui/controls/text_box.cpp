#include "gui/controls/text_box.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/gui.h"


namespace Lux
{
namespace UI
{


TextBox::TextBox(const char* text, Gui& gui, Block* parent)
	: Block(gui, parent, "_box")
{
	setArea(0, 0, 0, 0, 0, 100, 0, 20);
	Lux::UI::Block* label_ui = LUX_NEW(Block)(gui, this, "_text");
	label_ui->setBlockText(text);
	label_ui->setArea(0, 3, 0, 0, 1, 0, 1, 0);
	label_ui->onEvent("key_down").bind<TextBox, &TextBox::keyDown>(this);
	label_ui->onEvent("focus").bind<TextBox, &TextBox::focused>(this);
	label_ui->onEvent("blur").bind<TextBox, &TextBox::blurred>(this);
	label_ui->setIsClipping(true);
	m_cursor_pos = 0; 
	m_cursor = LUX_NEW(Block)(gui, label_ui, "_cursor");
	m_cursor->hide();
}


void TextBox::setCursorArea()
{
	Block::Area area = getGui().getRenderer().getCharArea(getChild(0)->getBlockText().c_str(), m_cursor_pos, getGlobalWidth());
	m_cursor->setArea(area);
	layout();
}


void TextBox::blurred(Block& block, void* user_data)
{
	m_cursor->hide();
}


void TextBox::focused(Block& block, void* user_data)
{
	m_cursor_pos = getChild(0)->getBlockText().length();
	m_cursor->show();
	setCursorArea();
}

static const int32_t KEY_RIGHT = 79 + (1 << 30);
static const int32_t KEY_LEFT = 80 + (1 << 30);
static const int32_t KEY_UP = 81 + (1 << 30);
static const int32_t KEY_DOWN = 82 + (1 << 30);
static const int32_t KEY_BACKSPACE = '\b';
static const int32_t KEY_DELETE = '\177';

void TextBox::keyDown(Block& block, void* user_data)
{
	Lux::string s = block.getBlockText();
	char c[2];
	switch((int32_t)user_data)
	{
		case KEY_RIGHT:
			m_cursor_pos = m_cursor_pos > s.length() - 1 ? s.length() : m_cursor_pos + 1;
			break;
		case KEY_LEFT:
			m_cursor_pos = m_cursor_pos < 1 ? 0 : m_cursor_pos - 1;
			break;
		case KEY_UP:
		case KEY_DOWN:
			break;
		case '\r':
			block.emitEvent("text_accepted");
			break;
		case KEY_BACKSPACE:
			s.erase(m_cursor_pos - 1);
			if(m_cursor_pos > 0)
			{
				--m_cursor_pos;
			}
			break;
		default:  
			c[0] = (char)user_data;
			c[1] = '\0';
			s.insert(m_cursor_pos, (char)user_data);
			++m_cursor_pos;
			break;
	}
	block.setBlockText(s.c_str());
	setCursorArea();
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