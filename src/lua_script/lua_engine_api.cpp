#include "core/crc32.h"
#include "core/input_system.h"
#include "core/lua_wrapper.h"
#include "engine.h"
#include "iplugin.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"


namespace Lumix
{


namespace LuaAPI
{


static int setTextureData(lua_State* L)
{
	if (!LuaWrapper::isType<Texture*>(L, 1) || !lua_istable(L, 2))
	{
		return 0;
	}

	auto* texture = (Texture*)lua_touserdata(L, 1);
	int len = (int)lua_rawlen(L, 2);
	
	if (!texture || texture->getBytesPerPixel() != 2) return 0;

	uint16_t* data = (uint16_t*)texture->getData();
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, 2, 1 + i) == LUA_TNUMBER)
		{
			data[i] = (uint16_t)lua_tonumber(L, -1);
		}
		lua_pop(L, 1);
	}

	texture->onDataUpdated();

	return 0;
}


static void
setTexturePixel(Texture* texture, int x, int y, int r, int g, int b, int a)
{
	if (!texture) return;

	uint32_t color = (r & 0xff) << 24 | (g & 0xff) << 16 | (b & 0xff) << 8 | (a & 0xff);
	texture->setPixel(x, y, color);
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
	if (entity_index < 0 || entity_index > univ->getEntityCount()) return;

	univ->setRotation(entity_index, Quat(Vec3(x, y, z), angle));
}


static void setEntityLocalRotation(
	UniverseContext* univ, int entity_index, float x, float y, float z, float angle)
{
	if (entity_index < 0 || entity_index >= univ->m_universe->getEntityCount()) return;

	univ->m_hierarchy->setLocalRotation(entity_index, Quat(Vec3(x, y, z), angle));
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


void registerUniverse(UniverseContext* ctx, lua_State* L)
{
	lua_pushlightuserdata(L, ctx);
	lua_setglobal(L, "g_universe_context");

	lua_pushlightuserdata(L, ctx ? ctx->m_universe : nullptr);
	lua_setglobal(L, "g_universe");
}


void registerEngineLuaAPI(Engine& engine, lua_State* L)
{
	lua_pushlightuserdata(L, &engine);
	lua_setglobal(L, "g_engine");

	registerCFunction(L, "API_setTextureData", &LuaAPI::setTextureData);

	registerCFunction(L,
					  "API_setTexturePixel",
					  LuaWrapper::wrap<decltype(&LuaAPI::setTexturePixel),
									   LuaAPI::setTexturePixel>);

	registerCFunction(L,
					  "API_getMaterialTexture",
					  LuaWrapper::wrap<decltype(&LuaAPI::getMaterialTexture),
									   LuaAPI::getMaterialTexture>);

	registerCFunction(L,
					  "API_getTerrainMaterial",
					  LuaWrapper::wrap<decltype(&LuaAPI::getTerrainMaterial),
									   LuaAPI::getTerrainMaterial>);

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

	registerCFunction(
		L,
		"API_setEntityLocalRotation",
		LuaWrapper::wrap<decltype(&LuaAPI::setEntityLocalRotation),
						 LuaAPI::setEntityLocalRotation>);

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