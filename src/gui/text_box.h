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

			void setOnTextAccepted(const char* callback);
			void setText(const string& text);
			void setText(const char* text);
			const string& getText() const;
	};


} // ~namespace UI
} // ~namespace Lux