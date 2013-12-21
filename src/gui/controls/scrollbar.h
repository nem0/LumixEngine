#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{

	class LUX_GUI_API Scrollbar : public Block
	{
		public:
			enum Type
			{
				VERTICAL,
				HORIZONTAL
			};

		public:
			Scrollbar(Gui& gui, Block* parent);
			virtual ~Scrollbar();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void layout() LUX_OVERRIDE;
			Block& getSliderUI() { return *m_slider; }

			float getMin() const { return m_min; }
			float getMax() const { return m_max; }
			float getValue() const { return m_value; }
			float getStep() const { return m_step; }

			void setRange(float min, float max) { m_min = min; m_max = max; }
			void setValue(float value);
			void setStep(float step) { m_step = step; }
			Type getScrollbarType() const { return m_scrollbar_type; }
			void setScrollbarType(Type type);

		private:
			void upArrowClicked(Block& block, void*);
			void downArrowClicked(Block& block, void*);
			void sliderMouseDown(Block& block, void*);
			void sliderMouseMove(int x, int y);
			void sliderMouseUp(int x, int y);

		private:
			float m_min;
			float m_max;
			float m_value;
			float m_step;
			Lux::UI::Block* m_down_arrow;
			Lux::UI::Block* m_up_arrow;
			Lux::UI::Block* m_slider;
			Type m_scrollbar_type;
	};


} // ~namespace UI
} // ~namespace Lux

