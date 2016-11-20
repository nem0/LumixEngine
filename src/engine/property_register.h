#pragma once


#include "engine/lumix.h"


namespace Lumix
{


template <typename T> class Array;
class IAllocator;
class IPropertyDescriptor;


namespace PropertyRegister
{


LUMIX_ENGINE_API void init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();
LUMIX_ENGINE_API void add(const char* component_type, IPropertyDescriptor* desc);
LUMIX_ENGINE_API const IPropertyDescriptor* getDescriptor(ComponentType type, u32 name_hash);
LUMIX_ENGINE_API const IPropertyDescriptor* getDescriptor(const char* component_type, const char* property_name);
LUMIX_ENGINE_API Array<IPropertyDescriptor*>& getDescriptors(ComponentType type);


LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API u32 getComponentTypeHash(ComponentType type);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);
LUMIX_ENGINE_API int getComponentTypesCount();
LUMIX_ENGINE_API const char* getComponentTypeID(int index);



} // namespace PropertyRegister


} // namespace Lumix
