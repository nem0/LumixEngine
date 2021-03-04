#include "engine/reflection.h"
#include "engine/allocator.h"
#include "engine/allocators.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/stream.h"

namespace Lumix
{


namespace reflection
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

struct Context {
	reflscene* first_refl_scene = nullptr; 
	RegisteredReflComponent reflcmps[ComponentType::MAX_TYPES_COUNT];
	u32 components_count = 0;
};

static Context& getContext() {
	static Context ctx;
	return ctx;
}

Array<FunctionBase*>& allFunctions() {
	static DefaultAllocator allocator;
	static Array<FunctionBase*> fncs(allocator);
	return fncs;
}


const reflcmp* getReflComponent(ComponentType cmp_type) {
	return getContext().reflcmps[cmp_type.index].cmp;
}

const reflprop* getReflProp(ComponentType cmp_type, const char* prop_name) {
	const reflcmp* cmp = getReflComponent(cmp_type);
	for (reflprop* prop : cmp->props) {
		if (equalStrings(prop->name, prop_name)) return prop;
	}
	return nullptr;
}

const ComponentBase* getComponent(ComponentType cmp_type) {
	return nullptr;
}

void builder::registerCmp(reflcmp* cmp) {
	getContext().reflcmps[cmp->component_type.index].cmp = cmp;
	getContext().reflcmps[cmp->component_type.index].name_hash = crc32(cmp->name);
	getContext().reflcmps[cmp->component_type.index].scene = crc32(scene->name);
}

ComponentType getComponentTypeFromHash(u32 hash)
{
	for (u32 i = 0, c = getContext().components_count; i < c; ++i) {
		if (getContext().reflcmps[i].name_hash == hash) {
			return {(i32)i};
		}
	}
	ASSERT(false);
	return {-1};
}


u32 getComponentTypeHash(ComponentType type)
{
	return getContext().reflcmps[type.index].name_hash;
}


ComponentType getComponentType(const char* name)
{
	Context& ctx = getContext();
	u32 name_hash = crc32(name);
	for (u32 i = 0, c = ctx.components_count; i < c; ++i) {
		if (ctx.reflcmps[i].name_hash == name_hash) {
			return {(i32)i};
		}
	}

	if (ctx.components_count == ComponentType::MAX_TYPES_COUNT) {
		logError("Too many component types");
		return INVALID_COMPONENT_TYPE;
	}

	RegisteredReflComponent& type = ctx.reflcmps[getContext().components_count];
	type.name_hash = name_hash;
	++ctx.components_count;
	return {i32(getContext().components_count - 1)};
}

Span<const RegisteredReflComponent> getReflComponents() {
	return Span(getContext().reflcmps, getContext().components_count);
}

static IAllocator& getReflAlloc() {
	static DefaultAllocator alloc;
	return alloc;
}

builder build_scene(const char* name) {
	builder res(getReflAlloc());
	res.scene->next = getContext().first_refl_scene;
	getContext().first_refl_scene = res.scene;
	res.scene->name = name;
	return res;
}


} // namespace Reflection


} // namespace Lumix
