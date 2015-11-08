#include "property_register.h"
#include "core/associative_array.h"
#include "editor/property_descriptor.h"


namespace Lumix
{


namespace PropertyRegister
{


struct ComponentType
{
	ComponentType(IAllocator& allocator)
		: m_name(allocator)
		, m_id(allocator)
	{
	}

	string m_name;
	string m_id;

	uint32_t m_id_hash;
	uint32_t m_dependency;
};


typedef AssociativeArray<uint32_t, Array<IPropertyDescriptor*>> PropertyMap;


static PropertyMap* g_properties = nullptr;
static Array<ComponentType>* g_component_types = nullptr;
static IAllocator* g_allocator = nullptr;


void init(IAllocator& allocator)
{
	ASSERT(!g_properties);
	g_properties = LUMIX_NEW(allocator, PropertyMap)(allocator);
	g_allocator = &allocator;
	g_component_types = LUMIX_NEW(allocator, Array<ComponentType>)(allocator);
}


void shutdown()
{
	for (int j = 0; j < g_properties->size(); ++j)
	{
		Array<IPropertyDescriptor*>& props = g_properties->at(j);
		for (auto* prop : props)
		{
			LUMIX_DELETE(*g_allocator, prop);
		}
	}

	LUMIX_DELETE(*g_allocator, g_properties);
	LUMIX_DELETE(*g_allocator, g_component_types);
	g_properties = nullptr;
	g_allocator = nullptr;
	g_component_types = nullptr;
}


void add(const char* component_type, IPropertyDescriptor* descriptor)
{
	getDescriptors(crc32(component_type)).push(descriptor);
}


Array<IPropertyDescriptor*>& getDescriptors(uint32_t type)
{
	int props_index = g_properties->find(type);
	if (props_index < 0)
	{
		g_properties->insert(type, Array<IPropertyDescriptor*>(*g_allocator));
		props_index = g_properties->find(type);
	}
	return g_properties->at(props_index);
}


const IPropertyDescriptor& getDescriptor(uint32_t type, uint32_t name_hash)
{
	Array<IPropertyDescriptor*>& props = getDescriptors(type);
	for (int i = 0; i < props.size(); ++i)
	{
		if (props[i]->getNameHash() == name_hash)
		{
			return *props[i];
		}
	}
	ASSERT(false);
	return *props[0];
}


const IPropertyDescriptor& getDescriptor(const char* component_type, const char* property_name)
{
	return getDescriptor(crc32(component_type), crc32(property_name));
}


void registerComponentDependency(const char* id, const char* dependency_id)
{
	for (ComponentType& cmp_type : *g_component_types)
	{
		if (cmp_type.m_id == id)
		{
			cmp_type.m_dependency = crc32(dependency_id);
			return;
		}
	}
	ASSERT(false);
}


bool componentDepends(uint32_t dependent, uint32_t dependency)
{
	for (ComponentType& cmp_type : *g_component_types)
	{
		if (cmp_type.m_id_hash == dependent)
		{
			return cmp_type.m_dependency == dependency;
		}
	}
	return false;
}


void registerComponentType(const char* id, const char* name)
{
	ComponentType& type = g_component_types->emplace(*g_allocator);
	type.m_name = name;
	type.m_id = id;
	type.m_id_hash = crc32(id);
	type.m_dependency = 0;
}


int getComponentTypesCount()
{
	return g_component_types->size();
}


const char* getComponentTypeName(int index)
{
	return (*g_component_types)[index].m_name.c_str();
}


const char* getComponentTypeID(int index)
{
	return (*g_component_types)[index].m_id.c_str();
}


} // namespace PropertyRegister


} // namespace Lumix
