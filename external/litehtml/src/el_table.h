#pragma once
#include "html_tag.h"

namespace litehtml
{
	struct col_info
	{
		int		width;
		bool	is_auto;
	};


	class el_table : public html_tag
	{
	public:
		el_table(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_table();

		virtual bool	appendChild(const litehtml::element::ptr& el) override;
		virtual void	parse_styles(bool is_reparse = false) override;
		virtual void	parse_attributes() override;
	};
}