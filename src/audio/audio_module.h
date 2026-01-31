#pragma once


#include "core/allocator.h"
#include "engine/plugin.h"


//@ module AudioModule audio "Audio"
namespace black {


struct AudioSystem;
struct Clip;
struct Path;


struct SoundAnimationEvent
{
	u32 clip;
	bool is_3d = true;
};


//@ component_struct
struct EchoZone {
	EntityRef entity;
	float radius;		//@ property min 0
	float delay;		//@ property min 0 label "Delay (ms)"
};
//@ end


//@ component_struct
struct ChorusZone {
	EntityRef entity;
	float radius;			//@ property min 0
	float delay;			//@ property min 0 label "Delay (ms)"
	float wet_dry_mix;
	float depth;
	float feedback;
	float frequency;
	i32 phase;
};
//@ end

using SoundHandle = i32;

struct AudioModule : IModule {
	static constexpr SoundHandle INVALID_SOUND_HANDLE = -1;

	static UniquePtr<AudioModule> createInstance(AudioSystem& system,
		World& world,
		struct IAllocator& allocator);
	static void reflect(struct Engine& engine);
	virtual SoundHandle play(EntityRef entity, Clip* clip, bool is_3d) = 0;

	//@ functions
	virtual SoundHandle play(EntityRef entity, const Path& clip, bool is_3d) = 0;
	virtual void setMasterVolume(float volume) = 0;
	virtual void stop(SoundHandle sound_id) = 0;
	virtual bool isEnd(SoundHandle sound_id) = 0;
	virtual void setFrequency(SoundHandle sound_id, u32 frequency_hz) = 0;
	virtual void setVolume(SoundHandle sound_id, float volume) = 0;
	virtual void setEcho(SoundHandle sound_id, float wet_dry_mix, float feedback, float left_delay, float right_delay) = 0;
	//@ end

	virtual EchoZone& getEchoZone(EntityRef entity) = 0;
	virtual ChorusZone& getChorusZone(EntityRef entity) = 0;
	virtual void createEchoZone(EntityRef entity) = 0;
	virtual void destroyEchoZone(EntityRef entity) = 0;
	virtual void createChorusZone(EntityRef entity) = 0;
	virtual void destroyChorusZone(EntityRef entity) = 0;
	virtual void createListener(EntityRef entity) = 0;
	virtual void destroyListener(EntityRef entity) = 0;
	virtual void createAmbientSound(EntityRef entity) = 0;
	virtual void destroyAmbientSound(EntityRef entity) = 0;

	//@ component Listener id audio_listener
	//@ end

	//@ component AmbientSound
	virtual Path getAmbientSoundClip(EntityRef entity) = 0;					//@ resource_type Clip::TYPE
	virtual void setAmbientSoundClip(EntityRef entity, const Path& clip) = 0;
	virtual bool isAmbientSound3D(EntityRef entity) = 0;					//@ getter Is_3D label "Is 3D"
	virtual void setAmbientSound3D(EntityRef entity, bool is_3d) = 0;		//@ setter Is_3D
	virtual void pauseAmbientSound(EntityRef entity) = 0;					//@ alias pause
	virtual void resumeAmbientSound(EntityRef entity) = 0;					//@ alias resume
	//@ end
};


} // namespace black