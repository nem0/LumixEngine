#pragma once


#include "gui/block.h"


namespace Lumix
{
namespace UI
{


	class LUMIX_GUI_API CheckBox : public Block
	{
		public:
			CheckBox(bool is_checked, Gui& gui, Block* parent);
			virtual ~CheckBox();
			virtual uint32_t getType() const override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;

			void toggle() { m_is_checked = !m_is_checked; }
			void setIsChecked(bool is_checked) { m_is_checked = is_checked; }
			bool isChecked() const { return m_is_checked; }
		
		private:
			void click(Block& block, void*);
	
		private:
			bool m_is_checked;
	};


} // ~namespace UI
} // ~namespace Lumix
