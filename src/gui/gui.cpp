#include "gui/gui.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/map.h"
#include "engine/engine.h"
#include "gui/atlas.h"
#include "gui/block.h"
#include "gui/controls/button.h"
#include "gui/controls/check_box.h"
#include "gui/controls/menu_bar.h"
#include "gui/controls/menu_item.h"
#include "gui/controls/text_box.h"
#include "gui/decorator_base.h"



namespace Lux
{
namespace UI
{

	struct GuiImpl
	{
		typedef Delegate<Block* (Gui&, Block*)> BlockCreator;

		void comboboxClick(Block& block, void*);
		void comboboxBlur(Block& block, void*);
		void menuShowSubmenu(Block& block, void*);
		void textboxKeyDown(Block& block, void*);
		void hideBlock(Block& block, void*);
		void hideParentBlock(Block& block, void*);
		void checkBoxToggle(Block& block, void*);

		Engine* m_engine;
		vector<Block*> m_blocks;
		map<uint32_t, Block::EventCallback> m_callbacks;
		map<uint32_t, DecoratorBase*> m_decorators;
		Block* m_focus;
		IRenderer* m_renderer;
		vector<Atlas*> m_atlases;
		map<uint32_t, BlockCreator> m_block_creators;
		vector<Gui::MouseMoveCallback> m_mouse_move_callbacks;
		vector<Gui::MouseCallback> m_mouse_up_callbacks;
	};


	void GuiImpl::hideBlock(Block& block, void*)
	{
		block.hide();
	}
	
	void GuiImpl::checkBoxToggle(Block& block, void*)
	{
		static_cast<CheckBox&>(block).toggle();
		block.emitEvent("check_state_changed");
	}
	
	void GuiImpl::hideParentBlock(Block& block, void*)
	{
		block.getParent()->hide();
	}


	void GuiImpl::textboxKeyDown(Block& block, void* user_data) 
	{
		Lux::string s = block.getBlockText();
		char c[2];
		switch((int32_t)user_data)
		{
			case '\r':
				block.emitEvent("text_accepted");
				break;
			case '\b':
				s = s.substr(0, s.length() - 1);
				break;
			default:			
				c[0] = (char)user_data;
				c[1] = '\0';
				s += c;
				break;
		}
		block.setBlockText(s.c_str());
	}

	void GuiImpl::menuShowSubmenu(Block& block, void*)
	{
		static_cast<Lux::UI::MenuItem&>(block).showSubMenu();
	}


	void GuiImpl::comboboxBlur(Block& block, void*)
	{
		block.hide();
	}


	void GuiImpl::comboboxClick(Block& block, void*)
	{
		Lux::UI::Block* popup = block.getParent()->getChild(1);
		if(popup->isShown())
		{
			popup->hide();					
		}
		else
		{
			popup->show();
			popup->getGui().focus(popup);
		}
		block.getGui().layout();
	}

	Block* createButton(Gui& gui, Block* parent)
	{
		return new Button("", gui, parent);
	}

	Block* createCheckBox(Gui& gui, Block* parent)
	{
		return new CheckBox("", gui, parent);
	}

	Block* createMenuBar(Gui& gui, Block* parent)
	{
		return new MenuBar(gui, parent);
	}

	Block* createMenuItem(Gui& gui, Block* parent)
	{
		MenuItem* menu_item = new MenuItem("", gui);
		static_cast<MenuBar*>(parent)->addItem(menu_item);
		return menu_item;
	}

	Block* createTextBox(Gui& gui, Block* parent)
	{
		return new TextBox("", gui, parent);
	}

	bool Gui::create(Engine& engine)
	{
		m_impl = new GuiImpl();
		m_impl->m_focus = NULL;
		m_impl->m_renderer = NULL;
		m_impl->m_engine = &engine;
		getCallback("_cb_click").bind<GuiImpl, &GuiImpl::comboboxClick>(m_impl);
		getCallback("_cb_blur").bind<GuiImpl, &GuiImpl::comboboxBlur>(m_impl);
		getCallback("_menu_show_submenu").bind<GuiImpl, &GuiImpl::menuShowSubmenu>(m_impl);
		getCallback("_tb_key_down").bind<GuiImpl, &GuiImpl::textboxKeyDown>(m_impl);
		getCallback("_hide").bind<GuiImpl, &GuiImpl::hideBlock>(m_impl);
		getCallback("_hide_parent").bind<GuiImpl, &GuiImpl::hideParentBlock>(m_impl);
		getCallback("_checkbox_toggle").bind<GuiImpl, &GuiImpl::checkBoxToggle>(m_impl);
		m_impl->m_block_creators[crc32("button")].bind<&createButton>();
		m_impl->m_block_creators[crc32("menu_item")].bind<&createMenuItem>();
		m_impl->m_block_creators[crc32("menu_bar")].bind<&createMenuBar>();
		m_impl->m_block_creators[crc32("text_box")].bind<&createTextBox>();
		m_impl->m_block_creators[crc32("check_box")].bind<&createCheckBox>();

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


	Block::EventCallback& Gui::getCallback(uint32_t name_hash)
	{
		return m_impl->m_callbacks[name_hash];
	}


	void Gui::addDecorator(DecoratorBase& decorator)
	{
		m_impl->m_decorators.insert(crc32(decorator.getName()), &decorator);
	}


	Block::EventCallback& Gui::getCallback(const char* name)
	{
		return m_impl->m_callbacks[crc32(name)];
	}


	Gui::MouseMoveCallback& Gui::addMouseMoveCallback()
	{
		return m_impl->m_mouse_move_callbacks.push_back_empty();
	}


	Gui::MouseCallback& Gui::addMouseUpCallback()
	{
		return m_impl->m_mouse_up_callbacks.push_back_empty();
	}


	void Gui::removeMouseMoveCallback(MouseMoveCallback& callback)
	{
		for(int i = m_impl->m_mouse_move_callbacks.size() - 1; i >= 0; --i)
		{
			if(m_impl->m_mouse_move_callbacks[i] == callback)
			{
				m_impl->m_mouse_move_callbacks.eraseFast(i);
				return;
			}
		}
	}


	void Gui::removeMouseUpCallback(MouseCallback& callback)
	{
		for(int i = m_impl->m_mouse_up_callbacks.size() - 1; i >= 0; --i)
		{
			if(m_impl->m_mouse_up_callbacks[i] == callback)
			{
				m_impl->m_mouse_up_callbacks.eraseFast(i);
				return;
			}
		}
	}


	void Gui::mouseDown(int x, int y)
	{
		for(int i = 0; i < m_impl->m_blocks.size(); ++i)
		{
			if(m_impl->m_blocks[i]->mouseDown(x, y))
			{
				return;
			}
		}
	}


	void Gui::mouseMove(int x, int y, int rel_x, int rel_y)
	{
		for(int i = m_impl->m_mouse_move_callbacks.size() - 1; i >= 0; --i)
		{
			m_impl->m_mouse_move_callbacks[i].invoke(x, y, rel_x, rel_y);
		}
	}


	Block* Gui::getBlock(int x, int y)
	{
		float fx = (float)x;
		float fy = (float)y;
		for(int i = m_impl->m_blocks.size() - 1; i >= 0; --i)
		{
			Block* dest = m_impl->m_blocks[i]->getBlock(fx, fy);
			if(dest)
			{
				return dest;
			}
		}
		return NULL;
	}


	void Gui::mouseUp(int x, int y)
	{
		for(int i = m_impl->m_mouse_up_callbacks.size() - 1; i >= 0; --i)
		{
			m_impl->m_mouse_up_callbacks[i].invoke(x, y);
		}
	}


	Block* Gui::createBlock(uint32_t type, Block* parent)
	{
		static const uint32_t block_hash = crc32("block");
		GuiImpl::BlockCreator creator;
		if(m_impl->m_block_creators.find(type, creator))
		{
			return creator.invoke(*this, parent);
		}
		ASSERT(type == block_hash);
		return new Block(*this, parent, NULL);
	}


	Block* Gui::createGui(Lux::FS::IFile& file)
	{
		Block* root = new Block(*this, NULL, NULL);
		JsonSerializer serializer(file, JsonSerializer::READ); 
		root->deserialize(serializer);
		return root;
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


	Block* Gui::createTopLevelBlock(float width, float height)
	{
		Block* block = new Block(*this, NULL, NULL);
		block->setArea(0, 0, 0, 0, 0, width, 0, height);
		m_impl->m_blocks.push_back(block);
		return block;
	}


	void Gui::setRenderer(IRenderer& renderer)
	{
		m_impl->m_renderer = &renderer;
	}


	IRenderer& Gui::getRenderer()
	{
		return *m_impl->m_renderer;
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
		for(int i = 0; i < m_impl->m_blocks.size(); ++i)
		{
			m_impl->m_renderer->beginRender(m_impl->m_blocks[i]->getGlobalWidth(), m_impl->m_blocks[i]->getGlobalHeight());
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
				handler->callback.invoke(*m_impl->m_focus, (void*)key);
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
			focus(NULL);
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