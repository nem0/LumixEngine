#include "gui/block.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "gui/gui.h"
#include "gui/decorator_base.h"


namespace Lux
{
namespace UI
{

	Block::Block(Gui& gui, Block* parent, const char* decorator_name)
		: m_gui(gui)
	{
		m_tag = NULL;
		m_z = 0;
		m_is_clipping = false;
		m_is_mouse_clickable = true;
		m_local_area.top = m_local_area.bottom = m_local_area.left = m_local_area.right = 0;
		m_global_area.top = m_global_area.bottom = m_global_area.left = m_global_area.right = 0;
		m_global_area.rel_top = m_global_area.rel_bottom = m_global_area.rel_left = m_global_area.rel_right = 0;
		m_is_shown = true;
		m_is_dirty_layout = true;
		m_is_focus_processing = false;
		m_parent = parent;
		m_fit_content = false;
		m_is_floating = false;
		if(m_parent)
		{
			m_gui = m_parent->m_gui;
			m_parent->addChild(*this);
			m_z = parent->m_z;
		}
		m_decorator = decorator_name ? m_gui.getDecorator(decorator_name) : NULL;
	}


	void Block::destroy()
	{
		setParent(NULL);
		delete this;
	}


	Block::~Block()
	{
		if(m_gui.getFocusedBlock() == this)
		{
			m_gui.focus(NULL);
		}
		for(int i = 0; i < m_children.size(); ++i)
		{
			delete m_children[i];
		}
	}


	uint32_t Block::getType() const
	{
		static const uint32_t hash = crc32("block");
		return hash;
	}


	int Block::getZIndex() const
	{
		return (int)(m_z * 100);
	}

	void Block::setZIndex(int z_index)
	{
		m_z = z_index / 100.0f;
		if(m_parent)
		{
			for(int i = 0; i < m_parent->m_children.size() - 1; ++i)
			{
				if(m_parent->m_children[i]->getZ() < m_parent->m_children[i+1]->getZ())
				{
					Lux::UI::Block* tmp = m_parent->m_children[i];
					m_parent->m_children[i] = m_parent->m_children[i+1];
					m_parent->m_children[i+1] = tmp;
				}
			}
			for(int i = m_parent->m_children.size() - 1; i > 0; --i)
			{
				if(m_parent->m_children[i]->getZ() > m_parent->m_children[i-1]->getZ())
				{
					Lux::UI::Block* tmp = m_parent->m_children[i];
					m_parent->m_children[i] = m_parent->m_children[i-1];
					m_parent->m_children[i-1] = tmp;
				}
			}
		}
		for(int i = 0; i < m_children.size(); ++i)
		{
			m_children[i]->setZIndex(z_index);
		}
	}


	void Block::setArea(const Area& area)
	{
		m_local_area = area;
	}


	void Block::setArea(float rel_left, float left, float rel_top, float top, float rel_right, float right, float rel_bottom, float bottom)
	{
		ASSERT(rel_left >= 0 && rel_top >= 0);
		m_local_area.rel_left = rel_left;
		m_local_area.left = left;
		m_local_area.rel_top = rel_top;
		m_local_area.top = top;
		m_local_area.rel_right = rel_right;
		m_local_area.right = right;
		m_local_area.rel_bottom = rel_bottom;
		m_local_area.bottom = bottom;
		m_is_dirty_layout = true;
	}


	void Block::blur()
	{
		if(!m_is_focus_processing)
		{
			static const uint32_t blur_hash = crc32("blur");
			EventHandler* handler = getEventHandler(blur_hash);
			if(handler)
			{
				handler->callback.invoke(*this, NULL);
			}
			if(m_parent)
			{
				m_parent->blur();
			}
		}
	}


	void Block::focus()
	{
		m_is_focus_processing = false;
		static const uint32_t blur_hash = crc32("focus");
		EventHandler* handler = getEventHandler(blur_hash);
		if(handler)
		{
			handler->callback.invoke(*this, NULL);
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
		for(int i = m_children.size() - 1; i > 0; --i)
		{
			if(m_children[i]->getZ() < m_children[i-1]->getZ())
			{
				Lux::UI::Block* tmp = m_children[i];
				m_children[i] = m_children[i-1];
				m_children[i-1] = tmp;
			}
			else
			{
				break;
			}
		}
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


	Block* Block::getBlock(float x, float y)
	{
		if(x > m_content_area.left && x < m_content_area.right && y > m_content_area.top && y < m_content_area.bottom && m_is_shown)
		{
			for(int i = 0; i < m_children.size(); ++i)
			{
				Block* dest = m_children[i]->getBlock(x, y);
				if(dest)
				{
					return dest;
				}
			}
			if(x > m_global_area.left && x < m_global_area.right && y > m_global_area.top && y < m_global_area.bottom)
			{
				return this;
			}
		}
		return NULL;
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
			m_z = m_parent->m_z;
			m_parent->addChild(*this);
		}
	}


	void Block::render(IRenderer& renderer)
	{
		if(m_is_shown)
		{
			if(m_is_clipping)
			{
				renderer.pushScissorArea(m_global_area.left, m_global_area.top, m_global_area.right, m_global_area.bottom);
			}
			if(m_decorator)
			{
				m_decorator->render(renderer, *this);
			}
			for(int i = 0, c = m_children.size(); i < c; ++i)
			{
				m_children[i]->render(renderer);
			}
			if(m_is_clipping)
			{
				renderer.popScissorArea();
			}
		}
	}


	void Block::serializeWOChild(ISerializer& serializer)
	{
		serializer.serialize("decorator", m_decorator ? m_decorator->getName() : "");
		serializer.serialize("event_count", (int32_t)m_event_handlers.size());
		serializer.beginArray("events");
		for(int i = 0; i < m_event_handlers.size(); ++i)
		{
			serializer.serializeArrayItem(m_gui.getCallbackNameHash(m_event_handlers[i].callback));
			serializer.serializeArrayItem(m_event_handlers[i].type);
		}
		serializer.endArray();
		serializer.serialize("is_shown", m_is_shown);
		serializer.serialize("left", m_local_area.left);
		serializer.serialize("top", m_local_area.top);
		serializer.serialize("right", m_local_area.right);
		serializer.serialize("bottom", m_local_area.bottom);
		serializer.serialize("text", m_text.c_str());
	}


	void Block::deserializeWOChild(ISerializer& serializer)
	{
		char tmp[1024];
		serializer.deserialize("decorator", tmp, 1024);
		m_decorator = m_gui.getDecorator(tmp);
		int32_t count;
		serializer.deserialize("event_count", count);
		m_event_handlers.resize(count);
		serializer.deserializeArrayBegin("events");
		for(int i = 0; i < m_event_handlers.size(); ++i)
		{
			uint32_t hash;
			serializer.deserializeArrayItem(hash);
			EventCallback callback = m_gui.getCallback(hash);
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
	}


	void Block::serialize(ISerializer& serializer)
	{
		serializeWOChild(serializer);
		serializer.serialize("child_count", (int32_t)m_children.size());
		serializer.beginArray("children");
		for(int i = 0; i < m_children.size(); ++i)
		{
			serializer.serializeArrayItem(m_children[i]->getType());
			m_children[i]->serialize(serializer);
		}
		serializer.endArray();
	}


	void Block::deserialize(ISerializer& serializer)
	{
		deserializeWOChild(serializer);
		int32_t count;
		serializer.deserialize("child_count", count);
		m_children.reserve(count);
		serializer.deserializeArrayBegin("children");
		for(int i = 0; i < count; ++i)
		{
			uint32_t type;
			serializer.deserializeArrayItem(type);
			m_children.push_back(m_gui.createBlock(type, this));
			m_children[i]->deserialize(serializer);
		}
		serializer.deserializeArrayEnd();
	}


	void Block::emitEvent(const char* type)
	{
		uint32_t hash = crc32(type);
		for(int i = 0, c = m_event_handlers.size(); i < c; ++i)
		{
			if(m_event_handlers[i].type == hash)
			{
				m_event_handlers[i].callback.invoke(*this, NULL);
			}
		}
	}


	Block::EventCallback& Block::getCallback(const char* type)
	{
		return getCallback(crc32(type));
	}


	Block::EventCallback& Block::getCallback(uint32_t type)
	{
		EventHandler handler;
		handler.type = type;
		m_event_handlers.push_back(handler);
		return m_event_handlers[m_event_handlers.size()-1].callback;
	}


	void Block::registerEventHandler(const char* type, const char* callback)
	{
		EventHandler handler;
		handler.callback = m_gui.getCallback(callback);
		handler.type = crc32(type);
		m_event_handlers.push_back(handler);
	}


	bool Block::mouseDown(int x, int y)
	{
		if(x > m_content_area.left && x < m_content_area.right && y > m_content_area.top && y < m_content_area.bottom && m_is_shown)
		{
			for(int i = 0,c = m_children.size(); i < c; ++i)
			{
				m_children[i]->mouseDown(x, y);
			}
			if(m_is_mouse_clickable)
			{
				emitEvent("mouse_down");
			}
			return true;
		}
		return false;
	}


	bool Block::click(int x, int y)
	{
		if(x > m_content_area.left && x < m_content_area.right && y > m_content_area.top && y < m_content_area.bottom && m_is_shown)
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
			if(clicked_this && m_is_mouse_clickable)
			{
				if(!focused)
				{
					m_gui.focus(this);
				}
				emitEvent("click");
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
		bottom = area.bottom > bottom ? area.bottom : bottom;
	}


	float round(float value)
	{
		return (float)(int)value;
	}


	void Block::layout()
	{
		m_is_dirty_layout = false;
		if(m_parent)
		{
			m_global_area.left = round(m_parent->m_global_area.left + m_local_area.left + m_local_area.rel_left * (m_parent->m_global_area.right - m_parent->m_global_area.left));
			m_global_area.right = round(m_parent->m_global_area.left + m_local_area.right + m_local_area.rel_right * (m_parent->m_global_area.right - m_parent->m_global_area.left));
			m_global_area.top = round(m_parent->m_global_area.top + m_local_area.top + m_local_area.rel_top * (m_parent->m_global_area.bottom - m_parent->m_global_area.top));
			m_global_area.bottom = round(m_parent->m_global_area.top + m_local_area.bottom + m_local_area.rel_bottom * (m_parent->m_global_area.bottom - m_parent->m_global_area.top));
		}
		else
		{
			m_global_area.left = round(m_local_area.left);
			m_global_area.right = round(m_local_area.right);
			m_global_area.top = round(m_local_area.top);
			m_global_area.bottom = round(m_local_area.bottom);
		}
		m_content_area = m_global_area;
		for(int i = 0, c = m_children.size(); i < c; ++i)
		{
			m_children[i]->layout();
			if(!m_is_clipping)
			{
				m_content_area.merge(m_children[i]->getContentArea());
			}
		}
	}


} // ~namespace UI
} // ~namespace Lux