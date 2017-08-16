#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_cdata : public element
	{
		tstring	m_text;
	public:
		el_cdata(const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_cdata();

		virtual void	get_text(tstring& text) override;
		virtual void	set_data(const tchar_t* data) override;
	};
}
