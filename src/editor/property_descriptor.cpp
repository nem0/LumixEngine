#include "property_descriptor.h"
#include "core/crc32.h"
#include <cfloat>
#include <cstdint>



namespace Lumix
{


void IPropertyDescriptor::setName(const char* name)
{
	m_name = name;
	m_name_hash = crc32(name);
}


IIntPropertyDescriptor::IIntPropertyDescriptor(IAllocator& allocator)
	: IPropertyDescriptor(allocator)
{
	m_min = INT32_MIN;
	m_max = INT32_MAX;
}


IDecimalPropertyDescriptor::IDecimalPropertyDescriptor(IAllocator& allocator)
	: IPropertyDescriptor(allocator)
{
	m_min = -FLT_MAX;
	m_max = FLT_MAX;
	m_step = 0.1f;
}



} // namespace Lumix