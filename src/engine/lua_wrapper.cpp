#include "lua_wrapper.h"
#include "log.h"
#include "string.h"

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

int traceback (lua_State *L) {
	if (!lua_isstring(L, 1)) return 1;
	
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


bool pcall(lua_State* L, int nargs, int nres)
{
	lua_pushcfunction(L, traceback);
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
	lua_pushcfunction(L, traceback);
	if (luaL_loadbuffer(L, content.begin, content.size(), name) != 0) {
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
	lua_pushcfunction(L, fn);
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
	lua_pushcclosure(L, fn, 1);
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
	char buf[128];
	copyString(buf, "expected ");
	catString(buf, expected_type);
	catString(buf, ", got ");
	int type = lua_type(L, index);
	catString(buf, LuaWrapper::luaTypeToString(type));
	luaL_argerror(L, index, buf);
}


} // namespace Lumix::LuaWrapper
