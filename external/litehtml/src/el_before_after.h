#pragma once
#include "html_tag.h"

namespace litehtml
{
	class el_before_after_base : public html_tag
	{
	public:
		el_before_after_base(const std::shared_ptr<litehtml::document>& doc, bool before);
		virtual ~el_before_after_base();

		virtual void add_style(const litehtml::style& st) override;
		virtual void apply_stylesheet(const litehtml::css& stylesheet) override;
	private:
		void	add_text(const tstring& txt);
		void	add_function(const tstring& fnc, const tstring& params);
		tchar_t convert_escape(const tchar_t* txt);
	};

	class el_before : public el_before_after_base
	{
	public:
		el_before(const std::shared_ptr<litehtml::document>& doc) : el_before_after_base(doc, true)
		{

		}
	};

	class el_after : public el_before_after_base
	{
	public:
		el_after(const std::shared_ptr<litehtml::document>& doc) : el_before_after_base(doc, false)
		{

		}
	};
}