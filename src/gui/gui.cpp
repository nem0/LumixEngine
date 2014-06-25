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
#include "gui/decorators/box_decorator.h"
#include "gui/decorators/cursor_decorator.h"
#include "gui/decorators/check_box_decorator.h"
#include "gui/decorators/dockable_decorator.h"
#include "gui/decorators/text_decorator.h"
#include "gui/decorators/scrollbar_decorator.h"



namespace Lumix
{
namespace UI
{

	struct GuiImpl
	{
		typedef Delegate<Block* (Gui&, Block*)> BlockCreator;

		~GuiImpl();

		void menuShowSubmenu(Block& block, void*);
		void textboxKeyDown(Block& block, void*);
		void hideBlock(Block& block, void*);
		void hideParentBlock(Block& block, void*);
		void checkBoxToggle(Block& block, void*);

		Engine* m_engine;
		Array<Block*> m_blocks;
		Map<uint32_t, DecoratorBase*> m_decorators;
		Block* m_focus;
		IRenderer* m_renderer;
		Array<Atlas*> m_atlases;
		Map<uint32_t, BlockCreator> m_block_creators;
		Array<Gui::MouseMoveCallback> m_mouse_move_callbacks;
		Array<Gui::MouseCallback> m_mouse_up_callbacks;
	};


	GuiImpl::~GuiImpl()
	{
		for(int i = 0; i < m_blocks.size(); ++i)
		{
			m_blocks[i]->destroy();
		}
		m_blocks.clear();
		for(Map<uint32_t, DecoratorBase*>::iterator iter = m_decorators.begin(), end = m_decorators.end(); iter != end; ++iter)
		{
			LUMIX_DELETE(iter.second());
		}
		m_decorators.clear();
		for(int i = 0; i < m_atlases.size(); ++i)
		{
			m_atlases[i]->destroy();
			LUMIX_DELETE(m_atlases[i]);
		}
		m_atlases.clear();
	}


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
		Lumix::string s = block.getBlockText();
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
		static_cast<MenuItem&>(block).showSubMenu();
	}

	Block* createButton(Gui& gui, Block* parent)
	{
		return LUMIX_NEW(Button)("", gui, parent);
	}

	Block* createCheckBox(Gui& gui, Block* parent)
	{
		return LUMIX_NEW(CheckBox)("", gui, parent);
	}

	Block* createMenuBar(Gui& gui, Block* parent)
	{
		return LUMIX_NEW(MenuBar)(gui, parent);
	}

	Block* createMenuItem(Gui& gui, Block* parent)
	{
		MenuItem* menu_item = LUMIX_NEW(MenuItem)("", gui);
		static_cast<MenuBar*>(parent)->addItem(menu_item);
		return menu_item;
	}

	Block* createTextBox(Gui& gui, Block* parent)
	{
		return LUMIX_NEW(TextBox)("", gui, parent);
	}

	bool Gui::create(Engine& engine)
	{
		m_impl = LUMIX_NEW(GuiImpl)();
		m_impl->m_focus = NULL;
		m_impl->m_renderer = NULL;
		m_impl->m_engine = &engine;
		m_impl->m_block_creators[crc32("button")].bind<&createButton>();
		m_impl->m_block_creators[crc32("menu_item")].bind<&createMenuItem>();
		m_impl->m_block_creators[crc32("menu_bar")].bind<&createMenuBar>();
		m_impl->m_block_creators[crc32("text_box")].bind<&createTextBox>();
		m_impl->m_block_creators[crc32("check_box")].bind<&createCheckBox>();

		return true;
	}


	void Gui::destroy()
	{
		LUMIX_DELETE(m_impl);
		m_impl = NULL;
	}


	void Gui::addDecorator(DecoratorBase& decorator)
	{
		m_impl->m_decorators.insert(crc32(decorator.getName()), &decorator);
	}


	Gui::MouseMoveCallback& Gui::addMouseMoveCallback()
	{
		return m_impl->m_mouse_move_callbacks.pushEmpty();
	}


	Gui::MouseCallback& Gui::addMouseUpCallback()
	{
		return m_impl->m_mouse_up_callbacks.pushEmpty();
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
		return LUMIX_NEW(Block)(*this, parent, NULL);
	}


	Block* Gui::createGui(Lumix::FS::IFile& file)
	{
		Block* root = LUMIX_NEW(Block)(*this, NULL, NULL);
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
		Atlas* atlas = LUMIX_NEW(Atlas)();
		if(!atlas->create())
		{
			LUMIX_DELETE(atlas);
			return NULL;
		}
		m_impl->m_atlases.push(atlas);
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
		Block* block = LUMIX_NEW(Block)(*this, NULL, NULL);
		block->setArea(0, 0, 0, 0, 0, width, 0, height);
		m_impl->m_blocks.push(block);
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


	void Gui::createBaseDecorators(const char* atlas_path)
	{
		CursorDecorator* cursor_decorator = LUMIX_NEW(Lumix::UI::CursorDecorator)("_cursor");
		CheckBoxDecorator* check_box_decorator = LUMIX_NEW(CheckBoxDecorator)("_check_box");
		TextDecorator* text_decorator = LUMIX_NEW(TextDecorator)("_text");
		TextDecorator* text_centered_decorator = LUMIX_NEW(TextDecorator)("_text_centered");
		text_centered_decorator->setTextCentered(true);
		DockableDecorator* dockable_decorator = LUMIX_NEW(DockableDecorator)("_dockable");
		BoxDecorator* box_decorator = LUMIX_NEW(BoxDecorator)("_box");
		ScrollbarDecorator* scrollbar_decorator = LUMIX_NEW(ScrollbarDecorator)("_scrollbar"); 
		addDecorator(*cursor_decorator);
		addDecorator(*text_decorator);
		addDecorator(*text_centered_decorator);
		addDecorator(*box_decorator);
		addDecorator(*dockable_decorator);
		addDecorator(*scrollbar_decorator);
		addDecorator(*check_box_decorator);
		cursor_decorator->create(*this, atlas_path);
		check_box_decorator->create(*this, atlas_path);
		scrollbar_decorator->create(*this, atlas_path);
		box_decorator->create(*this, atlas_path);
		dockable_decorator->create(*this, atlas_path);
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
			m_impl->m_focus->onEvent(key_down_hash).invoke(*m_impl->m_focus, (void*)key);
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


	extern "C" LUMIX_GUI_API IPlugin* createPlugin()
	{
		return LUMIX_NEW(Gui)();
	}

} // ~namespace UI
} // ~namespace Lumix
