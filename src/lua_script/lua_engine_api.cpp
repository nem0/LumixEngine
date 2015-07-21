#include "engine/lua_wrapper.h"
#include "core/crc32.h"
#include "core/input_system.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "graphics/render_scene.h"
#include "universe/universe.h"


namespace Lumix
{


namespace LuaAPI
{


static void* getScene(Engine* engine, const char* name)
{
	if (engine)
	{
		return engine->getScene(crc32(name));
	}
	return nullptr;
}


static int createComponent(IScene* scene, const char* type, int entity_idx)
{
	if (!scene)
	{
		return -1;
	}
	Entity e(&scene->getUniverse(), entity_idx);
	return scene->createComponent(crc32(type), e).index;
}


static void
setEntityPosition(Universe* univ, int entity_index, float x, float y, float z)
{
	Entity(univ, entity_index).setPosition(x, y, z);
}


static void setRenderablePath(IScene* scene, int component, const char* path)
{
	RenderScene* render_scene = (RenderScene*)scene;
	render_scene->setRenderablePath(component,
									string(path, render_scene->getAllocator()));
}


static float getInputActionValue(Engine* engine, uint32_t action)
{
	return engine->getInputSystem().getActionValue(action);
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


void registerEngineLuaAPI(Engine& engine, Universe& universe, lua_State* L)
{
	lua_pushlightuserdata(L, &universe);
	lua_setglobal(L, "g_universe");

	lua_pushlightuserdata(L, &engine);
	lua_setglobal(L, "g_engine");

	registerCFunction(
		L,
		"getScene",
		LuaWrapper::wrap<decltype(&LuaAPI::getScene), LuaAPI::getScene>);

	registerCFunction(L,
					  "setEntityPosition",
					  LuaWrapper::wrap<decltype(&LuaAPI::setEntityPosition),
									   LuaAPI::setEntityPosition>);
	registerCFunction(L,
					  "createComponent",
					  LuaWrapper::wrap<decltype(&LuaAPI::createComponent),
									   LuaAPI::createComponent>);

	registerCFunction(L,
					  "setRenderablePath",
					  LuaWrapper::wrap<decltype(&LuaAPI::setRenderablePath),
									   LuaAPI::setRenderablePath>);

	registerCFunction(L,
					  "getInputActionValue",
					  LuaWrapper::wrap<decltype(&LuaAPI::getInputActionValue),
									   LuaAPI::getInputActionValue>);
	registerCFunction(L,
					  "addInputAction",
					  LuaWrapper::wrap<decltype(&LuaAPI::addInputAction),
									   LuaAPI::addInputAction>);
}


} // namespace Lumix