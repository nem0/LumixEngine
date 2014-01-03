#include "gui/controls/dockable.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/gui.h"


namespace Lux
{
namespace UI
{


Dockable::Dockable(Gui& gui, Block* parent)
	: Block(gui, parent, "_dockable")
{
	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_content = LUX_NEW(Block)(getGui(), this, NULL);
	m_content->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_containing_dockable = NULL;
	m_content->setIsClickable(false);
	m_divider = NULL;
	m_is_dragged = false;
}


Dockable::~Dockable()
{
}


uint32_t Dockable::getType() const
{
	static const uint32_t hash = crc32("dockable");
	return hash;
}


void Dockable::serialize(ISerializer& serializer)
{
	Block::serialize(serializer);
}


void Dockable::deserialize(ISerializer& serializer)
{
	Block::deserialize(serializer);
}


void Dockable::startDrag(Block&, void*)
{
	startDrag();
}


void Dockable::undock()
{
	if(m_containing_dockable)
	{
		Dockable* top_dockable = m_containing_dockable->m_containing_dockable;
		Lux::UI::Block* parent = m_containing_dockable->getParent();
		ASSERT(m_containing_dockable->getParent());
		setParent(NULL);
		while(m_containing_dockable->getContent()->getChildCount() > 0)
		{
			Lux::UI::Block* child = m_containing_dockable->getContent()->getChild(0);
			if(child != m_containing_dockable->m_divider)
			{
				ASSERT(child->getType() == crc32("dockable"));
				static_cast<Dockable*>(child)->m_containing_dockable = m_containing_dockable->m_containing_dockable;
				child->setArea(m_containing_dockable->getLocalArea());
				child->setParent(m_containing_dockable->getParent());
			}
			else
			{
				child->destroy();
			}
		}

		m_containing_dockable->destroy();
		m_containing_dockable = NULL;
		if(top_dockable)
		{
			top_dockable->layout();
		}
		else if(parent)
		{
			parent->layout();
		}
	}
}


// dock dockable inside *this
void Dockable::dock(Dockable& dockable, Slot slot)
{
	if(&dockable == this || this == dockable.m_containing_dockable)
	{
		return;
	}
	dockable.undock();

	Lux::UI::Block* parent = getParent();
	Dockable* new_root = LUX_NEW(Dockable)(getGui(), NULL);
	setParent(new_root->getContent());
	dockable.setParent(new_root->getContent());	

	new_root->m_divider = LUX_NEW(Lux::UI::Block)(getGui(), new_root->getContent(), NULL);
	new_root->m_divider->onEvent("mouse_down").bind<Dockable, &Dockable::dividerMouseDown>(new_root);
	new_root->m_divider->setZIndex(dockable.getZIndex() + 1);
	new_root->m_divider->setBlockText("divider");

	new_root->setParent(parent);
	new_root->setIsClickable(false);
	new_root->m_containing_dockable =  m_containing_dockable;

	new_root->setArea(getLocalArea());
	switch(slot)
	{
		case TOP:
			dockable.setArea(0, 0, 0, 0, 1, 0, 0.5f, 0);
			new_root->m_divider->setArea(0, 0, 0.5f, -5, 1, 0, 0.5f, 5);
			setArea(0, 0, 0.5f, 0, 1, 0, 1, 0);
			break;
		case BOTTOM:
			dockable.setArea(0, 0, 0.5f, 0, 1, 0, 1, 0);
			new_root->m_divider->setArea(0, 0, 0.5f, -5, 1, 0, 0.5f, 5);
			setArea(0, 0, 0, 0, 1, 0, 0.5f, 0);
			break;
		case LEFT:
			dockable.setArea(0, 0, 0, 0, 0.5f, 0, 1, 0);
			new_root->m_divider->setArea(0.5f, -5, 0, 0, 0.5f, 5, 1, 0);
			setArea(0.5f, 0, 0, 0, 1, 0, 1, 0);
			break;
		case RIGHT:
			dockable.setArea(0.5f, 0, 0, 0, 1, 0, 1, 0);
			new_root->m_divider->setArea(0.5f, -5, 0, 0, 0.5f, 5, 1, 0);
			setArea(0, 0, 0, 0, 0.5f, 0, 1, 0);
			break;
	}
	
	dockable.m_containing_dockable = new_root;
	m_containing_dockable = new_root;
	new_root->layout();
}


void Dockable::dragMove(int x, int y, int, int)
{
	m_drag_x = x;
	m_drag_y = y;
}


void Dockable::startDrag()
{
	m_is_dragged = true;
	ASSERT(getType() == crc32("dockable"));
	getGui().addMouseMoveCallback().bind<Dockable, &Dockable::dragMove>(this);
	getGui().addMouseUpCallback().bind<Dockable, &Dockable::drop>(this);
}


void Dockable::dividerMouseDown(Block& block, void* user_data)
{
	getGui().addMouseMoveCallback().bind<Dockable, &Dockable::dividerMouseMove>(this);
	getGui().addMouseUpCallback().bind<Dockable, &Dockable::dividerMouseUp>(this);
}


void Dockable::dividerMouseMove(int x, int y, int rel_x, int rel_y)
{
	ASSERT(getContent()->getChildCount() == 3);
	Block::Area& area = m_divider->getLocalArea();
	Block* block_prev = m_divider != getContent()->getChild(0) ?  getContent()->getChild(0) :  getContent()->getChild(1);
	Block* block_next = m_divider != getContent()->getChild(1) && block_prev != getContent()->getChild(1) ?  getContent()->getChild(1) :  getContent()->getChild(2);
	Block::Area& area_next = block_next->getLocalArea();
	Block::Area& area_prev = block_prev->getLocalArea();
	if(area.rel_left > 0.1f)
	{
		area.left += rel_x;
		area.right += rel_x;
		if(area_prev.rel_left > 0.1f)
		{
			area_prev.left += rel_x;
			area_next.right += rel_x;
		}
		else
		{
			area_next.left += rel_x;
			area_prev.right += rel_x;
		}
	}
	else
	{
		area.top += rel_y;
		area.bottom += rel_y;
		if(area_prev.rel_top > 0.1f)
		{
			area_prev.top += rel_y;
			area_next.bottom += rel_y;
		}
		else
		{
			area_next.top += rel_y;
			area_prev.bottom += rel_y;
		}
	}

	m_divider->setArea(area);
	block_prev->setArea(area_prev);
	block_next->setArea(area_next);

	layout();
}


void Dockable::dividerMouseUp(int x, int y)
{
	Gui::MouseMoveCallback cb;
	cb.bind<Dockable, &Dockable::dividerMouseMove>(this);
	getGui().removeMouseMoveCallback(cb);
	Gui::MouseCallback cb2;
	cb2.bind<Dockable, &Dockable::dividerMouseUp>(this);
	getGui().removeMouseUpCallback(cb2);
}


void Dockable::drop(int x, int y)
{
	m_is_dragged = false;
	static const uint32_t dockable_hash = crc32("dockable");

	Gui::MouseCallback cb;
	cb.bind<Dockable, &Dockable::drop>(this);
	getGui().removeMouseUpCallback(cb);
	Gui::MouseMoveCallback cb2;
	cb2.bind<Dockable, &Dockable::dragMove>(this);
	getGui().removeMouseMoveCallback(cb2);

	Block* dest = getGui().getBlock(x, y);
	while(dest && dest->getType() != dockable_hash)
	{
		dest = dest->getParent();
	}
	if(dest && dest != this)
	{
		if(x < dest->getGlobalLeft() + dest->getGlobalWidth() * 0.25f)
		{
			static_cast<Dockable*>(dest)->dock(*this, LEFT);
		}
		else if(x > dest->getGlobalLeft() + dest->getGlobalWidth() * 0.75f)
		{
			static_cast<Dockable*>(dest)->dock(*this, RIGHT);
		}
		else if(y > (dest->getGlobalTop() + dest->getGlobalBottom()) / 2)
		{
			static_cast<Dockable*>(dest)->dock(*this, BOTTOM);
		}
		else
		{
			static_cast<Dockable*>(dest)->dock(*this, TOP);
		}
		dest->layout();
	}
}


} // ~namespace UI
} // ~namespace Lux