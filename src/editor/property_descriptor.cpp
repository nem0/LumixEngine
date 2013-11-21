#include "property_descriptor.h"
#include <cstdio>


namespace Lux
{


void PropertyDescriptor::set(Component cmp, const string& value) const
{
	switch(m_type)
	{
		case DECIMAL:
			{
				float f;
				sscanf_s(value.c_str(), "%f", &f);
				(static_cast<S*>(cmp.system)->*m_decimal_setter)(cmp, f); 
			}
			break;
		case BOOL:
			{
				(static_cast<S*>(cmp.system)->*m_bool_setter)(cmp, _stricmp(value.c_str(), "true") == 0); 
			}
			break;
		case FILE:
			(static_cast<S*>(cmp.system)->*m_setter)(cmp, value); 
			break;
	}
}


void PropertyDescriptor::get(Component cmp, string& value) const
{
	switch(m_type)
	{
		case FILE:
			(static_cast<S*>(cmp.system)->*m_getter)(cmp, value);
			break;
		case DECIMAL:
			{
				float f;
				char tmp[30];
				(static_cast<S*>(cmp.system)->*m_decimal_getter)(cmp, f);
				sprintf_s(tmp, "%f", f);
				value = tmp;
			}
			break;
		case BOOL:
			{
				bool b;
				(static_cast<S*>(cmp.system)->*m_bool_getter)(cmp, b);
				value = b ? "true" : "false";
			}
			break;
	}
}


} // !namespace Lux