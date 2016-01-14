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
	return "number";
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
template <> inline void pushLua(lua_State* L, void* value)
{
	lua_pushlightuserdata(L, value);
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


template <typename T>
bool checkParameterType(lua_State* L, int index)
{
	if (!isType<T>(L, index))
	{
		int depth = 0;
		lua_Debug entry;

		int type = lua_type(L, index);
		auto er = g_log_error.log("lua");
		er << "Wrong argument " << index << " of type " << LuaWrapper::luaTypeToString(type)
			<< " in:\n";
		while (lua_getstack(L, depth, &entry))
		{
			int status = lua_getinfo(L, "Sln", &entry);
			ASSERT(status);
			er << entry.short_src << "(" << entry.currentline
				<< "): " << (entry.name ? entry.name : "?") << "\n";
			depth++;
		}
		er << typeToString<T>() << " expected\n";
		return false;
	}
	return true;
}


template <int N> struct FunctionCaller
{
	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R (*f)(ArgsF...),
											 lua_State* L,
											 Args... args)
	{
		typedef std::tuple_element<sizeof...(ArgsF)-N,
								   std::tuple<ArgsF...>>::type T;
		if (!checkParameterType<T>(L, sizeof...(ArgsF)-N + 1)) return R();
		typedef std::remove_cv<std::remove_reference<T>::type>::type RealT;
		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 1);
		return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
	}

	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R(*f)(lua_State*, ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		if (!checkParameterType<T>(L, sizeof...(ArgsF)-N + 1)) return R();
		typedef std::remove_cv<std::remove_reference<T>::type>::type RealT;
		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 1);
		return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
	}

	template <typename R, typename C, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callMethod(C* inst, R(C::*f)(ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		if (!checkParameterType<T>(L, sizeof...(ArgsF)-N + 2)) return R();
		typedef std::remove_cv<std::remove_reference<T>::type>::type RealT;

		RealT a = toType<RealT>(L, sizeof...(ArgsF)-N + 2);
		return FunctionCaller<N - 1>::callMethod(inst, f, L, args..., a);
	}

	template <typename R, typename C, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callMethod(C* inst, R(C::*f)(lua_State*, ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		if (!checkParameterType<T>(L, sizeof...(ArgsF)-N + 2)) return R();
		typedef std::remove_cv<std::remove_reference<T>::type>::type RealT;
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
	auto* inst = toType<C*>(L, 1);
	(inst->*f)();
	return 0;
}


template <typename C, typename R>
int LUMIX_FORCE_INLINE callMethod(R(C::*f)(), lua_State* L)
{
	auto* inst = toType<C*>(L, 1);
	R v = (inst->*f)();
	pushLua(L, v);
	return 1;
}


template <typename C, typename... ArgsF>
int LUMIX_FORCE_INLINE callMethod(void(C::*f)(ArgsF...), lua_State* L)
{
	auto* inst = toType<C*>(L, 1);

	FunctionCaller<sizeof...(ArgsF)>::callMethod(inst, f, L);
	return 0;
}


template <typename C, typename R, typename... ArgsF>
int LUMIX_FORCE_INLINE callMethod(R (C::*f)(ArgsF...), lua_State* L)
{
	auto* inst = toType<C*>(L, 1);

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