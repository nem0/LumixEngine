#pragma once


#include "core/log.h"
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
template <> inline int64_t toType(lua_State* L, int index)
{
	return (int64_t)lua_tointeger(L, index);
}
template <> inline uint32_t toType(lua_State* L, int index)
{
	return (uint32_t)lua_tointeger(L, index);
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


template <typename T> inline bool isType(lua_State* L, int index)
{
	return lua_islightuserdata(L, index) != 0;
}
template <> inline bool isType<int>(lua_State* L, int index)
{
	return lua_isinteger(L, index) != 0;
}
template <> inline bool isType<uint32_t>(lua_State* L, int index)
{
	return lua_isinteger(L, index) != 0;
}
template <> inline bool isType<int64_t>(lua_State* L, int index)
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
	lua_pushnumber(L, value);
}
template <> inline void pushLua(lua_State* L, float value)
{
	lua_pushnumber(L, value);
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


template <int N> struct FunctionCaller
{
	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R (*f)(ArgsF...),
											 lua_State* L,
											 Args... args)
	{
		typedef std::tuple_element<sizeof...(ArgsF)-N,
								   std::tuple<ArgsF...>>::type T;
		if (!isType<T>(L, sizeof...(ArgsF)-N + 1))
		{
			lua_Debug entry;
			int depth = 0;

			auto er = g_log_error.log("lua");
			er << "Wrong arguments in\n";
			while (lua_getstack(L, depth, &entry))
			{
				int status = lua_getinfo(L, "Sln", &entry);
				ASSERT(status);
				er << entry.short_src << "(" << entry.currentline
				   << "): " << (entry.name ? entry.name : "?") << "\n";
				depth++;
			}
			return R();
		}
		T a = toType<T>(L, sizeof...(ArgsF)-N + 1);
		return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
	}

	template <typename R, typename... ArgsF, typename... Args>
	static LUMIX_FORCE_INLINE R callFunction(R(*f)(lua_State*, ArgsF...),
		lua_State* L,
		Args... args)
	{
		typedef std::tuple_element<sizeof...(ArgsF)-N,
			std::tuple<ArgsF... >> ::type T;
		if (!isType<T>(L, sizeof...(ArgsF)-N + 1))
		{
			lua_Debug entry;
			int depth = 0;

			auto er = g_log_error.log("lua");
			er << "Wrong arguments in\n";
			while (lua_getstack(L, depth, &entry))
			{
				int status = lua_getinfo(L, "Sln", &entry);
				ASSERT(status);
				er << entry.short_src << "(" << entry.currentline
					<< "): " << (entry.name ? entry.name : "?") << "\n";
				depth++;
			}
			return R();
		}
		T a = toType<T>(L, sizeof...(ArgsF)-N + 1);
		return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
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


} // namespace LuaWrapper


} // namespace Lumix