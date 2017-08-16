#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_body : public html_tag
	{
	public:
		el_body(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_body();

		virtual bool is_body() const override;
	};
}