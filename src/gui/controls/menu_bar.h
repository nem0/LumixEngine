#pragma once


#include "gui/block.h"


namespace Lumix
{
namespace UI
{


	class LUX_GUI_API MenuBar : public Block
	{
		public:
			MenuBar(Gui& gui, Block* parent);
			virtual ~MenuBar();
			virtual uint32_t getType() const override;

			void addItem(class MenuItem* item);

		private:
	};


} // ~namespace UI
} // ~namespace Lumix
