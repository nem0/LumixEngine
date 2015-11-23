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


static int playSound(IScene* scene, int entity, const char* clip_name, bool is_3d)
{
	auto* audio_scene = static_cast<AudioScene*>(scene);
	auto* clip = audio_scene->getClipInfo(clip_name);
	if (clip) return audio_scene->play(entity, clip, is_3d);

	return -1;
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