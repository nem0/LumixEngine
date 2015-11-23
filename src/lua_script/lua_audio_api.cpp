#include "audio/audio_scene.h"
#include "core/crc32.h"
#include "core/lua_wrapper.h"
#include "engine.h"
#include "iplugin.h"
#include "universe/universe.h"


namespace Lumix
{


namespace LuaAPI
{


static int playSound(IScene* scene, int entity, AudioScene::ClipInfo* clip, bool is_3d)
{
	return static_cast<AudioScene*>(scene)->play(entity, clip, is_3d);
}


static void setSoundVolume(IScene* scene, int sound_id, float volume)
{
	static_cast<AudioScene*>(scene)->setVolume(sound_id, volume);
}


} // namespace LuaAPI


static void registerCFunction(lua_State* L, const char* name, lua_CFunction func)
{
	lua_pushcfunction(L, func);
	lua_setglobal(L, name);
}


void registerAudioLuaAPI(Engine&, Universe&, lua_State* L)
{
	registerCFunction(L,
		"API_playSound",
		LuaWrapper::wrap<decltype(&LuaAPI::playSound), LuaAPI::playSound>);
	registerCFunction(L,
		"API_setSoundVolume",
		LuaWrapper::wrap<decltype(&LuaAPI::setSoundVolume), LuaAPI::setSoundVolume>);
}


} // namespace Lumix