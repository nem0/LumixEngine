#pragma once


#include "engine/allocator.h"
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


struct AudioModule : IModule
{
	using SoundHandle = i32;
	static constexpr SoundHandle INVALID_SOUND_HANDLE = -1;

	static UniquePtr<AudioModule> createInstance(AudioSystem& system,
		World& world,
		struct IAllocator& allocator);
	static void reflect(struct Engine& engine);

	virtual void setMasterVolume(float volume) = 0;

	virtual EchoZone& getEchoZone(EntityRef entity) = 0;
	virtual ChorusZone& getChorusZone(EntityRef entity) = 0;

	virtual Path getAmbientSoundClip(EntityRef entity) = 0;
	virtual void setAmbientSoundClip(EntityRef entity, const Path& clip) = 0;
	virtual bool isAmbientSound3D(EntityRef entity) = 0;
	virtual void setAmbientSound3D(EntityRef entity, bool is_3d) = 0;
	virtual void pauseAmbientSound(EntityRef entity) = 0;
	virtual void resumeAmbientSound(EntityRef entity) = 0;

	virtual SoundHandle play(EntityRef entity, Clip* clip, bool is_3d) = 0;
	virtual SoundHandle play(EntityRef entity, const Path& clip, bool is_3d) = 0;
	virtual bool isEnd(SoundHandle sound_id) = 0;
	virtual void stop(SoundHandle sound_id) = 0;
	virtual void setVolume(SoundHandle sound_id, float volume) = 0;
	virtual void setFrequency(SoundHandle sound_id, u32 frequency_hz) = 0;

	virtual void setEcho(SoundHandle sound_id,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) = 0;
};


} // namespace Lumix