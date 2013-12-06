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
			(static_cast<S*>(cmp.system)->*m_bool_setter)(cmp, _stricmp(value.c_str(), "true") == 0); 
			break;
		case FILE:
			(static_cast<S*>(cmp.system)->*m_setter)(cmp, value); 
			break;
		case VEC3:
			{
				char tmp[255];
				ASSERT(value.length() < 255);
				strcpy(tmp, value.c_str());
				for(int i = 0; i < value.length(); ++i)
				{
					if(tmp[i] == ',')
					{
						tmp[i] = '.';
					}
				}
				Vec3 v;
				sscanf(tmp, "%f;%f;%f", &v.x, &v.y, &v.z);
				(static_cast<S*>(cmp.system)->*m_vec3_setter)(cmp, v);
			}
			break;
		default:
			ASSERT(false);
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
		case VEC3:
			{
				char tmp[150];
				Vec3 v;
				(static_cast<S*>(cmp.system)->*m_vec3_getter)(cmp, v);
				sprintf(tmp, "%f;%f;%f", v.x, v.y, v.z);
				value = tmp;
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


} // !namespace Lux