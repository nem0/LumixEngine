#include "core/crc32.h"
#include "core/input_system.h"
#include "core/lua_wrapper.h"
#include "engine.h"
#include "iplugin.h"
#include "graphics/render_scene.h"
#include "universe/universe.h"


namespace Lumix
{


namespace LuaAPI
{


static void* getScene(UniverseContext* ctx, const char* name)
{
	if (ctx)
	{
		uint32_t hash = crc32(name);
		for (auto* scene : ctx->m_scenes)
		{
			if (crc32(scene->getPlugin().getName()) == hash)
			{
				return scene;
			}
		}
	}
	return nullptr;
}


static int createComponent(IScene* scene, const char* type, int entity_idx)
{
	if (!scene)
	{
		return -1;
	}
	Entity e(entity_idx);
	return scene->createComponent(crc32(type), e);
}


static int multVecQuat(lua_State* L)
{
	Vec3 v;
	v.x = (float)lua_tonumber(L, 1);
	v.y = (float)lua_tonumber(L, 2);
	v.z = (float)lua_tonumber(L, 3);
	Vec3 axis;
	axis.x = (float)lua_tonumber(L, 4);
	axis.y = (float)lua_tonumber(L, 5);
	axis.z = (float)lua_tonumber(L, 6);
	Quat q(axis, (float)lua_tonumber(L, 7));

	Vec3 res = q * v;

	lua_createtable(L, 3, 0);
	int table_idx = lua_gettop(L);

	lua_pushnumber(L, res.x);
	lua_rawseti(L, table_idx, 0);

	lua_pushnumber(L, res.y);
	lua_rawseti(L, table_idx, 1);

	lua_pushnumber(L, res.z);
	lua_rawseti(L, table_idx, 2);

	return 1;
}


static void logError(const char* text)
{
	g_log_error.log("lua script") << text;
}


static void logInfo(const char* text)
{
	g_log_info.log("lua script") << text;
}


static void
setEntityPosition(Universe* univ, int entity_index, float x, float y, float z)
{
	univ->setPosition(entity_index, x, y, z);
}


static void setEntityRotation(
	Universe* univ, int entity_index, float x, float y, float z, float angle)
{
	univ->setRotation(entity_index, Quat(Vec3(x, y, z), angle));
}


static void setRenderablePath(IScene* scene, int component, const char* path)
{
	RenderScene* render_scene = (RenderScene*)scene;
	render_scene->setRenderablePath(component, path);
}


static float getInputActionValue(Engine* engine, uint32_t action)
{
	auto v = engine->getInputSystem().getActionValue(action);
	return v;
}


static void addInputAction(Engine* engine, uint32_t action, int type, int key)
{
	engine->getInputSystem().addAction(
		action, Lumix::InputSystem::InputType(type), key);
}


} // namespace LuaAPI


static void
registerCFunction(lua_State* L, const char* name, lua_CFunction func)
{
	lua_pushcfunction(L, func);
	lua_setglobal(L, name);
}


void registerEngineLuaAPI(Engine& engine, UniverseContext& ctx, lua_State* L)
{
	lua_pushlightuserdata(L, &ctx);
	lua_setglobal(L, "g_universe_context");

	lua_pushlightuserdata(L, ctx.m_universe);
	lua_setglobal(L, "g_universe");

	lua_pushlightuserdata(L, &engine);
	lua_setglobal(L, "g_engine");

	registerCFunction(
		L,
		"API_getScene",
		LuaWrapper::wrap<decltype(&LuaAPI::getScene), LuaAPI::getScene>);

	registerCFunction(L,
					  "API_setEntityPosition",
					  LuaWrapper::wrap<decltype(&LuaAPI::setEntityPosition),
									   LuaAPI::setEntityPosition>);

	registerCFunction(L,
					  "API_setEntityRotation",
					  LuaWrapper::wrap<decltype(&LuaAPI::setEntityRotation),
									   LuaAPI::setEntityRotation>);

	registerCFunction(L,
					  "API_createComponent",
					  LuaWrapper::wrap<decltype(&LuaAPI::createComponent),
									   LuaAPI::createComponent>);

	registerCFunction(L,
					  "API_setRenderablePath",
					  LuaWrapper::wrap<decltype(&LuaAPI::setRenderablePath),
									   LuaAPI::setRenderablePath>);

	registerCFunction(L,
					  "API_getInputActionValue",
					  LuaWrapper::wrap<decltype(&LuaAPI::getInputActionValue),
									   LuaAPI::getInputActionValue>);
	registerCFunction(L,
					  "API_addInputAction",
					  LuaWrapper::wrap<decltype(&LuaAPI::addInputAction),
									   LuaAPI::addInputAction>);

	registerCFunction(
		L,
		"API_logError",
		LuaWrapper::wrap<decltype(&LuaAPI::logError), LuaAPI::logError>);

	registerCFunction(
		L,
		"API_logInfo",
		LuaWrapper::wrap<decltype(&LuaAPI::logInfo), LuaAPI::logInfo>);

	registerCFunction(L, "API_multVecQuat", &LuaAPI::multVecQuat);
}


} // namespace Lumix