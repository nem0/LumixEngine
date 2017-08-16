#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_break : public html_tag
	{
	public:
		el_break(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_break();

		virtual bool is_break() const override;
	};
}
