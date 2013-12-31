#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{


	class LUX_GUI_API CheckBox : public Block
	{
		public:
			CheckBox(bool is_checked, Gui& gui, Block* parent);
			virtual ~CheckBox();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;

			void toggle() { m_is_checked = !m_is_checked; }
			void setIsChecked(bool is_checked) { m_is_checked = is_checked; }
			bool isChecked() const { return m_is_checked; }
		private:
			bool m_is_checked;
	};


} // ~namespace UI
} // ~namespace Lux