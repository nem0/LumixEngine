#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_div : public html_tag
	{
	public:
		el_div(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_div();

		virtual void parse_attributes() override;
	};
}