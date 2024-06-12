#include "core/crt.h"
#include "core/log.h"
#include "core/string.h"

#include "lua_wrapper.h"
#include <luacode.h>

namespace Lumix::LuaWrapper {

#ifdef LUMIX_DEBUG
	DebugGuard::DebugGuard(lua_State* L)
		: L(L) 
	{
		top = lua_gettop(L);
	}

	DebugGuard::DebugGuard(lua_State* L, int offset)
		: L(L) 
	{
		top = lua_gettop(L) + offset;
	}


	DebugGuard::~DebugGuard()
	{
		const int current_top = lua_gettop(L);
		ASSERT(current_top == top);
	}
#endif

int traceback(lua_State *L) {
	if (!lua_isstring(L, 1)) return 1;
	
	lua_getfield(L, LUA_GLOBALSINDEX, "LumixDebugCallback");
	if (lua_isfunction(L, -1)) {
		lua_pushvalue(L, 1);
		if (lua_pcall(L, 1, 0, 0) != 0) {
			logError(lua_tostring(L, -1));
			return 1;
		}
	}
	else lua_pop(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "debug");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}
	
	lua_pushvalue(L, 1);
	lua_pushinteger(L, 2);
	lua_call(L, 2, 1);

	return 1;
}

void convertPropertyToLuaName(const char* src, Span<char> out) {
	const u32 max_size = out.length();
	ASSERT(max_size > 0);
	char* dest = out.begin();
	while (*src && dest - out.begin() < max_size - 1) {
		if (isLetter(*src)) {
			*dest = isUpperCase(*src) ? *src - 'A' + 'a' : *src;
			++dest;
		}
		else if (isNumeric(*src)) {
			*dest = *src;
			++dest;
		}
		else {
			*dest = '_';
			++dest;
		}
		++src;
	}
	*dest = 0;
}

bool pcall(lua_State* L, int nargs, int nres)
{
	lua_pushcfunction(L, traceback, "traceback");
	lua_insert(L, -2 - nargs);
	if (lua_pcall(L, nargs, nres, -2 - nargs) != 0) {
		logError(lua_tostring(L, -1));
		lua_pop(L, 2);
		return false;
	}
	lua_remove(L, -1 - nres);
	return true;
}

bool execute(lua_State* L
	, StringView content
	, const char* name
	, int nresults)
{
	lua_pushcfunction(L, traceback, "traceback");
	
	size_t bytecodeSize = 0;
	char* bytecode = luau_compile((const char*)content.begin, content.size(), NULL, &bytecodeSize);
	int res = luau_load(L, name, bytecode, bytecodeSize, 0);
	free(bytecode);
	if (res != 0) {
		logError(name, ": ", lua_tostring(L, -1));
		lua_pop(L, 2);
		return false;
	}

	if (lua_pcall(L, 0, nresults, -2) != 0) {
		logError(name, ": ", lua_tostring(L, -1));
		lua_pop(L, 2);
		return false;
	}
	lua_remove(L, -nresults - 1);
	return true;
}

bool checkStringField(lua_State* L, int idx, const char* k, Span<char> out) {
	lua_getfield(L, idx, k);
	if (!isType<const char*>(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	const char* tmp = toType<const char*>(L, -1);
	copyString(out, tmp);
	lua_pop(L, 1);
	return true;
}

void push(lua_State* L, EntityRef value) {
	lua_pushinteger(L, value.index);
}

bool toEntity(lua_State* L, int idx, World*& world, EntityRef& entity) {
	if (!lua_istable(L, idx)) return false;
	if (getField(L, 1, "_entity") != LUA_TNUMBER) {
		lua_pop(L, 1);
		return false;
	}
	entity = EntityRef{toType<i32>(L, -1)};
	lua_pop(L, 1);

	if (getField(L, 1, "_world") != LUA_TLIGHTUSERDATA) {
		lua_pop(L, 1);
		return false;
	}
	world = toType<World*>(L, -1);
	lua_pop(L, 1);

	return true;
}

void pushEntity(lua_State* L, EntityPtr value, World* world) {
	if (!value.isValid()) {
		lua_newtable(L); // [env, {}]
		return;
	}

	lua_getglobal(L, "Lumix");						// [Lumix]
	lua_getfield(L, -1, "Entity");					// [Lumix, Lumix.Entity]
	lua_remove(L, -2);								// [Lumix.Entity]
	lua_getfield(L, -1, "new");						// [Lumix.Entity, Entity.new]
	lua_pushvalue(L, -2);							// [Lumix.Entity, Entity.new, Lumix.Entity]
	lua_remove(L, -3);								// [Entity.new, Lumix.Entity]
	lua_pushlightuserdata(L, world);				// [Entity.new, Lumix.Entity, world]
	lua_pushnumber(L, value.index);					// [Entity.new, Lumix.Entity, world, entity_index]
	const bool error = !LuaWrapper::pcall(L, 3, 1); // [entity]
	ASSERT(!error);
}

void pushObject(lua_State* L, void* obj, StringView type_name) {
	ASSERT(!type_name.empty());
	LuaWrapper::DebugGuard guard(L, 1);
	lua_getglobal(L, "LumixAPI");
	char tmp[64];
	copyString(Span(tmp), type_name);

	if (LuaWrapper::getField(L, -1, tmp) != LUA_TTABLE) {
		lua_pop(L, 2);
		lua_newtable(L);
		lua_pushlightuserdata(L, obj);
		lua_setfield(L, -2, "_value");
		ASSERT(false);
		return;
	}

	lua_newtable(L); // [LumixAPI, class, obj]
	lua_pushlightuserdata(L, obj); // [LumixAPI, class, obj, obj_ptr]
	lua_setfield(L, -2, "_value"); // [LumixAPI, class, obj]
	lua_pushvalue(L, -2); // [LumixAPI, class, obj, class]
	lua_setmetatable(L, -2); // [LumixAPI, class, obj]
	lua_remove(L, -2); // [LumixAPI, obj]
	lua_remove(L, -2); // [obj]
}

int luaL_loadbuffer(lua_State* L, const char* buff, size_t size, const char* name) {
	size_t bytecode_size;
	char* bytecode = luau_compile(buff, size, nullptr, &bytecode_size);
	if (!bytecode) return 1;
	int res = luau_load(L, name ? name : "N/A", bytecode, bytecode_size, 0);
	free(bytecode);
	return res;
}

void releaseRef(lua_State* L, RefHandle ref) { lua_unref(L, ref); }
RefHandle createRef(lua_State* L) { return lua_ref(L, -1); }
void pushRef(lua_State* L, RefHandle ref) { lua_rawgeti(L, LUA_REGISTRYINDEX, ref); }

int getField(lua_State* L, int idx, const char* k) {
	lua_getfield(L, idx, k);
	return lua_type(L, -1);
}

bool getOptionalStringField(lua_State* L, int idx, const char* field_name, Span<char> out) {
	bool ret = false;
	if (LuaWrapper::getField(L, idx, field_name) != LUA_TNIL && isType<const char*>(L, -1)) {
		const char* src = toType<const char*>(L, -1);
		;
		copyString(out, src);
		ret = true;
	}
	lua_pop(L, 1);
	return ret;
}

void checkTableArg(lua_State* L, int index) {
	if (!lua_istable(L, index)) {
		argError(L, index, "table");
	}
}

void createSystemVariable(lua_State* L, const char* system, const char* var_name, void* value) {
	lua_getglobal(L, system);
	if (lua_type(L, -1) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushlightuserdata(L, value);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}

void createSystemVariable(lua_State* L, const char* system, const char* var_name, int value) {
	lua_getglobal(L, system);
	if (lua_type(L, -1) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushinteger(L, value);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}

void createSystemFunction(lua_State* L, const char* system, const char* var_name, lua_CFunction fn) {
	lua_getglobal(L, system);
	if (lua_type(L, -1) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushcfunction(L, fn, var_name);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}

void createSystemClosure(lua_State* L, const char* system, void* system_ptr, const char* var_name, lua_CFunction fn) {
	lua_getglobal(L, system);
	if (lua_type(L, -1) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushlightuserdata(L, system_ptr);
	lua_pushcclosure(L, fn, var_name, 1);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}

const char* luaTypeToString(int type) {
	switch (type) {
		case LUA_TNUMBER: return "number";
		case LUA_TBOOLEAN: return "boolean";
		case LUA_TFUNCTION: return "function";
		case LUA_TLIGHTUSERDATA: return "light userdata";
		case LUA_TNIL: return "nil";
		case LUA_TSTRING: return "string";
		case LUA_TTABLE: return "table";
		case LUA_TUSERDATA: return "userdata";
		default: return "Unknown";
	}
}

void argError(lua_State* L, int index, const char* expected_type) {
	int type = lua_type(L, index);
	StaticString<128> buf("expected ", expected_type, ", got ", LuaWrapper::luaTypeToString(type));
	luaL_argerror(L, index, buf);
}


} // namespace Lumix::LuaWrapper
