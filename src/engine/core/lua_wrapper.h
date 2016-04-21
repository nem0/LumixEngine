#pragma once


#include "core/log.h"
#include "core/vec.h"
#include <lua.hpp>
#include <lauxlib.h>
#include <tuple>


namespace Lumix
{


namespace LuaWrapper
{
template <typename T> inline T toType(lua_State* L, int index)
{
	return (T)lua_touserdata(L, index);
}
template <> inline int toType(lua_State* L, int index)
{
	return (int)lua_tointeger(L, index);
}
template <> inline Vec3 toType(lua_State* L, int index)
{
	Vec3 v;
	lua_rawgeti(L, index, 1);
	v.x = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 3);
	v.z = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}
template <> inline Vec2 toType(lua_State* L, int index)
{
	Vec2 v;
	lua_rawgeti(L, index, 1);
	v.x = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}
template <> inline int64 toType(lua_State* L, int index)
{
	return (int64)lua_tointeger(L, index);
}
template <> inline uint32 toType(lua_State* L, int index)
{
	return (uint32)lua_tointeger(L, index);
}
template <> inline bool toType(lua_State* L, int index)
{
	return lua_toboolean(L, index) != 0;
}
template <> inline float toType(lua_State* L, int index)
{
	return (float)lua_tonumber(L, index);
}
template <> inline const char* toType(lua_State* L, int index)
{
	return lua_tostring(L, index);
}
template <> inline void* toType(lua_State* L, int index)
{
	return lua_touserdata(L, index);
}


template <typename T> inline const char* typeToString()
{
	return "userdata";
}
template <> inline const char* typeToString<int>()
{
	return "number|integer";
}
template <> inline const char* typeToString<uint32>()
{
	return "number|integer";
}
template <> inline const char* typeToString<const char*>()
{
	return "string";
}
template <> inline const char* typeToString<bool>()
{
	return "boolean";
}


template <typename T> inline bool isType(lua_State* L, int index)
{
	return lua_islightuserdata(L, index) != 0;
}
template <> inline bool isType<int>(lua_State* L, int index)
{
	return lua_isinteger(L, index) != 0;
}
template <> inline bool isType<Vec3>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0;
}
template <> inline bool isType<uint32>(lua_State* L, int index)
{
	return lua_isinteger(L, index) != 0;
}
template <> inline bool isType<int64>(lua_State* L, int index)
{
	return lua_isinteger(L, index) != 0;
}
template <> inline bool isType<bool>(lua_State* L, int index)
{
	return lua_isboolean(L, index) != 0;
}
template <> inline bool isType<float>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<const char*>(lua_State* L, int index)
{
	return lua_isstring(L, index) != 0;
}
template <> inline bool isType<void*>(lua_State* L, int index)
{
	return lua_islightuserdata(L, index) != 0;
}


template <typename T> inline void pushLua(lua_State* L, T value)
{
	lua_pushlightuserdata(L, value);
}
template <> inline void pushLua(lua_State* L, float value)
{
	lua_pushnumber(L, value);
}
inline void pushLua(lua_State* L, const Vec3& value)
{
	lua_createtable(L, 3, 0);

	lua_pushvalue(L, -1);
	lua_pushnumber(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushvalue(L, -1);
	lua_pushnumber(L, value.y);
	lua_rawseti(L, -2, 2);

	lua_pushvalue(L, -1);
	lua_pushnumber(L, value.z);
	lua_rawseti(L, -2, 3);
}
template <> inline void pushLua(lua_State* L, bool value)
{
	lua_pushboolean(L, value);
}
template <> inline void pushLua(lua_State* L, const char* value)
{
	lua_pushstring(L, value);
}
template <> inline void pushLua(lua_State* L, int value)
{
	lua_pushinteger(L, value);
}
template <> inline void pushLua(lua_State* L, unsigned int value)
{
	lua_pushinteger(L, value);
}
template <> inline void pushLua(lua_State* L, void* value)
{
	lua_pushlightuserdata(L, value);
}


inline void createSystemVariable(lua_State* L, const char* system, const char* var_name, void* value)
{
	if (lua_getglobal(L, system) == LUA_TNIL)
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushlightuserdata(L, value);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}


inline void createSystemVariable(lua_State* L, const char* system, const char* var_name, int value)
{
	if (lua_getglobal(L, system) == LUA_TNIL)
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushinteger(L, value);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}


inline void createSystemFunction(lua_State* L, const char* system, const char* var_name, lua_CFunction fn)
{
	if (lua_getglobal(L, system) == LUA_TNIL)
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, system);
		lua_getglobal(L, system);
	}
	lua_pushcfunction(L, fn);
	lua_setfield(L, -2, var_name);
	lua_pop(L, 1);
}


inline const char* luaTypeToString(int type)
{
	switch (type)
	{
		case LUA_TNUMBER: return "number";
		case LUA_TBOOLEAN: return "boolean";
		case LUA_TFUNCTION: return "function";
		case LUA_TLIGHTUSERDATA: return "light userdata";
		case LUA_TNIL: return "nil";
		case LUA_TSTRING: return "string";
		case LUA_TTABLE: return "table";
		case LUA_TUSERDATA: return "userdata";
	}
	return "Unknown";
}


inline void argError(lua_State* L, int index, const char* expected_type)
{
	char buf[128];
	copyString(buf, "expected ");
	catString(buf, expected_type);
	catString(buf, ", got ");
	int type = lua_type(L, index);
	catString(buf, LuaWrapper::luaTypeToString(type));
	luaL_argerror(L, index, buf);
}



template <typename T>
void argError(lua_State* L, int index)
{
	argError(L, index, typeToString<T>());
}


template <typename T>
T checkArg(lua_State* L, int index)
{
	if (!isType<T>(L, index))
	{
		argError<T>(L, index);
	}
	return toType<T>(L, index);
}


inline void checkTableArg(lua_State* L, int index)
{
	if(!lua_istable(L, index))
	{
		argError(L, index, "table");
	}
}


template <int N> struct FunctionCaller
{
	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R (*f)(ArgsF...),
											 lua_State* L,
											 Args... args)
	{
		typedef typename std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF...>>::type T;
		typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type RealT;
		checkArg<RealT>(L, sizeof...(ArgsF)-N + 1);
		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 1);
		return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
	}

	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R(*f)(lua_State*, ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef typename std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type RealT;
		checkArg<RealT>(L, sizeof...(ArgsF)-N + 1);
		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 1);
		return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
	}

	template <typename R, typename C, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callMethod(C* inst, R(C::*f)(ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef typename std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type RealT;
		checkArg<RealT>(L, sizeof...(ArgsF)-N + 2);

		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 2);
		return FunctionCaller<N - 1>::callMethod(inst, f, L, args..., a);
	}

	template <typename R, typename C, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callMethod(C* inst, R(C::*f)(lua_State*, ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef typename std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type RealT;
		checkArg<RealT>(L, sizeof...(ArgsF)-N + 2);
		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 2);
		return FunctionCaller<N - 1>::callMethod(inst, f, L, args..., a);
	}
};


template <> struct FunctionCaller<0>
{
	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R (*f)(ArgsF...),
											 lua_State*,
											 Args... args)
	{
		return f(args...);
	}


	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R (*f)(lua_State*, ArgsF...),
											 lua_State* L,
											 Args... args)
	{
		return f(L, args...);
	}

	template <typename R, typename C, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callMethod(C* inst, R(C::*f)(ArgsF...),
		lua_State*,
		Args... args)
	{
		return (inst->*f)(args...);
	}


	template <typename R, typename C, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callMethod(C* inst, R(C::*f)(lua_State*, ArgsF...),
		lua_State* L,
		Args... args)
	{
		return (inst->*f)(L, args...);
	}
};


template <typename R, typename... ArgsF>
int LUMIX_FORCE_INLINE callFunction(R (*f)(ArgsF...), lua_State* L)
{
	R v = FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
	pushLua(L, v);
	return 1;
}


template <typename... ArgsF>
int LUMIX_FORCE_INLINE callFunction(void (*f)(ArgsF...), lua_State* L)
{
	FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
	return 0;
}


template <typename R, typename... ArgsF>
int LUMIX_FORCE_INLINE callFunction(R (*f)(lua_State*, ArgsF...), lua_State* L)
{
	R v = FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
	pushLua(L, v);
	return 1;
}


template <typename... ArgsF>
int LUMIX_FORCE_INLINE callFunction(void (*f)(lua_State*, ArgsF...),
									lua_State* L)
{
	FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
	return 0;
}


template <typename T, T t> int wrap(lua_State* L)
{
	return callFunction(t, L);
}


template <typename C>
int LUMIX_FORCE_INLINE callMethod(void(C::*f)(), lua_State* L)
{
	auto* inst = checkArg<C*>(L, 1);
	(inst->*f)();
	return 0;
}


template <typename C, typename R>
int LUMIX_FORCE_INLINE callMethod(R(C::*f)(), lua_State* L)
{
	auto* inst = checkArg<C*>(L, 1);
	R v = (inst->*f)();
	pushLua(L, v);
	return 1;
}


template <typename C, typename... ArgsF>
int LUMIX_FORCE_INLINE callMethod(void(C::*f)(ArgsF...), lua_State* L)
{
	auto* inst = checkArg<C*>(L, 1);

	FunctionCaller<sizeof...(ArgsF)>::callMethod(inst, f, L);
	return 0;
}


template <typename C, typename R, typename... ArgsF>
int LUMIX_FORCE_INLINE callMethod(R (C::*f)(ArgsF...), lua_State* L)
{
	auto* inst = checkArg<C*>(L, 1);

	R v = FunctionCaller<sizeof...(ArgsF)>::callMethod(inst, f, L);
	pushLua(L, v);
	return 1;
}


template <typename C, typename T, T t> int wrapMethod(lua_State* L)
{
	return callMethod<C>(t, L);
}


} // namespace LuaWrapper


} // namespace Lumix
