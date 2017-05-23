#include "engine/property_descriptor.h"
#include "engine/crc32.h"
#include <cfloat>
#include <cstdint>


namespace Lumix
{


void PropertyDescriptorBase::setName(const char* name)
{
	m_name = name;
	m_name_hash = crc32(name);
}


LUMIX_ENGINE_API int getIntPropertyMin()
{
	return INT32_MIN;
}


LUMIX_ENGINE_API int getIntPropertyMax()
{
	return INT32_MAX;
}


} // namespace Lumix
