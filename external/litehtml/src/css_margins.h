#pragma once
#include "css_length.h"

namespace litehtml
{
	struct css_margins
	{
		css_length	left;
		css_length	right;
		css_length	top;
		css_length	bottom;

		css_margins()
		{

		}

		css_margins(const css_margins& val)
		{
			left	= val.left;
			right	= val.right;
			top		= val.top;
			bottom	= val.bottom;
		}

		css_margins& operator=(const css_margins& val)
		{
			left	= val.left;
			right	= val.right;
			top		= val.top;
			bottom	= val.bottom;
			return *this;
		}
	};
}