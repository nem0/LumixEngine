#include "gui/controls/check_box.h"
#include "core/crc32.h"
#include "core/iserializer.h"


namespace Lux
{
namespace UI
{


CheckBox::CheckBox(bool is_checked, Gui& gui, Block* parent)
	: Block(gui, parent, "_check_box")
{
	m_is_checked = is_checked;
	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	onEvent("click").bind<CheckBox, &CheckBox::click>(this);
}


CheckBox::~CheckBox()
{
}


void CheckBox::click(Block& block, void*)
{
	toggle();
}


uint32_t CheckBox::getType() const
{
	static const uint32_t hash = crc32("check_box");
	return hash;
}


void CheckBox::serialize(ISerializer& serializer)
{
	Block::serializeWOChild(serializer);
	serializer.serialize("checked", m_is_checked);
}


void CheckBox::deserialize(ISerializer& serializer)
{
	Block::deserializeWOChild(serializer);
	serializer.deserialize("checked", m_is_checked);
}



} // ~namespace UI
} // ~namespace Lux