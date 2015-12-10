#include "core/crc32.h"
#include "core/input_system.h"
#include "core/lua_wrapper.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "iplugin.h"
#include "lua_script/lua_script_system.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"


namespace Lumix
{


namespace LuaAPI
{


static int getEnvironment(lua_State* L)
{
	if (!LuaWrapper::checkParameterType<void*>(L, 1) || !LuaWrapper::checkParameterType<int>(L, 2))
	{
		lua_pushnil(L);
		return 1;
	}

	LuaScriptScene* scene = (LuaScriptScene*)lua_touserdata(L, 1);
	Entity entity = (Entity)lua_tointeger(L, 2);

	int env = scene->getEnvironment(entity);

	if (env < 0)
	{
		lua_pushnil(L);
	}
	else
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
	}
	return 1;
}


static Texture* getMaterialTexture(Material* material, int texture_index)
{
	if (!material) return nullptr;
	return material->getTexture(texture_index);
}


static Material* getTerrainMaterial(RenderScene* scene, Entity entity)
{
	if (!scene) return nullptr;
	ComponentIndex cmp = scene->getTerrainComponent(entity);
	if (cmp < 0) return nullptr;
	return scene->getTerrainMaterial(cmp);
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


static void setEntityPosition(Universe* univ, int entity_index, float x, float y, float z)
{
	univ->setPosition(entity_index, x, y, z);
}


static void setEntityRotation(Universe* univ,
	int entity_index,
	float x,
	float y,
	float z,
	float angle)
{
	if (entity_index < 0 || entity_index > univ->getEntityCount()) return;

	univ->setRotation(entity_index, Quat(Vec3(x, y, z), angle));
}


static void setEntityLocalRotation(IScene* hierarchy,
	Entity entity,
	float x,
	float y,
	float z,
	float angle)
{
	if (entity == INVALID_ENTITY) return;

	static_cast<Hierarchy*>(hierarchy)->setLocalRotation(entity, Quat(Vec3(x, y, z), angle));
}


static void setRenderablePath(IScene* scene, int component, const char* path)
{
	RenderScene* render_scene = (RenderScene*)scene;
	render_scene->setRenderablePath(component, path);
}


static float getInputActionValue(Engine* engine, uint32 action)
{
	auto v = engine->getInputSystem().getActionValue(action);
	return v;
}


static void addInputAction(Engine* engine, uint32 action, int type, int key, int controller_id)
{
	engine->getInputSystem().addAction(
		action, Lumix::InputSystem::InputType(type), key, controller_id);
}


} // namespace LuaAPI


void registerUniverse(UniverseContext* ctx, lua_State* L)
{
	lua_pushlightuserdata(L, ctx);
	lua_setglobal(L, "g_universe_context");
	
	for (auto* scene : ctx->m_scenes)
	{
		const char* name = scene->getPlugin().getName();
		char tmp[128];

		copyString(tmp, "g_scene_");
		catString(tmp, name);
		lua_pushlightuserdata(L, scene);
		lua_setglobal(L, tmp);
	}

	lua_pushlightuserdata(L, ctx ? ctx->m_universe : nullptr);
	lua_setglobal(L, "g_universe");
}


void registerEngineLuaAPI(LuaScriptScene& scene, Engine& engine, lua_State* L)
{
	lua_pushlightuserdata(L, &engine);
	lua_setglobal(L, "g_engine");
	
	#define REGISTER_FUNCTION(name) \
		scene.registerFunction("Engine", #name, LuaWrapper::wrap<decltype(&LuaAPI::name), LuaAPI::name>)

	REGISTER_FUNCTION(createComponent);
	REGISTER_FUNCTION(getEnvironment);
	REGISTER_FUNCTION(getMaterialTexture);
	REGISTER_FUNCTION(getTerrainMaterial);
	REGISTER_FUNCTION(setEntityPosition);
	REGISTER_FUNCTION(setEntityRotation);
	REGISTER_FUNCTION(setEntityLocalRotation);
	REGISTER_FUNCTION(setRenderablePath);
	REGISTER_FUNCTION(getInputActionValue);
	REGISTER_FUNCTION(addInputAction);
	REGISTER_FUNCTION(logError);
	REGISTER_FUNCTION(logInfo);
	REGISTER_FUNCTION(logInfo);

	scene.registerFunction("Engine", "multVecQuat", &LuaAPI::multVecQuat);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix