#pragma once

#include "types.h"

namespace litehtml
{
	class element;

	class iterator_selector
	{
	public:
		virtual bool select(const element::ptr& el) = 0;
	};

	class elements_iterator
	{
	private:
		struct stack_item
		{
			int				idx;
			element::ptr	el;
			stack_item()
			{

			}
			stack_item(const stack_item& val)
			{
				idx = val.idx;
				el = val.el;
			}
			stack_item(stack_item&& val)
			{
				idx = val.idx;
				el = std::move(val.el);
			}
		};

		std::vector<stack_item>		m_stack;
		element::ptr				m_el;
		int							m_idx;
		iterator_selector*			m_go_inside;
		iterator_selector*			m_select;
	public:

		elements_iterator(const element::ptr& el, iterator_selector* go_inside, iterator_selector* select)
		{ 
			m_el			= el;
			m_idx			= -1; 
			m_go_inside		= go_inside;
			m_select		= select;
		}

		~elements_iterator()
		{

		}

		element::ptr next(bool ret_parent = true);
	
	private:
		void next_idx();
	};

	class go_inside_inline : public iterator_selector
	{
	public:
		virtual bool select(const element::ptr& el);
	};

	class go_inside_table : public iterator_selector
	{
	public:
		virtual bool select(const element::ptr& el);
	};

	class table_rows_selector : public iterator_selector
	{
	public:
		virtual bool select(const element::ptr& el);
	};

	class table_cells_selector : public iterator_selector
	{
	public:
		virtual bool select(const element::ptr& el);
	};
}
