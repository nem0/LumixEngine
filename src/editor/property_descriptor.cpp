#include "property_descriptor.h"
#include <cstdio>
#include "core/istream.h"


namespace Lux
{


void PropertyDescriptor::set(Component cmp, const uint8_t* data, int size) const
{
	switch(m_type)
	{
		case DECIMAL:
			{
				float f;
				f = *(float*)data;
				(static_cast<S*>(cmp.system)->*m_decimal_setter)(cmp, f); 
			}
			break;
		case BOOL:
			{
				bool b;
				b = *(bool*)data;
				(static_cast<S*>(cmp.system)->*m_bool_setter)(cmp, b); 
			}
			break;
		case FILE:
			{
				char tmp[300];
				ASSERT(size < 300);
				strncpy_s(tmp, (char*)data, size);
				tmp[size] = '\0';
				string s = (char*)tmp;
				(static_cast<S*>(cmp.system)->*m_setter)(cmp, s); 
			}
			break;
		case VEC3:
			{
				Vec3 v;
				v = *(Vec3*)data;
				(static_cast<S*>(cmp.system)->*m_vec3_setter)(cmp, v);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


void PropertyDescriptor::get(Component cmp, IStream& stream) const
{
	int len = 4;
	switch(m_type)
	{
		case FILE:
			{
				string value;
				(static_cast<S*>(cmp.system)->*m_getter)(cmp, value);
				len = value.length();
				stream.write(&len, sizeof(len));
				stream.write(value.c_str(), len);
			}
			break;
		case DECIMAL:
			{
				float f;
				(static_cast<S*>(cmp.system)->*m_decimal_getter)(cmp, f);
				len = sizeof(f);
				stream.write(&len, sizeof(len));
				stream.write(&f, len);
			}
			break;
		case BOOL:
			{
				bool b;
				(static_cast<S*>(cmp.system)->*m_bool_getter)(cmp, b);
				len = sizeof(b);
				stream.write(&len, sizeof(len));
				stream.write(&b, len);
			}
			break;
		case VEC3:
			{
				Vec3 v;
				(static_cast<S*>(cmp.system)->*m_vec3_getter)(cmp, v);
				len = sizeof(v);
				stream.write(&len, sizeof(len));
				stream.write(&v, len);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


} // !namespace Lux