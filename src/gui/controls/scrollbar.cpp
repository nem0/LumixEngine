#include "gui/controls/scrollbar.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "core/math_utils.h"
#include "gui/gui.h"


namespace Lumix
{
namespace UI
{


Scrollbar::Scrollbar(Gui& gui, Block* parent)
	: Block(gui, parent, "_scrollbar")
{
	m_min = 0;
	m_max = 100;
	m_value = 0;
	m_step = 1;
	setArea(0, 0, 0, 0, 1, 0, 0, 20);
	m_down_arrow = LUMIX_NEW(Block)(getGui(), this, NULL);
	m_down_arrow->onEvent("click").bind<Scrollbar, &Scrollbar::downArrowClicked>(this);
	m_down_arrow->setArea(1, -20, 0, 0, 1, 0, 0, 20);
	m_up_arrow = LUMIX_NEW(Block)(getGui(), this, NULL);
	m_up_arrow->onEvent("click").bind<Scrollbar, &Scrollbar::upArrowClicked>(this);
	m_up_arrow->setArea(0, 0, 0, 0, 0, 20, 0, 20);
	m_slider = LUMIX_NEW(Block)(getGui(), this, NULL);
	m_slider->setArea(0, 20, 0, 0, 0, 40, 0, 20);
	m_slider->onEvent("mouse_down").bind<Scrollbar, &Scrollbar::sliderMouseDown>(this);
	setScrollbarType(HORIZONTAL);
}


void Scrollbar::sliderMouseDown(Block& block, void*)
{
	getGui().addMouseMoveCallback().bind<Scrollbar, &Scrollbar::sliderMouseMove>(this);
	getGui().addMouseUpCallback().bind<Scrollbar, &Scrollbar::sliderMouseUp>(this);
}


void Scrollbar::sliderMouseMove(int x, int y, int, int)
{
	if(m_scrollbar_type == VERTICAL)
	{
		float y_clamped = Lumix::Math::clamp((float)y, m_up_arrow->getGlobalBottom(), m_down_arrow->getGlobalTop());
		setValue(m_min + (y_clamped - m_up_arrow->getGlobalBottom()) / (m_down_arrow->getGlobalTop() - m_up_arrow->getGlobalBottom()) * (m_max - m_min));
	}
	else
	{
		float x_clamped = Lumix::Math::clamp((float)x, m_up_arrow->getGlobalRight(), m_down_arrow->getGlobalLeft());
		setValue(m_min + (x_clamped - m_up_arrow->getGlobalRight()) / (m_down_arrow->getGlobalLeft() - m_up_arrow->getGlobalRight()) * (m_max - m_min));
	}
}


void Scrollbar::sliderMouseUp(int x, int y)
{
	Gui::MouseCallback cb;
	cb.bind<Scrollbar, &Scrollbar::sliderMouseUp>(this);
	getGui().removeMouseUpCallback(cb);
	Gui::MouseMoveCallback cb2;
	cb2.bind<Scrollbar, &Scrollbar::sliderMouseMove>(this);
	getGui().removeMouseMoveCallback(cb2);
}



Scrollbar::~Scrollbar()
{
}


void Scrollbar::setScrollbarType(Type type)
{
	m_scrollbar_type = type;
	setValue(m_value);
}


void Scrollbar::layout()
{
	if(m_scrollbar_type == VERTICAL)
	{
		m_down_arrow->setArea(0, 0, 1, -20, 1, 0, 1, 0);
	}
	else
	{
		m_down_arrow->setArea(1, -20, 0, 0, 1, 0, 1, 0);
	}
	Block::layout();
	setValue(m_value);
}


void Scrollbar::upArrowClicked(Block& block, void*)
{
	float val = m_value - m_step;
	val = val < m_min ? m_min : val;
	setValue(val);
}


void Scrollbar::setValue(float value)
{
	float old_value = m_value;
	m_value = value;
	float t =(m_value - m_min) / (m_max - m_min);
	if(m_scrollbar_type == VERTICAL)
	{
		float l = getGlobalBottom() - getGlobalTop();
		float w1 = m_up_arrow->getGlobalRight() - m_up_arrow->getGlobalLeft();
		float w2 = m_down_arrow->getGlobalRight() - m_down_arrow->getGlobalLeft();
		float w3 = m_slider->getGlobalRight() - m_slider->getGlobalLeft();
		t = w1 + t * (l - w1 - w2 - w3);
		m_slider->setArea(0, 0, 0, t, 0, 20, 0, t + 20);
	}
	else
	{
		float l = getGlobalRight() - getGlobalLeft();
		float h1 = m_up_arrow->getGlobalBottom() - m_up_arrow->getGlobalTop();
		float h2 = m_down_arrow->getGlobalBottom() - m_down_arrow->getGlobalTop();
		float h3 = m_slider->getGlobalBottom() - m_slider->getGlobalTop();
		t = h1 + t * (l - h1 - h2 - h3);
		m_slider->setArea(0, t, 0, 0, 0, t + 20, 0, 20);
	}
	m_slider->layout();
	if(old_value != m_value)
	{
		emitEvent("value_changed");
	}
}


void Scrollbar::downArrowClicked(Block& block, void*)
{
	float val = m_value + m_step;
	val = val > m_max ? m_max : val;
	setValue(val);
}


uint32_t Scrollbar::getType() const
{
	static const uint32_t hash = crc32("scrollbar");
	return hash;
}


void Scrollbar::serialize(ISerializer& serializer)
{
	Block::serializeWOChild(serializer);
	serializer.serialize("min", m_min);
	serializer.serialize("max", m_max);
	serializer.serialize("value", m_value);
}


void Scrollbar::deserialize(ISerializer& serializer)
{
	Block::deserializeWOChild(serializer);
	serializer.deserialize("min", m_min);
	serializer.deserialize("max", m_max);
	serializer.deserialize("value", m_value);
}



} // ~namespace UI
} // ~namespace Lumix
