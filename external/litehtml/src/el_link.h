#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_link : public html_tag
	{
	public:
		el_link(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_link();

	protected:
		virtual void	parse_attributes() override;
	};
}
