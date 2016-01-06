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


static void addDebugCross(RenderScene* scene, Vec3 pos, float size, int color, float life)
{
	scene->addDebugCross(pos, size, color, life);
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
	if (!scene) return -1;
	Entity e(entity_idx);
	uint32 hash = crc32(type);
	if (scene->getComponent(e, hash) != INVALID_COMPONENT)
	{
		g_log_error.log("script") << "Component " << type << " already exists in entity "
								  << entity_idx;
		return -1;
	}

	return scene->createComponent(hash, e);
}


static int getEntityPosition(lua_State* L)
{
	if (!LuaWrapper::checkParameterType<void*>(L, 1) ||
		!LuaWrapper::checkParameterType<Entity>(L, 2))
	{
		lua_pushnil(L);
		return 1;
	}

	auto* universe = LuaWrapper::toType<Universe*>(L, 1);
	Entity entity = LuaWrapper::toType<Entity>(L, 2);
	Vec3 pos = universe->getPosition(entity);
	LuaWrapper::pushLua(L, pos);
	return 1;
}

static int getEntityDirection(lua_State* L)
{
	if (!LuaWrapper::checkParameterType<void*>(L, 1) ||
		!LuaWrapper::checkParameterType<float>(L, 2))
	{
		lua_pushnil(L);
		return 1;
	}

	auto* universe = LuaWrapper::toType<Universe*>(L, 1);
	Entity entity = LuaWrapper::toType<Entity>(L, 2);
	Quat rot = universe->getRotation(entity);
	LuaWrapper::pushLua(L, rot * Vec3(0, 0, 1));
	return 1;
}


static int multVecQuat(lua_State* L)
{
	if (!LuaWrapper::checkParameterType<Vec3>(L, 1) ||
		!LuaWrapper::checkParameterType<Vec3>(L, 2) ||
		!LuaWrapper::checkParameterType<float>(L, 3))
	{
		lua_pushnil(L);
		return 1;
	}
	Vec3 v = LuaWrapper::toType<Vec3>(L, 1);
	Vec3 axis = LuaWrapper::toType<Vec3>(L, 2);
	Quat q(axis, (float)lua_tonumber(L, 7));

	Vec3 res = q * v;

	LuaWrapper::pushLua(L, res);
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


static void setEntityPosition(Universe* univ, int entity_index, Vec3 pos)
{
	univ->setPosition(entity_index, pos);
}


static void setEntityRotation(Universe* univ,
	int entity_index,
	Vec3 axis,
	float angle)
{
	if (entity_index < 0 || entity_index > univ->getEntityCount()) return;

	univ->setRotation(entity_index, Quat(axis, angle));
}


static void setEntityLocalRotation(IScene* hierarchy,
	Entity entity,
	Vec3 axis,
	float angle)
{
	if (entity == INVALID_ENTITY) return;

	static_cast<Hierarchy*>(hierarchy)->setLocalRotation(entity, Quat(axis, angle));
}


static void setRenderablePath(IScene* scene, int component, const char* path)
{
	RenderScene* render_scene = (RenderScene*)scene;
	render_scene->setRenderablePath(component, Path(path));
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
	REGISTER_FUNCTION(addDebugCross);

	scene.registerFunction("Engine", "multVecQuat", &LuaAPI::multVecQuat);
	scene.registerFunction("Engine", "getEntityPosition", &LuaAPI::getEntityPosition);
	scene.registerFunction("Engine", "getEntityDirection", &LuaAPI::getEntityDirection);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix