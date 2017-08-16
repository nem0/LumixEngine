#pragma once

#include "html_tag.h"

namespace litehtml
{
	class el_text : public element
	{
	protected:
		tstring			m_text;
		tstring			m_transformed_text;
		size			m_size;
		text_transform	m_text_transform;
		bool			m_use_transformed;
		bool			m_draw_spaces;
	public:
		el_text(const tchar_t* text, const std::shared_ptr<litehtml::document>& doc);
		virtual ~el_text();

		virtual void				get_text(tstring& text) override;
		virtual const tchar_t*		get_style_property(const tchar_t* name, bool inherited, const tchar_t* def = 0) override;
		virtual void				parse_styles(bool is_reparse) override;
		virtual int					get_base_line() override;
		virtual void				draw(uint_ptr hdc, int x, int y, const position* clip) override;
		virtual int					line_height() const override;
		virtual uint_ptr			get_font(font_metrics* fm = 0) override;
		virtual style_display		get_display() const override;
		virtual white_space			get_white_space() const override;
		virtual element_position	get_element_position(css_offsets* offsets = 0) const override;
		virtual css_offsets			get_css_offsets() const override;

	protected:
		virtual void				get_content_size(size& sz, int max_width) override;
	};
}