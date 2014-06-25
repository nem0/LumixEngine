#pragma once


#include "gui/block.h"


namespace Lumix
{
namespace UI
{

	class Scrollbar;

	class LUMIX_GUI_API Dockable : public Block
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
			virtual uint32_t getType() const override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;

			void startDrag();
			Block* getContent() const { return m_content; }
			void startDrag(Block&, void*);
			void dock(Dockable& dockable, Slot slot);
			void undock();
			bool isDragged() const { return m_is_dragged; }
			int getDragX() const { return m_drag_x; }
			int getDragY() const { return m_drag_y; }
			Dockable* getContainingDockable() const { return m_containing_dockable; }

		private:
			void dragMove(int x, int y, int, int);
			void drop(int x, int y); 
			void dividerMouseDown(Block& block, void* user_data); 
			void dividerMouseMove(int x, int y, int rel_x, int rel_y); 
			void dividerMouseUp(int x, int y); 

		private:
			Block* m_content;
			Block* m_divider;
			Dockable* m_containing_dockable;
			bool m_is_dragged;
			int m_drag_x;
			int m_drag_y;
	};


} // ~namespace UI
} // ~namespace Lumix
