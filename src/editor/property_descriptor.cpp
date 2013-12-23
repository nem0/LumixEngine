#include "property_descriptor.h"
#include <cstdio>
#include "core/blob.h"


namespace Lux
{


void PropertyDescriptor::set(Component cmp, Blob& stream) const
{
	int len;
	stream.read(&len, sizeof(len));
	switch(m_type)
	{
		case DECIMAL:
			{
				float f;
				stream.read(&f, sizeof(f));
				(static_cast<S*>(cmp.system)->*m_decimal_setter)(cmp, f); 
			}
			break;
		case BOOL:
			{
				bool b;
				stream.read(&b, sizeof(b));
				(static_cast<S*>(cmp.system)->*m_bool_setter)(cmp, b); 
			}
			break;
		case FILE:
			{
				char tmp[301];
				ASSERT(len < 300);
				stream.read(tmp, len);
				tmp[len] = '\0';
				string s = (char*)tmp;
				(static_cast<S*>(cmp.system)->*m_setter)(cmp, s); 
			}
			break;
		case VEC3:
			{
				Vec3 v;
				stream.read(&v, sizeof(v));
				(static_cast<S*>(cmp.system)->*m_vec3_setter)(cmp, v);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


void PropertyDescriptor::get(Component cmp, Blob& stream) const
{
	int len = 4;
	switch(m_type)
	{
		case FILE:
			{
				string value;
				(static_cast<S*>(cmp.system)->*m_getter)(cmp, value);
				len = value.length() + 1;
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