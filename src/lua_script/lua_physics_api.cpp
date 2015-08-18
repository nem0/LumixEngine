#include "core/crc32.h"
#include "core/lua_wrapper.h"
#include "engine.h"
#include "iplugin.h"
#include "universe/universe.h"
#include "physics/physics_scene.h"


namespace Lumix
{


namespace LuaAPI
{


static void moveController(
	IScene* scene, int component, float x, float y, float z, float time_delta)
{
	static_cast<PhysicsScene*>(scene)
		->moveController(component, Vec3(x, y, z), time_delta);
}


} // namespace LuaAPI


static void
registerCFunction(lua_State* L, const char* name, lua_CFunction func)
{
	lua_pushcfunction(L, func);
	lua_setglobal(L, name);
}


void registerPhysicsLuaAPI(Engine& engine, Universe& universe, lua_State* L)
{
	registerCFunction(L,
					  "API_moveController",
					  LuaWrapper::wrap<decltype(&LuaAPI::moveController),
									   LuaAPI::moveController>);
}


} // namespace Lumix