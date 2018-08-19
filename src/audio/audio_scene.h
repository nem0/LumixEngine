#pragma once


#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{


class AudioSystem;
class Clip;
class Path;


struct SoundAnimationEvent
{
	u32 clip;
	bool is_3d = true;
};


class AudioScene : public IScene
{
public:
	typedef int SoundHandle;
	static const SoundHandle INVALID_SOUND_HANDLE = -1;

	struct ClipInfo
	{
		Clip* clip;
		char name[30];
		u32 name_hash;
		bool looped;
		float volume = 1;
	};

public:
	static AudioScene* createInstance(AudioSystem& system,
		Universe& universe,
		struct IAllocator& allocator);
	static void destroyInstance(AudioScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual int getClipCount() const = 0;
	virtual const char* getClipName(int index) = 0;
	virtual ClipInfo* getClipInfo(int index) = 0;
	virtual ClipInfo* getClipInfo(u32 hash) = 0;
	virtual ClipInfo* getClipInfo(const char* name) = 0;
	virtual int getClipInfoIndex(ClipInfo* info) = 0;
	virtual void addClip(const char* name, const Path& path) = 0;
	virtual void removeClip(ClipInfo* clip) = 0;
	virtual void setClip(int clip_id, const Path& path) = 0;

	virtual float getEchoZoneRadius(EntityRef entity) = 0;
	virtual void setEchoZoneRadius(EntityRef entity, float radius) = 0;
	virtual float getEchoZoneDelay(EntityRef entity) = 0;
	virtual void setEchoZoneDelay(EntityRef entity, float delay) = 0;

	virtual float getChorusZoneDelay(EntityRef entity) = 0;
	virtual void setChorusZoneDelay(EntityRef entity, float delay) = 0;
	virtual float getChorusZoneRadius(EntityRef entity) = 0;
	virtual void setChorusZoneRadius(EntityRef entity, float radius) = 0;

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