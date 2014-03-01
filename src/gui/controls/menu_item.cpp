#include "gui/controls/menu_item.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/controls/menu_bar.h"
#include "gui/gui.h"


namespace Lux
{
namespace UI
{


MenuItem::MenuItem(const char* label, Gui& gui)
	: Block(gui, NULL, NULL)
{
	m_label = LUX_NEW(Block)(getGui(), this, "_text");
	m_label->setArea(0, 20, 0, 2, 1, 0, 1, 0);
	m_label->setBlockText(label);

	m_sub_container = LUX_NEW(Block)(gui, this, "_box");
	m_sub_container->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_sub_container->hide();
	m_sub_container->onEvent("blur").bind<MenuItem, &MenuItem::blurSubMenu>(this);
}


MenuItem::~MenuItem()
{
}


void MenuItem::blurSubMenu(Block& block, void*)
{
	m_sub_container->hide();
}


void MenuItem::click(Block& block, void*)
{
	showSubMenu();
}


void MenuItem::showSubMenu()
{
	m_sub_container->show();
	getGui().focus(m_sub_container);
}


void MenuItem::addSubItem(MenuItem* item)
{
	onEvent("click").unbind<MenuItem, &MenuItem::click>(this);
	onEvent("click").bind<MenuItem, &MenuItem::click>(this);
	item->setArea(0, 0, 0, m_sub_container->getChildCount() * 20.0f, 1, 0, 0, 20.0f + m_sub_container->getChildCount() * 20.0f);
	item->setParent(m_sub_container);
	m_sub_container->setZIndex(getZIndex() + 1);
	m_sub_container->setArea(0, 0, 0, 19, 0, 150, 0, 20.0f + 20.0f * m_sub_container->getChildCount());
}


uint32_t MenuItem::getType() const
{
	static const uint32_t hash = crc32("menu_item");
	return hash;
}


void MenuItem::serialize(ISerializer& serializer)
{
	Block::serializeWOChild(serializer);
	serializer.serialize("label", m_label->getBlockText().c_str());
}


void MenuItem::deserialize(ISerializer& serializer)
{
	char tmp[256];
	Block::deserializeWOChild(serializer);
	serializer.deserialize("label", tmp, 256);
	m_label->setBlockText(tmp);
}


} // ~namespace UI
} // ~namespace Lux