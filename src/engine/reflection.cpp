#include "engine/reflection.h"
#include "engine/crc32.h"
#include "engine/allocator.h"
#include "engine/log.h"


namespace Lumix
{


namespace Reflection
{


template <> Path readFromStream<Path>(InputMemoryStream& stream)
{
	const char* c_str = (const char*)stream.getData() + stream.getPosition();
	Path path(c_str);
	stream.skip(stringLength(c_str) + 1);
	return path;
}


template <> void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path)
{
	const char* str = path.c_str();
	stream.write(str, stringLength(str) + 1);
}


template <> void writeToStream<Path>(OutputMemoryStream& stream, Path path)
{
	const char* str = path.c_str();
	stream.write(str, stringLength(str) + 1);
}


template <> const char* readFromStream<const char*>(InputMemoryStream& stream)
{
	const char* c_str = (const char*)stream.getData() + stream.getPosition();
	stream.skip(stringLength(c_str) + 1);
	return c_str;
}


template <> void writeToStream<const char*>(OutputMemoryStream& stream, const char* value)
{
	stream.write(value, stringLength(value) + 1);
}


struct ComponentTypeData
{
	char id[50];
	u32 id_hash;
};


static IAllocator* g_allocator = nullptr;


struct ComponentLink
{
	const ComponentBase* desc;
	ComponentLink* next;
};


static ComponentLink* g_first_component = nullptr;


const ComponentBase* getComponent(ComponentType cmp_type)
{
	ComponentLink* link = g_first_component;
	while (link)
	{
		if (link->desc->component_type == cmp_type) return link->desc;
		link = link->next;
	}

	return nullptr;
}


void registerComponent(const ComponentBase& desc)
{
	ComponentLink* link = LUMIX_NEW(*g_allocator, ComponentLink);
	link->next = g_first_component;
	link->desc = &desc;
	g_first_component = link;
}


void registerScene(const SceneBase& scene) {
	for (const ComponentBase* cmp : scene.getComponents()) {
		registerComponent(*cmp);
	}
}


static Array<ComponentTypeData>& getComponentTypes()
{
	static DefaultAllocator allocator;
	static Array<ComponentTypeData> types(allocator);
	return types;
}


void init(IAllocator& allocator)
{
	g_allocator = &allocator;
}


static void destroy(ComponentLink* link)
{
	if (!link) return;
	destroy(link->next);
	LUMIX_DELETE(*g_allocator, link);
}


void shutdown()
{
	destroy(g_first_component);
	g_allocator = nullptr;
}


ComponentType getComponentTypeFromHash(u32 hash)
{
	auto& types = getComponentTypes();
	for (int i = 0, c = types.size(); i < c; ++i)
	{
		if (types[i].id_hash == hash)
		{
			return {i};
		}
	}
	ASSERT(false);
	return {-1};
}


u32 getComponentTypeHash(ComponentType type)
{
	return getComponentTypes()[type.index].id_hash;
}


ComponentType getComponentType(const char* id)
{
	u32 id_hash = crc32(id);
	auto& types = getComponentTypes();
	for (int i = 0, c = types.size(); i < c; ++i)
	{
		if (types[i].id_hash == id_hash)
		{
			return {i};
		}
	}

	auto& cmp_types = getComponentTypes();
	if (types.size() == ComponentType::MAX_TYPES_COUNT)
	{
		logError("Engine") << "Too many component types";
		return INVALID_COMPONENT_TYPE;
	}

	ComponentTypeData& type = cmp_types.emplace();
	copyString(type.id, id);
	type.id_hash = id_hash;
	return {getComponentTypes().size() - 1};
}


int getComponentTypesCount()
{
	return getComponentTypes().size();
}


const char* getComponentTypeID(int index)
{
	return getComponentTypes()[index].id;
}


} // namespace Reflection


} // namespace Lumix
