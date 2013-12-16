#pragma once


#include "core/lux.h"
#include "engine/iplugin.h"
#include "gui/block.h"
#include "gui/irenderer.h"

namespace Lux
{
namespace UI
{

	class Block;


	class LUX_GUI_API Gui : public IPlugin
	{
		public:
			Gui() { m_impl = 0; }

			virtual bool create(Engine& engine) LUX_OVERRIDE;
			virtual Component createComponent(uint32_t, const Entity&) LUX_OVERRIDE;
			virtual const char* getName() const LUX_OVERRIDE { return "gui"; }
			void setRenderer(IRenderer& renderer);
			void render();
			void layout();
			Block* createTopLevelBlock(int width, int height);
			void focus(Block* block);
			void click(int x, int y);
			void keyDown(int32_t key);
			Block::EventCallback getCallback(const char* name);
			Block::EventCallback getCallback(uint32_t name_hash);
			uint32_t getCallbackNameHash(Block::EventCallback callback);
			DecoratorBase* getDecorator(const char* name);
			void addDecorator(DecoratorBase& decorator);
			void addCallback(const char* name, Block::EventCallback callback);

		private:
			struct GuiImpl* m_impl;
	};


} // ~namespace UI
} // ~namespace Lux