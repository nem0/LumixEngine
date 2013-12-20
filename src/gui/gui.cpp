#include "gui/gui.h"
#include "core/crc32.h"
#include "core/map.h"
#include "engine/engine.h"
#include "gui/atlas.h"
#include "gui/decorator_base.h"
#include "gui/block.h"


namespace Lux
{
namespace UI
{

	struct GuiImpl
	{
		static void comboboxClick(Block& block);
		static void comboboxBlur(Block& block);
		static void menuShowSubmenu(Block& block);
		static void textboxKeyDown(int32_t key, Block& block);
		static void hideBlock(Block& block);
		static void hideParentBlock(Block& block);

		Engine* m_engine;
		vector<Block*> m_blocks;
		map<uint32_t, Block::EventCallback> m_callbacks;
		map<uint32_t, DecoratorBase*> m_decorators;
		Block* m_focus;
		IRenderer* m_renderer;
		vector<Atlas*> m_atlases;
	};


	void GuiImpl::hideBlock(Block& block)
	{
		block.hide();
	}
	
	
	void GuiImpl::hideParentBlock(Block& block)
	{
		block.getParent()->hide();
	}


	void GuiImpl::textboxKeyDown(int32_t key, Block& block) 
	{
		Lux::string s = block.getText();
		char c[2];
		switch(key)
		{
			case '\r':
				block.emitEvent("text_accepted");
				break;
			case '\b':
				s = s.substr(0, s.length() - 1);
				break;
			default:			
				c[0] = (char)key;
				c[1] = '\0';
				s += c;
				break;
		}
		block.setText(s.c_str());
	}

	void GuiImpl::menuShowSubmenu(Block& block)
	{
		if(block.getChild(1))
		{
			block.getChild(1)->show();
			block.getChild(1)->getGui()->focus(block.getChild(1));
		}
	}


	void GuiImpl::comboboxBlur(Block& block)
	{
		block.hide();
	}


	void GuiImpl::comboboxClick(Block& block)
	{
		Lux::UI::Block* popup = block.getParent()->getChild(1);
		if(popup->isShown())
		{
			popup->hide();					
		}
		else
		{
			popup->show();
			popup->getGui()->focus(popup);
		}
		block.getGui()->layout();
	}


	bool Gui::create(Engine& engine)
	{
		m_impl = new GuiImpl();
		m_impl->m_focus = NULL;
		m_impl->m_renderer = NULL;
		m_impl->m_engine = &engine;
		addCallback("_cb_click", &GuiImpl::comboboxClick);
		addCallback("_cb_blur", &GuiImpl::comboboxBlur);
		addCallback("_menu_show_submenu", &GuiImpl::menuShowSubmenu);
		addCallback("_tb_key_down", (Block::EventCallback)&GuiImpl::textboxKeyDown);
		addCallback("_hide", &GuiImpl::hideBlock);
		addCallback("_hide_parent", &GuiImpl::hideParentBlock);

		return true;
	}


	uint32_t Gui::getCallbackNameHash(Block::EventCallback callback)
	{
		map<uint32_t, Block::EventCallback>::iterator iter = m_impl->m_callbacks.begin(), end = m_impl->m_callbacks.end();
		while(iter != end)
		{
			if(iter.second() == callback)
			{
				return iter.first();
			}
			++ iter;
		}
		return 0;
	}


	Block::EventCallback Gui::getCallback(const char* name)
	{
		Block::EventCallback callback = NULL;
		m_impl->m_callbacks.find(crc32(name), callback);
		return callback;
	}


	Block::EventCallback Gui::getCallback(uint32_t name_hash)
	{
		Block::EventCallback callback = NULL;
		m_impl->m_callbacks.find(name_hash, callback);
		return callback;
	}


	void Gui::addDecorator(DecoratorBase& decorator)
	{
		m_impl->m_decorators.insert(crc32(decorator.getName()), &decorator);
	}


	void Gui::addCallback(const char* name, Block::EventCallback callback)
	{
		m_impl->m_callbacks[crc32(name)] =  callback;
	}


	Atlas* Gui::loadAtlas(const char* path)
	{
		for(int i = 0; i < m_impl->m_atlases.size(); ++i)
		{
			if(m_impl->m_atlases[i]->getPath() == path)
			{
				return m_impl->m_atlases[i];
			}
		}
		Atlas* atlas = new Atlas();
		if(!atlas->create())
		{
			delete atlas;
			return NULL;
		}
		m_impl->m_atlases.push_back(atlas);
		atlas->load(*m_impl->m_renderer, m_impl->m_engine->getFileSystem(), path);
		return atlas;
	}


	DecoratorBase* Gui::getDecorator(const char* name)
	{
		DecoratorBase* decorator = NULL;
		if(m_impl->m_decorators.find(crc32(name), decorator))
		{
			return decorator;
		}
		else
		{
			return NULL;
		}
	}


	Component Gui::createComponent(uint32_t type, const Entity& entity)
	{
		return Component::INVALID;
	}


	Block* Gui::createBlock(Block* parent, const char* decorator)
	{
		return new Block(*this, parent, decorator);
	}


	Block* Gui::createTopLevelBlock(float width, float height)
	{
		Block* block = createBlock(NULL, NULL);
		block->setArea(0, 0, 0, 0, 0, width, 0, height);
		m_impl->m_blocks.push_back(block);
		return block;
	}


	void Gui::setRenderer(IRenderer& renderer)
	{
		m_impl->m_renderer = &renderer;
	}

	
	void Gui::layout()
	{
		for(int i = 0; i < m_impl->m_blocks.size(); ++i)
		{
			m_impl->m_blocks[i]->layout();
		}
	}

	
	void Gui::render()
	{
		m_impl->m_renderer->beginRender();
		for(int i = 0; i < m_impl->m_blocks.size(); ++i)
		{
			m_impl->m_blocks[i]->render(*m_impl->m_renderer);
		}
	}


	void Gui::keyDown(int32_t key)
	{
		static const uint32_t key_down_hash = crc32("key_down");
		if(m_impl->m_focus)
		{
			Lux::UI::Block::EventHandler* handler = m_impl->m_focus->getEventHandler(key_down_hash);
			if(handler)
			{
				handler->key_callback(key, *m_impl->m_focus);
			}
		}
	}


	bool Gui::click(int x, int y)
	{
		bool focused = false;
		for(int i = 0; i < m_impl->m_blocks.size(); ++i)
		{
			focused = focused || m_impl->m_blocks[i]->click(x, y);
		}
		if(!focused)
		{
			m_impl->m_focus = NULL;
		}
		return focused;
	}


	Block* Gui::getFocusedBlock() const
	{
		return m_impl->m_focus;
	}


	void Gui::focus(Block* block)
	{
		if(m_impl->m_focus)
		{
			if(block)
			{
				block->setFocusProcessing();
			}
			m_impl->m_focus->blur();
		}
		m_impl->m_focus = block;
		if(block)
		{
			block->focus();
		}
	}


	extern "C" LUX_GUI_API IPlugin* createPlugin()
	{
		return new Gui();
	}

} // ~namespace UI
} // ~namespace Lux