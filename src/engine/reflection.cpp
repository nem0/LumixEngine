#include "engine/reflection.h"
#include "engine/allocators.h"
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


static SceneBase* g_first_scene = nullptr;
static RegisteredComponent g_components[ComponentType::MAX_TYPES_COUNT];
static u32 g_components_count = 0;

Array<FunctionBase*>& allFunctions() {
	static DefaultAllocator allocator;
	static Array<FunctionBase*> fncs(allocator);
	return fncs;
}

const ComponentBase* getComponent(ComponentType cmp_type) {
	return g_components[cmp_type.index].cmp;
}

SceneBase* getFirstScene() { return g_first_scene; }

void registerScene(SceneBase& scene) {
	scene.next = g_first_scene;
	g_first_scene = &scene;

	const u32 scene_name_hash = crc32(scene.name);
	for (ComponentBase* cmp : scene.getComponents()) {
		g_components[cmp->component_type.index].cmp = cmp;
		g_components[cmp->component_type.index].scene = scene_name_hash;
	}
}


ComponentType getComponentTypeFromHash(u32 hash)
{
	for (u32 i = 0, c = g_components_count; i < c; ++i) {
		if (g_components[i].name_hash == hash) {
			return {(i32)i};
		}
	}
	ASSERT(false);
	return {-1};
}


u32 getComponentTypeHash(ComponentType type)
{
	return g_components[type.index].name_hash;
}


ComponentType getComponentType(const char* name)
{
	u32 name_hash = crc32(name);
	for (u32 i = 0, c = g_components_count; i < c; ++i) {
		if (g_components[i].name_hash == name_hash) {
			return {(i32)i};
		}
	}

	static_assert(ComponentType::MAX_TYPES_COUNT == lengthOf(g_components));
	if (g_components_count == ComponentType::MAX_TYPES_COUNT) {
		logError("Too many component types");
		return INVALID_COMPONENT_TYPE;
	}

	RegisteredComponent& type = g_components[g_components_count];
	type.name_hash = name_hash;
	++g_components_count;
	return {i32(g_components_count - 1)};
}

Span<const RegisteredComponent> getComponents() {
	return Span(g_components, g_components_count);
}


} // namespace Reflection


} // namespace Lumix
