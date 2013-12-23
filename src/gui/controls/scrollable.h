#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{

	class Scrollbar;

	class LUX_GUI_API Scrollable : public Block
	{
		public:
			Scrollable(Gui& gui, Block* parent);
			virtual ~Scrollable();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void layout() LUX_OVERRIDE;
			Lux::UI::Block* getContainer() const { return m_container; } 

		private:
			void scollbarValueChanged(Block& block, void*);

		private:
			Scrollbar* m_horizontal_scrollbar;
			Scrollbar* m_vertical_scrollbar;
			Lux::UI::Block* m_container;
	};


} // ~namespace UI
} // ~namespace Lux