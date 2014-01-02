#include "gui/controls/tab_block.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/controls/scrollbar.h"


namespace Lux
{
namespace UI
{


TabBlock::TabBlock(Gui& gui, Block* parent)
	: Block(gui, parent, "_box")
{
	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_header = new Block(getGui(), this, NULL);
	m_header->setArea(0, 0, 0, 0, 1, 0, 0, 20);

	m_tab_content = new Block(getGui(), this, "_box");
	m_tab_content->setArea(0, 0, 0, 20, 1, 0, 1, 0);
}


TabBlock::~TabBlock()
{
}


void TabBlock::labelClick(Block& block, void*)
{
	for(int i = 0; i < m_header->getChildCount(); ++i)
	{
		if(m_header->getChild(i) == &block)
		{
			m_tab_content->getChild(i)->show();
		}
		else
		{
			m_tab_content->getChild(i)->hide();
		}
	}
}


Lux::UI::Block& TabBlock::addPage(const char* title)
{
	float x = m_header->getChildCount() > 0 ? m_header->getChild(m_header->getChildCount() - 1)->getLocalArea().right : 0; 
	Block* label = new Block(getGui(), m_header, "_text");
	label->setArea(0, x, 0, 0, 0, x + 50, 0, 20);
	label->setBlockText(title);
	label->getCallback("click").bind<TabBlock, &TabBlock::labelClick>(this);

	Block* content = new Block(getGui(), m_tab_content, NULL);
	content->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	if(m_tab_content->getChildCount() > 1)
	{
		content->hide();
	}

	layout();
	return *content;
}


Lux::UI::Block* TabBlock::getPageContent(int index)
{
	if(index < 0 || index >= m_tab_content->getChildCount())
	{
		return NULL;
	}
	return m_tab_content->getChild(index);
}


void TabBlock::setPageTitle(int index, const char* title)
{
	ASSERT(index >= 0 && index < m_header->getChildCount());
	m_header->getChild(index)->setBlockText(title);
}


const char* TabBlock::getPageTitle(int index)
{
	if(index >= 0 && index < m_header->getChildCount())
	{
		return m_header->getChild(index)->getBlockText().c_str();
	}
	return NULL;
}


void TabBlock::removePage(int index)
{
	if(index >= 0 && index < m_header->getChildCount())
	{
		m_header->getChild(index)->destroy();
		m_tab_content->getChild(index)->destroy();
		for(int i = 0; i < m_header->getChildCount(); ++i)
		{
			m_header->getChild(i)->setArea(0, i*50.0f, 0, 0, 0, i*50.0f+50, 0, 20);
		}
		layout();
	}
}


uint32_t TabBlock::getType() const
{
	static const uint32_t hash = crc32("tab_block");
	return hash;
}


void TabBlock::serialize(ISerializer& serializer)
{
	Block::serialize(serializer);
}


void TabBlock::deserialize(ISerializer& serializer)
{
	Block::deserialize(serializer);
}


} // ~namespace UI
} // ~namespace Lux