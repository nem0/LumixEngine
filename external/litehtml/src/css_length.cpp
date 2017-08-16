#include "html.h"
#include "css_length.h"

void litehtml::css_length::fromString( const tstring& str, const tstring& predefs, int defValue )
{
	// TODO: Make support for calc
	if(str.substr(0, 4) == _t("calc"))
	{
		m_is_predefined = true;
		m_predef		= 0;
		return;
	}

	int predef = value_index(str.c_str(), predefs.c_str(), -1);
	if(predef >= 0)
	{
		m_is_predefined = true;
		m_predef		= predef;
	} else
	{
		m_is_predefined = false;

		tstring num;
		tstring un;
		bool is_unit = false;
		for(tstring::const_iterator chr = str.begin(); chr != str.end(); chr++)
		{
			if(!is_unit)
			{
				if(t_isdigit(*chr) || *chr == _t('.') || *chr == _t('+') || *chr == _t('-'))
				{
					num += *chr;
				} else
				{
					is_unit = true;
				}
			}
			if(is_unit)
			{
				un += *chr;
			}
		}
		if(!num.empty())
		{
			m_value = (float) t_strtod(num.c_str(), 0);
			m_units	= (css_units) value_index(un.c_str(), css_units_strings, css_units_none);
		} else
		{
			// not a number so it is predefined
			m_is_predefined = true;
			m_predef = defValue;
		}
	}
}
