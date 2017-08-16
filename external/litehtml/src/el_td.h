#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_td : public html_tag
	{
	public:
		el_td(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_td();

		virtual void parse_attributes() override;
	};
}