#include "animation/property_animation.h"
#include "engine/allocator.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/reflection.h"
#include "engine/stream.h"


namespace Lumix {


const ResourceType PropertyAnimation::TYPE("property_animation");

PropertyAnimation::PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, fps(30)
	, curves(allocator)
{
}


PropertyAnimation::Curve& PropertyAnimation::addCurve()
{
	return curves.emplace(m_allocator);
}


bool PropertyAnimation::save(OutputMemoryStream& blob) {
	if (!isReady()) return false;

	for (Curve& curve : curves) {
		blob << "curve {\n";
		blob << "\t component = \"" << reflection::getComponent(curve.cmp_type)->name << "\",\n";
		blob << "\t property = \"" << curve.property->name << "\",\n";
		blob << "\tkeyframes = {\n";
		for (int i = 0; i < curve.frames.size(); ++i) {
			if (i != 0) blob << ", ";
			blob << curve.frames[i];
		}
		blob << "},\n";
		blob << "\tvalues = {\n";
		for (int i = 0; i < curve.values.size(); ++i) {
			if (i != 0) blob << ", ";
			blob << curve.values[i];
		}
		blob << "}\n}\n\n";
	}

	return true;
}

void PropertyAnimation::LUA_curve(lua_State* L) {
	LuaWrapper::DebugGuard guard(L);
	LuaWrapper::checkTableArg(L, 1);
	const char* cmp_name;
	const char* prop_name;
	if (!LuaWrapper::checkField<const char*>(L, 1, "component", &cmp_name)) {
		luaL_argerror(L, 1, "`component` field must be a string");
	}
	if (!LuaWrapper::checkField<const char*>(L, 1, "prop_name", &prop_name)) {
		luaL_argerror(L, 1, "`property` field must be a string");
	}
	Curve& curve = curves.emplace(m_allocator);
	curve.cmp_type = reflection::getComponentType(cmp_name);
	// TODO property
	if (!LuaWrapper::getField(L, 1, "keyframes")) {
		luaL_argerror(L, 1, "`keyframes` field must be an array");
	}
	LuaWrapper::forEachArrayItem<i32>(L, -1, "`keyframes` field must be an array of keyframes", [&](i32 v){
		curve.frames.emplace(v);
	});
	lua_pop(L, 1);
	if (!LuaWrapper::getField(L, 1, "values")) {
		luaL_argerror(L, 1, "`values` field must be an array");
	}
	LuaWrapper::forEachArrayItem<float>(L, -1, "`values` field must be an array of numbers", [&](float v){
		curve.values.emplace(v);
	});
	lua_pop(L, 1);
}

bool PropertyAnimation::load(u64 size, const u8* mem)
{
	lua_State* L = luaL_newstate(); // TODO reuse
	auto fn = &LuaWrapper::wrapMethodClosure<&PropertyAnimation::LUA_curve>;
	lua_pushlightuserdata(L, this);
	lua_pushcclosure(L, fn, 1);
	lua_setglobal(L, "curve");

	return LuaWrapper::execute(L, Span((const char*)mem, (u32)size), getPath().c_str(), 0);
}


void PropertyAnimation::unload()
{
	curves.clear();
}


} // namespace Lumix
