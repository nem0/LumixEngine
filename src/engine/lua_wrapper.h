#pragma once


#include "engine/math.h"
#include "engine/metaprogramming.h"
#include "engine/path.h"
#include <lua.h>
#include <lualib.h>

namespace Lumix {

struct World;
struct CameraParams;
struct PipelineTexture;

namespace LuaWrapper {


#ifdef LUMIX_DEBUG
	struct LUMIX_ENGINE_API DebugGuard {
		DebugGuard(lua_State* L);
		DebugGuard(lua_State* L, int offset);
		~DebugGuard();

	private:
		lua_State* L;
		int top;
	};
#else
	struct LUMIX_ENGINE_API DebugGuard { 
		DebugGuard(lua_State* L) {} 
		DebugGuard(lua_State* L, int offset) {} 
	};
#endif

template <typename T, u32 C>
struct Array {
	T& operator[](u32 idx) { ASSERT(idx < size); return values[idx]; }
	T* begin() { return values; }
	T* end() { return values + size; }
	T values[C];
	u32 size = 0;
};

template <typename T>
struct Optional {
	T get(T default_value) { return valid ? value : default_value; }
	T value;
	bool valid = false;
};

using RefHandle = i32;

LUMIX_ENGINE_API int traceback (lua_State *L);
LUMIX_ENGINE_API bool pcall(lua_State* L, int nargs, int nres);
LUMIX_ENGINE_API bool execute(lua_State* L, StringView content, const char* name, int nresults);
LUMIX_ENGINE_API int getField(lua_State* L, int idx, const char* k);

// create reference to a value on top of stack, so it's not garbage collected and can be easily pushed to stack. Similar to luaL_ref
LUMIX_ENGINE_API RefHandle createRef(lua_State* L);
// release the reference, so it can be GCed
LUMIX_ENGINE_API void releaseRef(lua_State* L, RefHandle ref);
// push the reference on top of stack
LUMIX_ENGINE_API void pushRef(lua_State* L, RefHandle ref);

// get closure objects passed to `createSystemClosure`
template <typename T>
T* getClosureObject(lua_State* L) {
	int upvalue_index = lua_upvalueindex(1);
	if (!lua_islightuserdata(L, upvalue_index)) {
		ASSERT(false);
		luaL_error(L, "Invalid Lua closure");
	}
	return (T*)lua_tolightuserdata(L, upvalue_index);
}

LUMIX_ENGINE_API int luaL_loadbuffer(lua_State* L, const char* buff, size_t size, const char* name);
LUMIX_ENGINE_API void pushObject(lua_State* L, void* obj, StringView type_name);

template <typename T> inline bool isType(lua_State* L, int index)
{
	return lua_islightuserdata(L, index) != 0;
}
template <> inline bool isType<int>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<u16>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<Path>(lua_State* L, int index)
{
	return lua_isstring(L, index);
}
template <> inline bool isType<u8>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<EntityRef>(lua_State* L, int index)
{
	if (lua_istable(L, index) == 0) return false;
	const bool is_entity = getField(L, index, "_entity") == LUA_TNUMBER;
	lua_pop(L, 1);
	return is_entity;
}
template <> inline bool isType<EntityPtr>(lua_State* L, int index)
{
	if (lua_istable(L, index) == 0) return false;
	const int type = getField(L, index, "_entity");
	const bool is_entity = type == LUA_TNUMBER || type == LUA_TNIL;
	lua_pop(L, 1);
	return is_entity;
}
template <> inline bool isType<ComponentType>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<Vec3>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 3;
}
template <> inline bool isType<DVec3>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 3;
}
template <> inline bool isType<Vec4>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 4;
}
template <> inline bool isType<Vec2>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 2;
}
template <> inline bool isType<IVec2>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 2;
}
template <> inline bool isType<Matrix>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 16;
}
template <> inline bool isType<Quat>(lua_State* L, int index)
{
	return lua_istable(L, index) != 0 && lua_objlen(L, index) == 4;
}
template <> inline bool isType<u32>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<u64>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
}
template <> inline bool isType<i64>(lua_State* L, int index)
{
	return lua_isnumber(L, index) != 0;
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

template <typename T> inline T toType(lua_State* L, int index)
{
	return (T)lua_touserdata(L, index);
}

template <> CameraParams toType(lua_State* L, int idx);

template <> inline int toType(lua_State* L, int index) { return (int)lua_tointeger(L, index); }
template <> inline u16 toType(lua_State* L, int index) { return (u16)lua_tointeger(L, index); }
template <> inline Path toType(lua_State* L, int index) { return Path(lua_tostring(L, index)); }
template <> inline u8 toType(lua_State* L, int index) { return (u8)lua_tointeger(L, index); }
template <> inline ComponentType toType(lua_State* L, int index) { return {(int)lua_tointeger(L, index)}; }

template <> inline EntityRef toType(lua_State* L, int index) {
	if (getField(L, index, "_entity") == LUA_TNUMBER) {
		const EntityRef e = {(i32)lua_tointeger(L, -1)};
		lua_pop(L, 1);
		return e;
	}
	lua_pop(L, 1);
	ASSERT(false);
	return {};
}

template <> inline EntityPtr toType(lua_State* L, int index) {
	if (getField(L, index, "_entity") == LUA_TNUMBER) {
		const EntityRef e = {(i32)lua_tointeger(L, -1)};
		lua_pop(L, 1);
		return e;
	}
	lua_pop(L, 1);
	return INVALID_ENTITY;
}

template <> inline Vec3 toType(lua_State* L, int index) {
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

template <> inline IVec3 toType(lua_State* L, int index) {
	IVec3 v;
	lua_rawgeti(L, index, 1);
	v.x = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 3);
	v.z = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

template <> inline DVec3 toType(lua_State* L, int index) {
	DVec3 v;
	lua_rawgeti(L, index, 1);
	v.x = (double)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (double)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 3);
	v.z = (double)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

template <> inline Vec4 toType(lua_State* L, int index) {
	Vec4 v;
	lua_rawgeti(L, index, 1);
	v.x = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 3);
	v.z = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 4);
	v.w = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

template <> inline Quat toType(lua_State* L, int index) {
	Quat v;
	lua_rawgeti(L, index, 1);
	v.x = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 3);
	v.z = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 4);
	v.w = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

template <> inline Vec2 toType(lua_State* L, int index) {
	Vec2 v;
	lua_rawgeti(L, index, 1);
	v.x = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

template <> inline Matrix toType(lua_State* L, int index) {
	Matrix v;
	for (int i = 0; i < 16; ++i) {
		lua_rawgeti(L, index, i + 1);
		(&(v.columns[0].x))[i] = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	return v;
}

template <> inline IVec2 toType(lua_State* L, int index) {
	IVec2 v;
	lua_rawgeti(L, index, 1);
	v.x = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, index, 2);
	v.y = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return v;
}

template <> inline i64 toType(lua_State* L, int index) { return (i64)lua_tointeger(L, index); }
template <> inline u32 toType(lua_State* L, int index) { return (u32)lua_tointeger(L, index); }
template <> inline u64 toType(lua_State* L, int index) { return (u64)lua_tointeger(L, index); }
template <> inline bool toType(lua_State* L, int index) { return lua_toboolean(L, index) != 0; }
template <> inline float toType(lua_State* L, int index) { return (float)lua_tonumber(L, index); }
template <> inline void* toType(lua_State* L, int index) { return lua_touserdata(L, index); }
template <> inline const char* toType(lua_State* L, int index) {
	const char* res = lua_tostring(L, index);
	return res ? res : "";
}

template <typename T> inline bool checkField(lua_State* L, int idx, const char* k, T* out)
{
	lua_getfield(L, idx, k);
	if(!isType<T>(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	*out = toType<T>(L, -1);
	lua_pop(L, 1);
	return true;
}

template <typename T, typename F> bool forEachArrayItem(lua_State* L, int index, const char* error_msg, F&& func)
{
	if (!lua_istable(L, index)) {
		if (error_msg) luaL_argerror(L, index, error_msg);
		return false;
	}
	
	bool all_match = true;
	const int n = (int)lua_objlen(L, index);
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, index, i + 1);
		if(isType<T>(L, -1)) {
			func(toType<T>(L, -1));
		}
		else if (error_msg) {
			lua_pop(L, 1);
			luaL_argerror(L, index, error_msg);
		}
		else {
			all_match = false;
		}
		lua_pop(L, 1);
	}
	return all_match;
}

template <typename T> inline const char* typeToString()
{
	return "userdata";
}
template <> inline const char* typeToString<int>()
{
	return "number|integer";
}
template <> inline const char* typeToString<Vec3>()
{
	return "Vec3";
}
template <> inline const char* typeToString<DVec3>()
{
	return "DVec3";
}
template <> inline const char* typeToString<u16>()
{
	return "number|u16";
}
template <> inline const char* typeToString<Path>()
{
	return "path";
}
template <> inline const char* typeToString<u8>()
{
	return "number|u8";
}
template <> inline const char* typeToString<EntityRef>()
{
	return "entity";
}
template <> inline const char* typeToString<ComponentType>()
{
	return "component type";
}
template <> inline const char* typeToString<u32>()
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

template <> inline const char* typeToString<float>()
{
	return "number|float";
}

template <typename T> inline void push(lua_State* L, T* value)
{
	lua_pushlightuserdata(L, value);
}

void push(lua_State* L, const CameraParams& value);
void push(lua_State* L, const PipelineTexture& value);

template <typename T> inline void push(lua_State* L, const T* value)
{
	lua_pushlightuserdata(L, (T*)value);
}

inline void push(lua_State* L, float value)
{
	lua_pushnumber(L, value);
}
inline void push(lua_State* L, ComponentType value)
{
	lua_pushinteger(L, value.index);
}
inline void push(lua_State* L, const Vec2& value)
{
	lua_createtable(L, 2, 0);

	lua_pushnumber(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushnumber(L, value.y);
	lua_rawseti(L, -2, 2);
}
inline void push(lua_State* L, const Matrix& value)
{
	lua_createtable(L, 16, 0);

	for (int i = 0; i < 16; ++i)
	{
		lua_pushnumber(L, (&value.columns[0].x)[i]);
		lua_rawseti(L, -2, i + 1);
	}
}
inline void push(lua_State* L, const IVec2& value)
{
	lua_createtable(L, 2, 0);

	lua_pushinteger(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushinteger(L, value.y);
	lua_rawseti(L, -2, 2);
}
inline void push(lua_State* L, const IVec3& value)
{
	lua_createtable(L, 3, 0);

	lua_pushinteger(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushinteger(L, value.y);
	lua_rawseti(L, -2, 2);

	lua_pushinteger(L, value.z);
	lua_rawseti(L, -2, 3);
}
inline void push(lua_State* L, const Vec3& value)
{
	lua_createtable(L, 3, 0);

	lua_pushnumber(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushnumber(L, value.y);
	lua_rawseti(L, -2, 2);

	lua_pushnumber(L, value.z);
	lua_rawseti(L, -2, 3);
}
inline void push(lua_State* L, const DVec3& value)
{
	lua_createtable(L, 3, 0);

	lua_pushnumber(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushnumber(L, value.y);
	lua_rawseti(L, -2, 2);

	lua_pushnumber(L, value.z);
	lua_rawseti(L, -2, 3);
}
inline void push(lua_State* L, const Vec4& value)
{
	lua_createtable(L, 4, 0);

	lua_pushnumber(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushnumber(L, value.y);
	lua_rawseti(L, -2, 2);

	lua_pushnumber(L, value.z);
	lua_rawseti(L, -2, 3);

	lua_pushnumber(L, value.w);
	lua_rawseti(L, -2, 4);
}
inline void push(lua_State* L, const Quat& value)
{
	lua_createtable(L, 4, 0);

	lua_pushnumber(L, value.x);
	lua_rawseti(L, -2, 1);

	lua_pushnumber(L, value.y);
	lua_rawseti(L, -2, 2);

	lua_pushnumber(L, value.z);
	lua_rawseti(L, -2, 3);

	lua_pushnumber(L, value.w);
	lua_rawseti(L, -2, 4);
}
inline void push(lua_State* L, bool value)
{
	lua_pushboolean(L, value);
}
inline void push(lua_State* L, const char* value)
{
	lua_pushstring(L, value);
}
inline void push(lua_State* L, char* value)
{
	lua_pushstring(L, value);
}
inline void push(lua_State* L, int value)
{
	lua_pushinteger(L, value);
}
inline void push(lua_State* L, u16 value)
{
	lua_pushinteger(L, value);
}
inline void push(lua_State* L, u8 value)
{
	lua_pushinteger(L, value);
}
inline void push(lua_State* L, u32 value)
{
	lua_pushinteger(L, value);
}
inline void push(lua_State* L, u64 value)
{
	lua_pushnumber(L, double(value));
}
template <> inline void push(lua_State* L, void* value)
{
	lua_pushlightuserdata(L, value);
}

LUMIX_ENGINE_API void createSystemVariable(lua_State* L, const char* system, const char* var_name, void* value);
LUMIX_ENGINE_API void createSystemVariable(lua_State* L, const char* system, const char* var_name, int value);
LUMIX_ENGINE_API void createSystemFunction(lua_State* L, const char* system, const char* var_name, lua_CFunction fn);
LUMIX_ENGINE_API void createSystemClosure(lua_State* L, const char* system, void* system_ptr, const char* var_name, lua_CFunction fn);
LUMIX_ENGINE_API const char* luaTypeToString(int type);
LUMIX_ENGINE_API void argError(lua_State* L, int index, const char* expected_type);
LUMIX_ENGINE_API void checkTableArg(lua_State* L, int index);
LUMIX_ENGINE_API bool getOptionalStringField(lua_State* L, int idx, const char* field_name, Span<char> out);
LUMIX_ENGINE_API void push(lua_State* L, EntityRef value);
LUMIX_ENGINE_API bool toEntity(lua_State* L, int idx, World*& world, EntityRef& entity);
LUMIX_ENGINE_API void pushEntity(lua_State* L, EntityPtr value, World* world);
LUMIX_ENGINE_API bool checkStringField(lua_State* L, int idx, const char* k, Span<char> out);

template <typename T> inline void setField(lua_State* L, int table_idx, const char* name, T value)
{
 	push(L, value);
	lua_setfield(L, table_idx - 1, name);
}

template <typename T> void argError(lua_State* L, int index)
{
	argError(L, index, typeToString<T>());
}

template <typename T> struct Tag {};

template <typename T> T checkArg(lua_State* L, int index, Tag<T>)
{
	if (!isType<T>(L, index))
	{
		argError<T>(L, index);
	}
	return toType<T>(L, index);
}

template <typename T> Optional<T> checkArg(lua_State* L, int index, Tag<Optional<T>>)
{
	Optional<T> res;
	int type = lua_type(L, index);
	if (type == LUA_TNONE || type == LUA_TNIL) {
		res.valid = false;
		return res;
	}
	if (!isType<T>(L, index)) {
		luaL_argerror(L, index, "unexpected type");
	}
	else {
		res.value = toType<T>(L, index);
		res.valid = true;
	}
	return res;
}

template <typename T, u32 C> Array<T, C> checkArg(lua_State* L, int index, Tag<Array<T, C>>)
{
	Array<T, C> res;
	if (!lua_istable(L, index)) {
		luaL_argerror(L, index, "expected array");
	}
	res.size = (u32)lua_objlen(L, index);
	if (res.size > C) {
		luaL_argerror(L, index, "array too long");
	}

	for (u32 i = 0; i < res.size; ++i) {
		lua_rawgeti(L, index, i + 1);
		if (!isType<T>(L, -1)) {
			luaL_error(L, "expected array of %s as %d-th argument", typeToString<T>(), index);
		}
		res.values[i] = toType<T>(L, -1);
		lua_pop(L, 1);
	}

	return res;
}

template <typename T> T checkArg(lua_State* L, int index)
{
	return checkArg(L, index, Tag<T>{});
}

template <typename T>
bool getOptionalField(lua_State* L, int idx, const char* field_name, T* out)
{
	bool res = false;
	if (LuaWrapper::getField(L, idx, field_name) != LUA_TNIL && isType<T>(L, -1))
	{
		*out = toType<T>(L, -1);
		res = true;
	}
	lua_pop(L, 1);
	return res;
}

template <typename T>
void getOptionalFlagField(lua_State* L, int idx, const char* field_name, T* out, T flag, bool default_value)
{
	bool value = default_value;
	if (LuaWrapper::getField(L, idx, field_name) != LUA_TNIL && isType<bool>(L, -1)) {
		value = toType<bool>(L, -1);
	}
	lua_pop(L, 1);
	if (value) *out = *out | flag;
	else *out = *out & ~flag;
}


namespace details
{


template <typename T> struct Caller;


template <int... indices>
struct Caller<Indices<indices...>>
{
	template <typename R, typename... Args>
	static int callFunction(R (*f)(Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 1);
		R v = f(checkArg<RemoveCVR<Args>>(L, indices)...);
		push(L, v);
		return 1;
	}


	template <typename... Args>
	static int callFunction(void (*f)(Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 0);
		f(checkArg<RemoveCVR<Args>>(L, indices)...);
		return 0;
	}


	template <typename R, typename... Args>
	static int callFunction(R(*f)(lua_State*, Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 1);
		R v = f(L, checkArg<RemoveCVR<Args>>(L, indices)...);
		push(L, v);
		return 1;
	}


	template <typename... Args>
	static int callFunction(void(*f)(lua_State*, Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 0);
		f(L, checkArg<RemoveCVR<Args>>(L, indices)...);
		return 0;
	}


	template <typename C, typename... Args>
	static int callMethod(C* inst, void(C::*f)(lua_State*, Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 0);
		(inst->*f)(L, checkArg<RemoveCVR<Args>>(L, indices)...);
		return 0;
	}


	template <typename R, typename C, typename... Args>
	static int callMethod(C* inst, R(C::*f)(lua_State*, Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 1);
		R v = (inst->*f)(L, checkArg<RemoveCVR<Args>>(L, indices)...);
		push(L, v);
		return 1;
	}


	template <typename R, typename C, typename... Args>
	static int callMethod(C* inst, R(C::*f)(lua_State*, Args...) const, lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 1);
		R v = (inst->*f)(L, checkArg<RemoveCVR<Args>>(L, indices)...);
		push(L, v);
		return 1;
	}


	template <typename C, typename... Args>
	static int callMethod(C* inst, void(C::*f)(Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 0);
		(inst->*f)(checkArg<RemoveCVR<Args>>(L, indices)...);
		return 0;
	}


	template <typename R, typename C, typename... Args>
	static int callMethod(C* inst, R(C::*f)(Args...), lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 1);
		R v = (inst->*f)(checkArg<RemoveCVR<Args>>(L, indices)...);
		push(L, v);
		return 1;
	}


	template <typename R, typename C, typename... Args>
	static int callMethod(C* inst, R(C::*f)(Args...) const, lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L, 1);
		R v = (inst->*f)(checkArg<RemoveCVR<Args>>(L, indices)...);
		push(L, v);
		return 1;
	}
};


template <typename R, typename... Args> constexpr int arity(R (*f)(Args...))
{
	return sizeof...(Args);
}


template <typename R, typename... Args> constexpr int arity(R (*f)(lua_State*, Args...))
{
	return sizeof...(Args);
}


template <typename R, typename C, typename... Args> constexpr int arity(R (C::*f)(Args...))
{
	return sizeof...(Args);
}


template <typename R, typename C, typename... Args> constexpr int arity(R(C::*f)(Args...) const)
{
	return sizeof...(Args);
}


template <typename R, typename C, typename... Args> constexpr int arity(R (C::*f)(lua_State*, Args...))
{
	return sizeof...(Args);
}


template <typename R, typename C, typename... Args> constexpr int arity(R (C::*f)(lua_State*, Args...) const)
{
	return sizeof...(Args);
}


} // namespace details


template <auto t> int wrap(lua_State* L)
{
	using indices = typename BuildIndices<0, details::arity(t)>::result;
	return details::Caller<indices>::callFunction(t, L);
}


template <auto t> int wrapMethod(lua_State* L)
{
	using indices = typename BuildIndices<1, details::arity(t)>::result;
	using C = typename ClassOf<decltype(t)>::Type;
	auto* inst = checkArg<C*>(L, 1);
	return details::Caller<indices>::callMethod(inst, t, L);
}


template <auto t> int wrapMethodClosure(lua_State* L)
{
	using C = typename ClassOf<decltype(t)>::Type;
	using indices = typename BuildIndices<0, details::arity(t)>::result;
	int index = lua_upvalueindex(1);
	if (!lua_islightuserdata(L, index)) {
		ASSERT(false);
		luaL_error(L, "Invalid Lua closure");
	}
	auto* inst = (C*)lua_tolightuserdata(L, index);
	return details::Caller<indices>::callMethod(inst, t, L);
}


} // namespace LuaWrapper
} // namespace Lumix
