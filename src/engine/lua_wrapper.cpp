#include "lua_wrapper.h"

namespace Lumix::LuaWrapper {

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
	, Span<const char> content
	, const char* name
	, int nresults)
{
	lua_pushcfunction(L, traceback);
	if (luaL_loadbuffer(L, content.begin(), content.length(), name) != 0) {
		logError(name, ": ", lua_tostring(L, -1));
		lua_pop(L, 2);
		return false;
	}

	if (lua_pcall(L, 0, nresults, -2) != 0) {
		logError(lua_tostring(L, -1));
		lua_pop(L, 2);
		return false;
	}
	lua_pop(L, 1);
	return true;
}

}