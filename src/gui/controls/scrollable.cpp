#include "gui/controls/scrollable.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/controls/scrollbar.h"


namespace Lux
{
namespace UI
{


Scrollable::Scrollable(Gui& gui, Block* parent)
	: Block(gui, parent, "_box")
{
	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_container = new Lux::UI::Block(gui, this, NULL);
	m_container->setIsClipping(true);
	m_container->setArea(0, 0, 0, 0, 1, 0, 1, 0);

	m_vertical_scrollbar = new Scrollbar(gui, this);
	m_vertical_scrollbar->setArea(1, -20, 0, 0, 1, 0, 1, 0); 
	m_vertical_scrollbar->hide();
	m_vertical_scrollbar->setScrollbarType(Scrollbar::VERTICAL);
	m_vertical_scrollbar->getCallback("value_changed").bind<Scrollable, &Scrollable::scollbarValueChanged>(this);

	m_horizontal_scrollbar = new Scrollbar(gui, this);
	m_horizontal_scrollbar->setArea(0, 0, 1, -20, 1, 0, 1, 0); 
	m_horizontal_scrollbar->hide();
	m_horizontal_scrollbar->getCallback("value_changed").bind<Scrollable, &Scrollable::scollbarValueChanged>(this);
}





Scrollable::~Scrollable()
{
}


uint32_t Scrollable::getType() const
{
	static const uint32_t hash = crc32("scrollable");
	return hash;
}


void Scrollable::scollbarValueChanged(Block& block, void*)
{
	layout();
}


void Scrollable::serialize(ISerializer& serializer)
{
	Block::serialize(serializer);
}


void Scrollable::deserialize(ISerializer& serializer)
{
	Block::deserialize(serializer);
}


void Scrollable::layout()
{
	Block::layout();
	if(m_container->getChildCount() > 0)
	{
		Area content_size = m_container->getChild(m_container->getChildCount()-1)->getGlobalArea();
		for(int i = m_container->getChildCount() - 2; i >= 0; --i)
		{
			content_size.merge(m_container->getChild(i)->getGlobalArea());
		}
		float container_delta_w = 0;
		float container_delta_h = 0;
		bool both_visible = true;
		if(m_container->getGlobalWidth() < content_size.right)
		{
			m_horizontal_scrollbar->show();
			container_delta_h = -m_horizontal_scrollbar->getGlobalHeight();
		}
		else
		{
			both_visible = false;
			m_horizontal_scrollbar->hide();
		}
		if(m_container->getGlobalHeight() < content_size.bottom)
		{
			m_vertical_scrollbar->show();
			container_delta_w = -m_vertical_scrollbar->getGlobalWidth();
		}
		else
		{
			both_visible = false;
			m_vertical_scrollbar->hide();
		}
		if(both_visible)
		{
			m_vertical_scrollbar->setArea(1, -20, 0, 0, 1, 0, 1, -20); 
			m_horizontal_scrollbar->setArea(0, 0, 1, -20, 1, -20, 1, 0); 
		}
		else
		{
			m_vertical_scrollbar->setArea(1, -20, 0, 0, 1, 0, 1, 0); 
			m_horizontal_scrollbar->setArea(0, 0, 1, -20, 1, 0, 1, 0); 
		}
		m_container->setArea(0, 0, 0, 0, 1, container_delta_w, 1, container_delta_h);
		m_container->layout();
		float dx = m_horizontal_scrollbar->isShown() ? m_horizontal_scrollbar->getRelativeValue() : 0;
		float dy = m_vertical_scrollbar->isShown() ? m_vertical_scrollbar->getRelativeValue() : 0;
		dx *= content_size.right - m_container->getGlobalRight();
		dy *= content_size.bottom - m_container->getGlobalBottom();
		dx = (float)(int)dx;
		dy = (float)(int)dy;
		for(int i = m_container->getChildCount() - 1; i >= 0; --i)
		{
			Area& area = m_container->getChild(i)->getGlobalArea();
			area.left -= dx;
			area.right -= dx;
			area.top -= dy;
			area.bottom -= dy;
		}
		m_vertical_scrollbar->layout();
		m_horizontal_scrollbar->layout();
	}
}


} // ~namespace UI
} // ~namespace Lux