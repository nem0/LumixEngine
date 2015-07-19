#include "engine/lua_wrapper.h"
#include "core/crc32.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
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


		static void setEntityPosition(Universe* univ, int entity_index, float x, float y, float z)
		{
			Entity(univ, entity_index).setPosition(x, y, z);
		}


	} // namespace LuaAPI


	static void registerCFunction(lua_State* L, const char* name, lua_CFunction func)
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

		registerCFunction(L, "setEntityPosition", LuaWrapper::wrap<decltype(&LuaAPI::setEntityPosition), LuaAPI::setEntityPosition>);
		registerCFunction(L, "createComponent", LuaWrapper::wrap<decltype(&LuaAPI::createComponent), LuaAPI::createComponent>);
		registerCFunction(L, "getScene", LuaWrapper::wrap<decltype(&LuaAPI::getScene), LuaAPI::getScene>);
	}


}