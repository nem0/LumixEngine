#pragma once
#include "css_length.h"

namespace litehtml
{
	struct css_position
	{
		css_length	x;
		css_length	y;
		css_length	width;
		css_length	height;

		css_position()
		{

		}

		css_position(const css_position& val)
		{
			x		= val.x;
			y		= val.y;
			width	= val.width;
			height	= val.height;
		}

		css_position& operator=(const css_position& val)
		{
			x		= val.x;
			y		= val.y;
			width	= val.width;
			height	= val.height;
			return *this;
		}
	};
}