#pragma once


#include "gui/block.h"


namespace Lux
{
namespace UI
{


	class LUX_GUI_API TextBox : public Block
	{
		public:
			TextBox(const char* text, Gui& gui, Block* parent);
			virtual ~TextBox();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;

			void setText(const string& text);
			void setText(const char* text);
			const string& getText() const;

		private:
			void keyDown(Block& block, void* user_data);
			void focused(Block& block, void* user_data);
			void blurred(Block& block, void* user_data);
			void setCursorArea();

		private:
			int m_cursor_pos;
			Lux::UI::Block* m_cursor;
	};


} // ~namespace UI
} // ~namespace Lux