#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_font : public html_tag
	{
	public:
		el_font(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_font();

		virtual void parse_attributes() override;
	};
}