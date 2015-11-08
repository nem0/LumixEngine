#pragma once


#include "lumix.h"
#include "core/array.h"


namespace Lumix
{


class IPropertyDescriptor;


namespace PropertyRegister
{


LUMIX_EDITOR_API void init(IAllocator& allocator);
LUMIX_EDITOR_API void shutdown();
LUMIX_EDITOR_API void add(const char* component_type, IPropertyDescriptor* desc);
LUMIX_EDITOR_API const IPropertyDescriptor& getDescriptor(uint32_t type, uint32_t name_hash);
LUMIX_EDITOR_API const IPropertyDescriptor& getDescriptor(const char* component_type,
	const char* property_name);
LUMIX_EDITOR_API Array<IPropertyDescriptor*>& getDescriptors(uint32_t type);


LUMIX_EDITOR_API void registerComponentDependency(const char* id, const char* dependency_id);
LUMIX_EDITOR_API bool componentDepends(uint32_t dependent, uint32_t dependency);
LUMIX_EDITOR_API void registerComponentType(const char* name, const char* title);
LUMIX_EDITOR_API int getComponentTypesCount();
LUMIX_EDITOR_API const char* getComponentTypeName(int index);
LUMIX_EDITOR_API const char* getComponentTypeID(int index);



} // namespace PropertyRegister


} // namespace Lumix
