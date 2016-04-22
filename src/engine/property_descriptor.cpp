#include "property_descriptor.h"
#include "engine/core/crc32.h"
#include <cfloat>
#include <cstdint>


namespace Lumix
{


void IPropertyDescriptor::setName(const char* name)
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


IDecimalPropertyDescriptor::IDecimalPropertyDescriptor(IAllocator& allocator)
	: IPropertyDescriptor(allocator)
{
	m_min = -FLT_MAX;
	m_max = FLT_MAX;
	m_step = 0.1f;
}



} // namespace Lumix
