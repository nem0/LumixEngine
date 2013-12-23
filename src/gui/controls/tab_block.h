#pragma once


#include "core/string.h"
#include "gui/block.h"


namespace Lux
{
namespace UI
{

	class LUX_GUI_API TabBlock : public Block
	{
		public:
			TabBlock(Gui& gui, Block* parent);
			virtual ~TabBlock();
			virtual uint32_t getType() const LUX_OVERRIDE;
			virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
			virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;

			Lux::UI::Block& addPage(const char* title);
			Lux::UI::Block* getPageContent(int index);
			void setPageTitle(int index, const char* title);
			const char* getPageTitle(int index);
			void removePage(int index);

		private:
			void labelClick(Block& block, void*);

		private:
			Lux::UI::Block* m_header;
			Lux::UI::Block* m_tab_content;
	};


} // ~namespace UI
} // ~namespace Lux