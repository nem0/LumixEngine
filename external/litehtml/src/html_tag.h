#pragma once

#include "element.h"
#include "style.h"
#include "background.h"
#include "css_margins.h"
#include "borders.h"
#include "css_selector.h"
#include "stylesheet.h"
#include "box.h"
#include "table.h"

namespace litehtml
{
	struct line_context
	{
		int calculatedTop;
		int top;
		int left;
		int right;

		int width()
		{
			return right - left;
		}
		void fix_top()
		{
			calculatedTop = top;
		}
	};

	class html_tag : public element
	{
		friend class elements_iterator;
		friend class el_table;
		friend class table_grid;
		friend class block_box;
		friend class line_box;
	public:
		typedef std::shared_ptr<litehtml::html_tag>	ptr;
	protected:
		box::vector				m_boxes;
		string_vector			m_class_values;
		tstring					m_tag;
		litehtml::style			m_style;
		string_map				m_attrs;
		vertical_align			m_vertical_align;
		text_align				m_text_align;
		style_display			m_display;
		list_style_type			m_list_style_type;
		list_style_position		m_list_style_position;
		white_space				m_white_space;
		element_float			m_float;
		element_clear			m_clear;
		floated_box::vector		m_floats_left;
		floated_box::vector		m_floats_right;
		elements_vector			m_positioned;
		background				m_bg;
		element_position		m_el_position;
		int						m_line_height;
		bool					m_lh_predefined;
		string_vector			m_pseudo_classes;
		used_selector::vector	m_used_styles;		
		
		uint_ptr				m_font;
		int						m_font_size;
		font_metrics			m_font_metrics;

		css_margins				m_css_margins;
		css_margins				m_css_padding;
		css_borders				m_css_borders;
		css_length				m_css_width;
		css_length				m_css_height;
		css_length				m_css_min_width;
		css_length				m_css_min_height;
		css_length				m_css_max_width;
		css_length				m_css_max_height;
		css_offsets				m_css_offsets;
		css_length				m_css_text_indent;

		overflow				m_overflow;
		visibility				m_visibility;
		int						m_z_index;
		box_sizing				m_box_sizing;

		int_int_cache			m_cahe_line_left;
		int_int_cache			m_cahe_line_right;

		// data for table rendering
		std::unique_ptr<table_grid>	m_grid;
		css_length				m_css_border_spacing_x;
		css_length				m_css_border_spacing_y;
		int						m_border_spacing_x;
		int						m_border_spacing_y;
		border_collapse			m_border_collapse;

		virtual void			select_all(const css_selector& selector, elements_vector& res);

	public:
		html_tag(const std::shared_ptr<litehtml::document>& doc);
		virtual ~html_tag();

		/* render functions */

		virtual int					render(int x, int y, int max_width, bool second_pass = false) override;

		virtual int					render_inline(const element::ptr &container, int max_width) override;
		virtual int					place_element(const element::ptr &el, int max_width) override;
		virtual bool				fetch_positioned() override;
		virtual void				render_positioned(render_type rt = render_all) override;

		int							new_box(const element::ptr &el, int max_width, line_context& line_ctx);

		int							get_cleared_top(const element::ptr &el, int line_top) const;
		int							finish_last_box(bool end_of_render = false);

		virtual bool				appendChild(const element::ptr &el) override;
		virtual bool				removeChild(const element::ptr &el) override;
		virtual void				clearRecursive() override;
		virtual const tchar_t*		get_tagName() const override;
		virtual void				set_tagName(const tchar_t* tag) override;
		virtual void				set_data(const tchar_t* data) override;
		virtual element_float		get_float() const override;
		virtual vertical_align		get_vertical_align() const override;
		virtual css_length			get_css_left() const override;
		virtual css_length			get_css_right() const override;
		virtual css_length			get_css_top() const override;
		virtual css_length			get_css_bottom() const override;
		virtual css_length			get_css_width() const override;
		virtual css_offsets			get_css_offsets() const override;
		virtual void				set_css_width(css_length& w) override;
		virtual css_length			get_css_height() const override;
		virtual element_clear		get_clear() const override;
		virtual size_t				get_children_count() const override;
		virtual element::ptr		get_child(int idx) const override;
		virtual element_position	get_element_position(css_offsets* offsets = 0) const override;
		virtual overflow			get_overflow() const override;

		virtual void				set_attr(const tchar_t* name, const tchar_t* val) override;
		virtual const tchar_t*		get_attr(const tchar_t* name, const tchar_t* def = 0) override;
		virtual void				apply_stylesheet(const litehtml::css& stylesheet) override;
		virtual void				refresh_styles() override;

		virtual bool				is_white_space() const override;
		virtual bool				is_body() const override;
		virtual bool				is_break() const override;
		virtual int					get_base_line() override;
		virtual bool				on_mouse_over() override;
		virtual bool				on_mouse_leave() override;
		virtual bool				on_lbutton_down() override;
		virtual bool				on_lbutton_up() override;
		virtual void				on_click() override;
		virtual bool				find_styles_changes(position::vector& redraw_boxes, int x, int y) override;
		virtual const tchar_t*		get_cursor() override;
		virtual void				init_font() override;
		virtual bool				set_pseudo_class(const tchar_t* pclass, bool add) override;
		virtual bool				set_class(const tchar_t* pclass, bool add) override;
		virtual bool				is_replaced() const override;
		virtual int					line_height() const override;
		virtual white_space			get_white_space() const override;
		virtual style_display		get_display() const override;
		virtual visibility			get_visibility() const override;
		virtual void				parse_styles(bool is_reparse = false) override;
		virtual void				draw(uint_ptr hdc, int x, int y, const position* clip) override;
		virtual void				draw_background(uint_ptr hdc, int x, int y, const position* clip) override;

		virtual const tchar_t*		get_style_property(const tchar_t* name, bool inherited, const tchar_t* def = 0) override;
		virtual uint_ptr			get_font(font_metrics* fm = 0) override;
		virtual int					get_font_size() const override;

		elements_vector&			children();
		virtual void				calc_outlines(int parent_width) override;
		virtual void				calc_auto_margins(int parent_width) override;

		virtual int					select(const css_selector& selector, bool apply_pseudo = true) override;
		virtual int					select(const css_element_selector& selector, bool apply_pseudo = true) override;

		virtual elements_vector		select_all(const tstring& selector) override;
		virtual elements_vector		select_all(const css_selector& selector) override;

		virtual element::ptr		select_one(const tstring& selector) override;
		virtual element::ptr		select_one(const css_selector& selector) override;

		virtual element::ptr		find_ancestor(const css_selector& selector, bool apply_pseudo = true, bool* is_pseudo = 0) override;
		virtual element::ptr		find_adjacent_sibling(const element::ptr& el, const css_selector& selector, bool apply_pseudo = true, bool* is_pseudo = 0) override;
		virtual element::ptr		find_sibling(const element::ptr& el, const css_selector& selector, bool apply_pseudo = true, bool* is_pseudo = 0) override;
		virtual void				get_text(tstring& text) override;
		virtual void				parse_attributes() override;

		virtual bool				is_first_child_inline(const element::ptr& el) const override;
		virtual bool				is_last_child_inline(const element::ptr& el) override;
		virtual bool				have_inline_child() const override;
		virtual void				get_content_size(size& sz, int max_width) override;
		virtual void				init() override;
		virtual void				get_inline_boxes(position::vector& boxes) override;
		virtual bool				is_floats_holder() const override;
		virtual int					get_floats_height(element_float el_float = float_none) const override;
		virtual int					get_left_floats_height() const override;
		virtual int					get_right_floats_height() const override;
		virtual int					get_line_left(int y) override;
		virtual int					get_line_right(int y, int def_right) override;
		virtual void				get_line_left_right(int y, int def_right, int& ln_left, int& ln_right) override;
		virtual void				add_float(const element::ptr &el, int x, int y) override;
		virtual void				update_floats(int dy, const element::ptr &parent) override;
		virtual void				add_positioned(const element::ptr &el) override;
		virtual int					find_next_line_top(int top, int width, int def_right) override;
		virtual void				apply_vertical_align() override;
		virtual void				draw_children(uint_ptr hdc, int x, int y, const position* clip, draw_flag flag, int zindex) override;
		virtual int					get_zindex() const override;
		virtual void				draw_stacking_context(uint_ptr hdc, int x, int y, const position* clip, bool with_positioned) override;
		virtual void				calc_document_size(litehtml::size& sz, int x = 0, int y = 0) override;
		virtual void				get_redraw_box(litehtml::position& pos, int x = 0, int y = 0) override;
		virtual void				add_style(const litehtml::style& st) override;
		virtual element::ptr		get_element_by_point(int x, int y, int client_x, int client_y) override;
		virtual element::ptr		get_child_by_point(int x, int y, int client_x, int client_y, draw_flag flag, int zindex) override;

		virtual bool				is_nth_child(const element::ptr& el, int num, int off, bool of_type) const override;
		virtual bool				is_nth_last_child(const element::ptr& el, int num, int off, bool of_type) const override;
		virtual bool				is_only_child(const element::ptr& el, bool of_type) const override;
		virtual const background*	get_background(bool own_only = false) override;

	protected:
		void						draw_children_box(uint_ptr hdc, int x, int y, const position* clip, draw_flag flag, int zindex);
		void						draw_children_table(uint_ptr hdc, int x, int y, const position* clip, draw_flag flag, int zindex);
		int							render_box(int x, int y, int max_width, bool second_pass = false);
		int							render_table(int x, int y, int max_width, bool second_pass = false);
		int							fix_line_width(int max_width, element_float flt);
		void						parse_background();
		void						init_background_paint( position pos, background_paint &bg_paint, const background* bg );
		void						draw_list_marker( uint_ptr hdc, const position &pos );
		void						parse_nth_child_params( tstring param, int &num, int &off );
		void						remove_before_after();
		litehtml::element::ptr		get_element_before();
		litehtml::element::ptr		get_element_after();
	};

	/************************************************************************/
	/*                        Inline Functions                              */
	/************************************************************************/

	inline elements_vector& litehtml::html_tag::children()
	{
		return m_children;
	}
}

