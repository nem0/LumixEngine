#pragma once


#include "gui/block.h"


namespace Lumix
{
namespace UI
{

	class MenuBar;

	class LUX_GUI_API MenuItem : public Block
	{
		public:
			MenuItem(const char* label, Gui& gui);
			virtual ~MenuItem();
			virtual uint32_t getType() const override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;

			void addSubItem(MenuItem* item);
			void showSubMenu();
		
		private:
			void click(Block& block, void*);
			void blurSubMenu(Block& block, void*);

		private:
			Lumix::UI::Block* m_sub_container;
			Lumix::UI::Block* m_label;
	};


} // ~namespace UI
} // ~namespace Lumix
