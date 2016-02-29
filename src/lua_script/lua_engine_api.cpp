#include "core/crc32.h"
#include "core/input_system.h"
#include "core/lua_wrapper.h"
#include "engine.h"
#include "core/blob.h"
#include "engine/engine.h"
#include "engine/iproperty_descriptor.h"
#include "engine/plugin_manager.h"
#include "engine/property_register.h"
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
	auto* scene = LuaWrapper::checkArg<LuaScriptScene*>(L, 1);
	Entity entity = LuaWrapper::checkArg<Entity>(L, 2);
	int scr_index = LuaWrapper::checkArg<int>(L, 3);

	ComponentIndex cmp = scene->getComponent(entity);
	int env = scene->getEnvironment(cmp, scr_index);
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


static void setProperty(const ComponentUID& cmp,
	const IPropertyDescriptor& desc,
	lua_State* L,
	IAllocator& allocator)
{
	switch (desc.getType())
	{
		case IPropertyDescriptor::STRING:
		case IPropertyDescriptor::FILE:
		case IPropertyDescriptor::RESOURCE:
			if (lua_isstring(L, -1))
			{
				const char* str = lua_tostring(L, -1);
				desc.set(cmp, -1, InputBlob(str, stringLength(str)));
			}
			break;
		case IPropertyDescriptor::DECIMAL:
			if (lua_isnumber(L, -1))
			{
				float f = (float)lua_tonumber(L, -1);
				desc.set(cmp, -1, InputBlob(&f, sizeof(f)));
			}
			break;
		case IPropertyDescriptor::BOOL:
			if (lua_isboolean(L, -1))
			{
				bool b = lua_toboolean(L, -1) != 0;
				desc.set(cmp, -1, InputBlob(&b, sizeof(b)));
			}
			break;
		case IPropertyDescriptor::VEC3:
		case IPropertyDescriptor::COLOR:
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec3>(L, -1);
				desc.set(cmp, -1, InputBlob(&v, sizeof(v)));
			}
			break;
		default:
			g_log_error.log("Lua Script") << "Property " << desc.getName() << " has unsupported type";
			break;
	}
	;
}


static int createEntityEx(lua_State* L)
{
	auto* engine = LuaWrapper::checkArg<Engine*>(L, 1);
	auto* ctx = LuaWrapper::checkArg<Universe*>(L, 2);
	LuaWrapper::checkTableArg(L, 3);

	Entity e = ctx->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));

	lua_pushvalue(L, 3);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0)
	{
		const char* parameter_name = luaL_checkstring(L, -2);
		if (compareString(parameter_name, "position") == 0)
		{
			auto pos = LuaWrapper::toType<Vec3>(L, -1);
			ctx->setPosition(e, pos);
		}
		else
		{
			uint32 cmp_hash = crc32(parameter_name);
			for (auto* scene : ctx->getScenes())
			{
				ComponentUID cmp(e, cmp_hash, scene, scene->createComponent(cmp_hash, e));
				if (cmp.isValid())
				{
					lua_pushvalue(L, -1);
					lua_pushnil(L);
					while (lua_next(L, -2) != 0)
					{
						const char* property_name = luaL_checkstring(L, -2);
						auto* desc = PropertyRegister::getDescriptor(cmp_hash, crc32(property_name));
						if (!desc)
						{
							g_log_error.log("Lua Script") << "Unknown property " << property_name;
						}
						else
						{
							setProperty(cmp, *desc, L, engine->getAllocator());
						}

						lua_pop(L, 1);
					}
					lua_pop(L, 1);
					break;
				}
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	LuaWrapper::pushLua(L, e);
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
		g_log_error.log("Lua Script") << "Component " << type << " already exists in entity "
								  << entity_idx;
		return -1;
	}

	return scene->createComponent(hash, e);
}


static int getEntityPosition(lua_State* L)
{
	auto* universe = LuaWrapper::checkArg<Universe*>(L, 1);
	Entity entity = LuaWrapper::checkArg<Entity>(L, 2);

	Vec3 pos = universe->getPosition(entity);
	LuaWrapper::pushLua(L, pos);
	return 1;
}

static int getEntityDirection(lua_State* L)
{
	auto* universe = LuaWrapper::checkArg<Universe*>(L, 1);
	Entity entity = LuaWrapper::checkArg<Entity>(L, 2);

	Quat rot = universe->getRotation(entity);
	LuaWrapper::pushLua(L, rot * Vec3(0, 0, 1));
	return 1;
}


static int multVecQuat(lua_State* L)
{
	Vec3 v = LuaWrapper::checkArg<Vec3>(L, 1);
	Vec3 axis = LuaWrapper::checkArg<Vec3>(L, 2);
	float angle = LuaWrapper::checkArg<float>(L, 3);

	Quat q(axis, angle);
	Vec3 res = q * v;

	LuaWrapper::pushLua(L, res);
	return 1;
}


static void logError(const char* text)
{
	g_log_error.log("Lua Script") << text;
}


static void logInfo(const char* text)
{
	g_log_info.log("Lua Script") << text;
}


static Entity createEntity(Universe* univ)
{
	return univ->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
}


static void setEntityPosition(Universe* univ, Entity entity, Vec3 pos)
{
	univ->setPosition(entity, pos);
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


static ComponentIndex getRenderableComponent(IScene* scene, Entity entity)
{
	RenderScene* render_scene = (RenderScene*)scene;
	return render_scene->getRenderableComponent(entity);
}


static void setRenderableMaterial(IScene* scene, int component, int mesh_idx, const char* path)
{
	RenderScene* render_scene = (RenderScene*)scene;
	render_scene->setRenderableMaterial(component, mesh_idx, Path(path));
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


void registerUniverse(Universe* ctx, lua_State* L)
{
	for (auto* scene : ctx->getScenes())
	{
		const char* name = scene->getPlugin().getName();
		char tmp[128];

		copyString(tmp, "g_scene_");
		catString(tmp, name);
		lua_pushlightuserdata(L, scene);
		lua_setglobal(L, tmp);
	}

	lua_pushlightuserdata(L, ctx);
	lua_setglobal(L, "g_universe");
}


void registerEngineLuaAPI(LuaScriptScene& scene, Engine& engine, lua_State* L)
{
	lua_pushlightuserdata(L, &engine);
	lua_setglobal(L, "g_engine");

	#define REGISTER_FUNCTION(name) \
		LuaWrapper::createSystemFunction(L, "Engine", #name, &LuaWrapper::wrap<decltype(&LuaAPI::name), LuaAPI::name>); \

	REGISTER_FUNCTION(createComponent);
	REGISTER_FUNCTION(getEnvironment);
	REGISTER_FUNCTION(getMaterialTexture);
	REGISTER_FUNCTION(getTerrainMaterial);
	REGISTER_FUNCTION(createEntity);
	REGISTER_FUNCTION(setEntityPosition);
	REGISTER_FUNCTION(setEntityRotation);
	REGISTER_FUNCTION(setEntityLocalRotation);
	REGISTER_FUNCTION(setRenderableMaterial);
	REGISTER_FUNCTION(getRenderableComponent);
	REGISTER_FUNCTION(setRenderablePath);
	REGISTER_FUNCTION(getInputActionValue);
	REGISTER_FUNCTION(addInputAction);
	REGISTER_FUNCTION(logError);
	REGISTER_FUNCTION(logInfo);
	REGISTER_FUNCTION(addDebugCross);

	LuaWrapper::createSystemFunction(L, "Engine", "createEntityEx", &LuaAPI::createEntityEx);
	LuaWrapper::createSystemFunction(L, "Engine", "multVecQuat", &LuaAPI::multVecQuat);
	LuaWrapper::createSystemFunction(L, "Engine", "getEntityPosition", &LuaAPI::getEntityPosition);
	LuaWrapper::createSystemFunction(L, "Engine", "getEntityDirection", &LuaAPI::getEntityDirection);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix