#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{

	class MenuBar;

	class LUX_GUI_API MenuItem : public Block
	{
		public:
			MenuItem(const char* label, Gui& gui);
			virtual ~MenuItem();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;

			void addSubItem(MenuItem* item);
			void showSubMenu();
		private:
			Lux::UI::Block* m_sub_container;
			Lux::UI::Block* m_label;
	};


} // ~namespace UI
} // ~namespace Lux