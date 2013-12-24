#include "gui/controls/dockable.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/gui.h"


namespace Lux
{
namespace UI
{


Dockable::Dockable(Gui& gui, Block* parent)
	: Block(gui, parent, NULL)
{
	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_content = new Block(getGui(), this, NULL);
	m_content->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_containing_dockable = NULL;
	m_content->setIsClickable(false);
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
			ASSERT(child->getType() == crc32("dockable"));
			static_cast<Dockable*>(child)->m_containing_dockable = m_containing_dockable->m_containing_dockable;
			child->setArea(m_containing_dockable->getLocalArea());
			child->setParent(m_containing_dockable->getParent());
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
	if(&dockable == this)
	{
		return;
	}
	dockable.undock();

	Lux::UI::Block* parent = getParent();
	Dockable* new_root = new Dockable(getGui(), NULL);
	setParent(new_root->getContent());
	dockable.setParent(new_root->getContent());	
	new_root->setParent(parent);
	new_root->setIsClickable(false);
	new_root->m_containing_dockable =  m_containing_dockable;

	new_root->setArea(getLocalArea());
	switch(slot)
	{
		case TOP:
			dockable.setArea(0, 0, 0, 0, 1, 0, 0.5f, 0);
			setArea(0, 0, 0.5f, 0, 1, 0, 1, 0);
			break;
		case BOTTOM:
			dockable.setArea(0, 0, 0.5f, 0, 1, 0, 1, 0);
			setArea(0, 0, 0, 0, 1, 0, 0.5f, 0);
			break;
		case LEFT:
			dockable.setArea(0, 0, 0, 0, 0.5f, 0, 1, 0);
			setArea(0.5f, 0, 0, 0, 1, 0, 1, 0);
			break;
		case RIGHT:
			dockable.setArea(0.5f, 0, 0, 0, 1, 0, 1, 0);
			setArea(0, 0, 0, 0, 0.5f, 0, 1, 0);
			break;
	}
	
	dockable.m_containing_dockable = new_root;
	m_containing_dockable = new_root;
	new_root->layout();
}


void Dockable::startDrag()
{
	ASSERT(getType() == crc32("dockable"));
	getGui().addMouseUpCallback().bind<Dockable, &Dockable::drop>(this);
}


void Dockable::drop(int x, int y)
{
	static const uint32_t dockable_hash = crc32("dockable");

	Gui::MouseCallback cb;
	cb.bind<Dockable, &Dockable::drop>(this);
	getGui().removeMouseUpCallback(cb);

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