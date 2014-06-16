#pragma once


#include "core/string.h"
#include "gui/block.h"


namespace Lumix
{
namespace UI
{

	class LUX_GUI_API TabBlock : public Block
	{
		public:
			TabBlock(Gui& gui, Block* parent);
			virtual ~TabBlock();
			virtual uint32_t getType() const override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;

			Lumix::UI::Block& addPage(const char* title);
			Lumix::UI::Block* getPageContent(int index);
			void setPageTitle(int index, const char* title);
			const char* getPageTitle(int index);
			void removePage(int index);

		private:
			void labelClick(Block& block, void*);

		private:
			Lumix::UI::Block* m_header;
			Lumix::UI::Block* m_tab_content;
	};


} // ~namespace UI
} // ~namespace Lumix
