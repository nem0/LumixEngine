#include "gui/block.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/gui.h"
#include "gui/decorator_base.h"


namespace Lux
{
namespace UI
{

	Block::Block()
	{
		m_decorator = NULL;
		m_parent = NULL;
		m_gui = NULL;
		m_tag = NULL;
	}


	void Block::create(Block* parent, DecoratorBase* decorator)
	{
		m_decorator = 0;
		m_tag = 0;
		m_local_area.top = m_local_area.bottom = m_local_area.left = m_local_area.right = 0;
		m_global_area.top = m_global_area.bottom = m_global_area.left = m_global_area.right = 0;
		m_is_shown = true;
		m_is_dirty_layout = true;
		m_parent = parent;
		m_fit_content = false;
		m_is_floating = false;
		if(m_parent)
		{
			m_gui = m_parent->m_gui;
			m_parent->addChild(*this);
		}
		m_decorator = decorator;
	}


	void Block::blur()
	{
		static const uint32_t blur_hash = crc32("blur");
		EventHandler* handler = getEventHandler(blur_hash);
		if(handler)
		{
			handler->callback(*this);
		}
		if(m_parent)
		{
			m_parent->blur();
		}
	}


	void Block::focus()
	{
		static const uint32_t blur_hash = crc32("focus");
		EventHandler* handler = getEventHandler(blur_hash);
		if(handler)
		{
			handler->callback(*this);
		}
		if(m_parent)
		{
			m_parent->focus();
		}
	}

	Block::EventHandler* Block::getEventHandler(uint32_t type)
	{
		for(int i = 0, c = m_event_handlers.size(); i < c; ++i)
		{
			if(m_event_handlers[i].type == type)
			{
				return &m_event_handlers[i];
			}
		}
		return NULL;
	}


	void Block::addChild(Block& child)
	{
		m_children.push_back(&child);
	}


	void Block::removeChild(Block& child)
	{
		for(int i = 0; i < m_children.size(); ++i)
		{
			if(m_children[i] == &child)
			{
				m_children.eraseFast(i);
				break;
			}
		}
	}


	void Block::setParent(Block* block)
	{
		if(m_parent)
		{
			m_parent->removeChild(*this);
		}
		m_parent = block;
		if(m_parent)
		{
			m_parent->addChild(*this);
		}
	}


	void Block::render(IRenderer& renderer)
	{
		if(m_is_shown)
		{
			if(m_decorator)
			{
				m_decorator->render(renderer, *this);
			}
			for(int i = 0, c = m_children.size(); i < c; ++i)
			{
				m_children[i]->render(renderer);
			}
		}
	}


	void Block::serialize(ISerializer& serializer)
	{
		serializer.serialize("decorator", m_decorator ? m_decorator->getName() : "");
		serializer.serialize("event_count", (int32_t)m_event_handlers.size());
		serializer.beginArray("events");
		for(int i = 0; i < m_event_handlers.size(); ++i)
		{
			serializer.serializeArrayItem(m_gui->getCallbackNameHash(m_event_handlers[i].callback));
			serializer.serializeArrayItem(m_event_handlers[i].type);
		}
		serializer.endArray();
		serializer.serialize("is_shown", m_is_shown);
		serializer.serialize("left", m_local_area.left);
		serializer.serialize("top", m_local_area.top);
		serializer.serialize("right", m_local_area.right);
		serializer.serialize("bottom", m_local_area.bottom);
		serializer.serialize("text", m_text.c_str());
		serializer.serialize("child_count", (int32_t)m_children.size());
		serializer.beginArray("children");
		for(int i = 0; i < m_children.size(); ++i)
		{
			m_children[i]->serialize(serializer);
		}
		serializer.endArray();
	}


	void Block::deserialize(ISerializer& serializer)
	{
		char tmp[1024];
		serializer.deserialize("decorator", tmp, 1024);
		m_decorator = m_gui->getDecorator(tmp);
		int32_t count;
		serializer.deserialize("event_count", count);
		m_event_handlers.resize(count);
		serializer.deserializeArrayBegin("events");
		for(int i = 0; i < m_event_handlers.size(); ++i)
		{
			uint32_t hash;
			serializer.deserializeArrayItem(hash);
			EventCallback callback = m_gui->getCallback(hash);
			m_event_handlers[i].callback = callback;
			serializer.deserializeArrayItem(m_event_handlers[i].type);
		}
		serializer.deserializeArrayEnd();
		serializer.deserialize("is_shown", m_is_shown);
		serializer.deserialize("left", m_local_area.left);
		serializer.deserialize("top", m_local_area.top);
		serializer.deserialize("right", m_local_area.right);
		serializer.deserialize("bottom", m_local_area.bottom);
		serializer.deserialize("text", tmp, 1024);
		m_text = tmp;
		serializer.deserialize("child_count", count);
		m_children.resize(count);
		serializer.deserializeArrayBegin("children");
		for(int i = 0; i < count; ++i)
		{
			m_children[i] = new Block();
			m_children[i]->m_parent = this;
			m_children[i]->m_gui = m_gui;
			m_children[i]->deserialize(serializer);
		}
		serializer.deserializeArrayEnd();
	}



	void Block::registerEventHandler(const char* type, void* callback)
	{
		EventHandler handler;
		handler.callback = (EventCallback)callback;
		handler.type = crc32(type);
		m_event_handlers.push_back(handler);
	}


	bool Block::click(int x, int y)
	{
		if(x > m_click_area.left && x < m_click_area.right && y > m_click_area.top && y < m_click_area.bottom && m_is_shown)
		{
			bool focused = false;
			for(int i = 0,c = m_children.size(); i < c; ++i)
			{
				focused = focused || m_children[i]->click(x, y);
			}
			bool clicked_this = false;
			if(x > m_global_area.left && x < m_global_area.right && y > m_global_area.top && y < m_global_area.bottom)
			{
				clicked_this = true;
			}
			if(clicked_this)
			{
				if(!focused)
				{
					m_gui->focus(this);
				}
				static const uint32_t click_hash = crc32("click");
				EventHandler* handler = getEventHandler(click_hash);
				if(handler)
				{
					handler->callback(*this);
				}
				return true;
			}
			return focused;
		}
		return false;
	}


	void Block::Area::merge(const Block::Area& area)
	{
		left = area.left < left ? area.left : left;
		right = area.right > right ? area.right : right;
		top = area.top < top ? area.top : top;
		bottom = area.bottom > left ? area.bottom : bottom;
	}


	void Block::layout()
	{
		m_is_dirty_layout = false;
		if(m_parent)
		{
			m_global_area.left = m_parent->m_global_area.left + m_local_area.left;
			m_global_area.right = m_parent->m_global_area.left + m_local_area.right;
			m_global_area.top = m_parent->m_global_area.top + m_local_area.top;
			m_global_area.bottom = m_parent->m_global_area.top + m_local_area.bottom;
		}
		else
		{
			m_global_area.left = m_local_area.left;
			m_global_area.right = m_local_area.right;
			m_global_area.top = m_local_area.top;
			m_global_area.bottom = m_local_area.bottom;
		}
		m_click_area = m_global_area;
		for(int i = 0, c = m_children.size(); i < c; ++i)
		{
			m_children[i]->layout();
			m_click_area.merge(m_children[i]->getClickArea());
		}
	}


} // ~namespace UI
} // ~namespace Lux