#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{


	class LUX_GUI_API Button : public Block
	{
		public:
			Button(const char* label, Gui& gui, Block* parent);
			virtual ~Button();
			virtual uint32_t getType() const override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;

		private:
	};


} // ~namespace UI
} // ~namespace Lux