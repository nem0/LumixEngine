#pragma once

namespace litehtml
{
	class html_tag;

	enum box_type
	{
		box_block,
		box_line
	};

	class box
	{
	public:
		typedef std::unique_ptr<litehtml::box>	ptr;
		typedef std::vector< box::ptr >			vector;
	protected:
		int		m_box_top;
		int		m_box_left;
		int		m_box_right;
	public:
		box(int top, int left, int right)
		{
			m_box_top	= top;
			m_box_left	= left;
			m_box_right	= right;
		}
		virtual ~box() {}

		int		bottom()	{ return m_box_top + height();	}
		int		top()		{ return m_box_top;				}
		int		right()		{ return m_box_left + width();	}
		int		left()		{ return m_box_left;			}

		virtual litehtml::box_type	get_type() = 0;
		virtual int					height() = 0;
		virtual int					width() = 0;
		virtual void				add_element(const element::ptr &el) = 0;
		virtual bool				can_hold(const element::ptr &el, white_space ws) = 0;
		virtual void				finish(bool last_box = false) = 0;
		virtual bool				is_empty() = 0;
		virtual int					baseline() = 0;
		virtual void				get_elements(elements_vector& els) = 0;
		virtual int					top_margin() = 0;
		virtual int					bottom_margin() = 0;
		virtual void				y_shift(int shift) = 0;
		virtual void				new_width(int left, int right, elements_vector& els) = 0;
	};

	//////////////////////////////////////////////////////////////////////////

	class block_box : public box
	{
		element::ptr m_element;
	public:
		block_box(int top, int left, int right) : box(top, left, right)
		{
			m_element = 0;
		}

		virtual litehtml::box_type	get_type();
		virtual int					height();
		virtual int					width();
		virtual void				add_element(const element::ptr &el);
		virtual bool				can_hold(const element::ptr &el, white_space ws);
		virtual void				finish(bool last_box = false);
		virtual bool				is_empty();
		virtual int					baseline();
		virtual void				get_elements(elements_vector& els);
		virtual int					top_margin();
		virtual int					bottom_margin();
		virtual void				y_shift(int shift);
		virtual void				new_width(int left, int right, elements_vector& els);
	};

	//////////////////////////////////////////////////////////////////////////

	class line_box : public box
	{
		elements_vector			m_items;
		int						m_height;
		int						m_width;
		int						m_line_height;
		font_metrics			m_font_metrics;
		int						m_baseline;
		text_align				m_text_align;
	public:
		line_box(int top, int left, int right, int line_height, font_metrics& fm, text_align align) : box(top, left, right)
		{
			m_height		= 0;
			m_width			= 0;
			m_font_metrics	= fm;
			m_line_height	= line_height;
			m_baseline		= 0;
			m_text_align	= align;
		}

		virtual litehtml::box_type	get_type();
		virtual int					height();
		virtual int					width();
		virtual void				add_element(const element::ptr &el);
		virtual bool				can_hold(const element::ptr &el, white_space ws);
		virtual void				finish(bool last_box = false);
		virtual bool				is_empty();
		virtual int					baseline();
		virtual void				get_elements(elements_vector& els);
		virtual int					top_margin();
		virtual int					bottom_margin();
		virtual void				y_shift(int shift);
		virtual void				new_width(int left, int right, elements_vector& els);

	private:
		bool						have_last_space();
		bool						is_break_only();
	};
}