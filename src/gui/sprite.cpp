#include "sprite.h"
#include "core/log.h"
#include "engine/lua_wrapper.h"
#include "engine/resource_manager.h"
#include "core/stream.h"
#include "core/string.h"
#include "renderer/texture.h"


namespace Lumix
{


const ResourceType Sprite::TYPE("sprite");


Sprite::Sprite(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_texture(nullptr)
{
}


void Sprite::unload()
{
	if (!m_texture) return;
	
	m_texture->decRefCount();
	m_texture = nullptr;
}


void Sprite::setTexture(const Path& path)
{
	if (m_texture) {
		m_texture->decRefCount();
	}

	if (path.isEmpty()) {
		m_texture = nullptr;
	} else {
		m_texture = (Texture*)getResourceManager().getOwner().load<Texture>(path);
	}
}


void Sprite::serialize(OutputMemoryStream& out)
{
	ASSERT(isReady());
	out << "type " << (type == PATCH9 ? "\"patch9\"\n" : "\"simple\"\n");
	out << "top(" << top << ")\n";
	out << "bottom(" << bottom << ")\n";
	out << "left(" << left << ")\n";
	out << "right(" << right << ")\n";
	out << "texture \"" << (m_texture ? m_texture->getPath() : Path()) << "\"";
}

namespace LuaSpriteAPI {
	static int type(lua_State* L) {
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Sprite* sprite = (Sprite*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		const char* tmp = LuaWrapper::checkArg<const char*>(L, 1);
		sprite->type = equalIStrings(tmp, "simple") ? Sprite::SIMPLE : Sprite::PATCH9; 
		return 0;
	}

	static int texture(lua_State* L) {
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Sprite* sprite = (Sprite*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		const char* tmp = LuaWrapper::checkArg<const char*>(L, 1);
		sprite->setTexture(Path(tmp));
		return 0;
	}

	static int top(lua_State* L) {
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Sprite* sprite = (Sprite*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		sprite->top = LuaWrapper::checkArg<i32>(L, 1);
		return 0;
	}

	static int bottom(lua_State* L) {
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Sprite* sprite = (Sprite*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		sprite->bottom = LuaWrapper::checkArg<i32>(L, 1);
		return 0;
	}

	static int left(lua_State* L) {
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Sprite* sprite = (Sprite*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		sprite->left = LuaWrapper::checkArg<i32>(L, 1);
		return 0;
	}

	static int right(lua_State* L) {
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Sprite* sprite = (Sprite*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		sprite->right = LuaWrapper::checkArg<i32>(L, 1);
		return 0;
	}
}

bool Sprite::load(Span<const u8> mem) {
	lua_State* L = luaL_newstate();

	#define DEFINE_LUA_FUNC(func) \
		lua_pushcfunction(L, LuaSpriteAPI::func, #func); \
		lua_setfield(L, LUA_GLOBALSINDEX, #func); 
	
	DEFINE_LUA_FUNC(type);
	DEFINE_LUA_FUNC(texture);
	DEFINE_LUA_FUNC(top);
	DEFINE_LUA_FUNC(bottom);
	DEFINE_LUA_FUNC(left);
	DEFINE_LUA_FUNC(right);

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this"); 

	#undef DEFINE_LUA_FUNC

	bool res = LuaWrapper::execute(L, StringView((const char*)mem.begin(), mem.length()), getPath().c_str(), 0);
	lua_close(L);

	return res;
}


} // namespace Lumix