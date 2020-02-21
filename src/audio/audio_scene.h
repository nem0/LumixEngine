#pragma once


#include "engine/plugin.h"


struct lua_State;


namespace Lumix
{


struct AudioSystem;
struct Clip;
struct Path;


struct SoundAnimationEvent
{
	u32 clip;
	bool is_3d = true;
};


struct EchoZone
{
	EntityRef entity;
	float radius;
	float delay;
};


struct ChorusZone
{
	EntityRef entity;
	float radius;
	float delay;
	float wet_dry_mix;
	float depth;
	float feedback;
	float frequency;
	i32 phase;
};


struct AudioScene : IScene
{
	using SoundHandle = i32;
	static constexpr SoundHandle INVALID_SOUND_HANDLE = -1;

	struct ClipInfo
	{
		Clip* clip;
		char name[30];
		u32 name_hash;
		bool looped;
		float volume = 1;
	};

	static AudioScene* createInstance(AudioSystem& system,
		Universe& universe,
		struct IAllocator& allocator);
	static void destroyInstance(AudioScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual u32 getClipCount() const = 0;
	virtual const char* getClipName(u32 index) = 0;
	virtual ClipInfo* getClipInfoByIndex(u32 index) = 0;
	virtual ClipInfo* getClipInfo(u32 hash) = 0;
	virtual ClipInfo* getClipInfo(const char* name) = 0;
	virtual int getClipInfoIndex(ClipInfo* info) = 0;
	virtual void addClip(const char* name, const Path& path) = 0;
	virtual void removeClip(ClipInfo* clip) = 0;
	virtual void setClip(u32 clip_id, const Path& path) = 0;

	virtual EchoZone& getEchoZone(EntityRef entity) = 0;
	virtual ChorusZone& getChorusZone(EntityRef entity) = 0;

	virtual ClipInfo* getAmbientSoundClip(EntityRef entity) = 0;
	virtual int getAmbientSoundClipIndex(EntityRef entity) = 0;
	virtual void setAmbientSoundClipIndex(EntityRef entity, int index) = 0;
	virtual void setAmbientSoundClip(EntityRef entity, ClipInfo* clip) = 0;
	virtual bool isAmbientSound3D(EntityRef entity) = 0;
	virtual void setAmbientSound3D(EntityRef entity, bool is_3d) = 0;

	virtual SoundHandle play(EntityRef entity, ClipInfo* clip, bool is_3d) = 0;
	virtual void stop(SoundHandle sound_id) = 0;
	virtual void setVolume(SoundHandle sound_id, float volume) = 0;

	virtual void setEcho(SoundHandle sound_id,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) = 0;
};


} // namespace Lumix