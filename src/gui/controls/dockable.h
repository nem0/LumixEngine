#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{

	class Scrollbar;

	class LUX_GUI_API Dockable : public Block
	{
		public:
			enum Slot
			{
				LEFT,
				TOP,
				RIGHT,
				BOTTOM,
				SLOT_COUNT,
				NONE
			};

		public:
			Dockable(Gui& gui, Block* parent);
			virtual ~Dockable();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;

			void startDrag();
			Block* getContent() const { return m_content; }
			void startDrag(Block&, void*);
			void dock(Dockable& dockable, Slot slot);
			void undock();

		private:
			void drop(int x, int y); 
			void dividerMouseDown(Block& block, void* user_data); 
			void dividerMouseMove(int x, int y, int rel_x, int rel_y); 
			void dividerMouseUp(int x, int y); 

		private:
			Block* m_content;
			Block* m_divider;
			Dockable* m_containing_dockable;
	};


} // ~namespace UI
} // ~namespace Lux