#include "core/allocator.hpp"
#include "core/associative_array.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/stream.hpp"

#include "audio_module.hpp"
#include "animation/animation_module.hpp"
#include "audio_device.hpp"
#include "audio_system.hpp"
#include "clip.hpp"
#include "engine/engine.hpp"
#include "engine/reflection.hpp"
#include "engine/resource_manager.hpp"
#include "engine/world.hpp"
#include "imgui/IconsFontAwesome5.h"

namespace Lumix
{


static const ComponentType LISTENER_TYPE = reflection::getComponentType("audio_listener");
static const ComponentType AMBIENT_SOUND_TYPE = reflection::getComponentType("ambient_sound");
static const ComponentType ECHO_ZONE_TYPE = reflection::getComponentType("echo_zone");
static const ComponentType CHORUS_ZONE_TYPE = reflection::getComponentType("chorus_zone");

struct Listener
{
	EntityPtr entity;
};


struct AmbientSound
{
	EntityRef entity;
	Clip* clip = nullptr;
	bool is_3d;
	int playing_sound;
};


struct PlayingSound
{
	AudioDevice::BufferHandle buffer_id;
	EntityPtr entity;
	Clip* clip = nullptr;
	bool is_3d;
};


struct AudioModuleImpl final : AudioModule
{
	enum class Version : i32 {
		INIT,
		CLIPS_REWORKED,
		LATEST
	};

	AudioModuleImpl(AudioSystem& system, World& world, IAllocator& allocator)
		: m_allocator(allocator)
		, m_world(world)
		, m_system(system)
		, m_device(system.getDevice())
		, m_ambient_sounds(allocator)
		, m_echo_zones(allocator)
		, m_chorus_zones(allocator)
	{
		m_listener.entity = INVALID_ENTITY;
		for (auto& i : m_playing_sounds)
		{
			i.entity = INVALID_ENTITY;
			i.buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
		}
	}

	i32 getVersion() const override { return (i32)Version::LATEST; }
	const char* getName() const override { return "audio"; }

	~AudioModuleImpl() {
		for (const AmbientSound& snd : m_ambient_sounds) {
			if (snd.clip) snd.clip->decRefCount();
		}
	}

	void updateAnimationEvents()
	{
		/*if (!m_animation_module) return;
		
		InputMemoryStream blob(m_animation_module->getEventStream());
		const RuntimeHash sound_type("sound");
		while (blob.getPosition() < blob.size())
		{
			RuntimeHash type;
			u8 size;
			EntityRef entity;
			blob.read(type);
			blob.read(entity);
			blob.read(size);
			if (type == sound_type)
			{
				SoundAnimationEvent event;
				blob.read(event);
				ClipInfo* clip = getClipInfo(event.clip);
				if (clip)
				{
					play(entity, clip, event.is_3d);
				}
			}
			else
			{
				blob.skip(size);
			}
		}*/
	}

	void update(float time_delta) override
	{
		if (m_listener.entity.isValid())
		{
			const EntityRef listener = (EntityRef) m_listener.entity;
			const DVec3 pos = m_world.getPosition(listener);
			m_device.setListenerPosition(pos);
			const Matrix orientation = m_world.getRotation(listener).toMatrix();
			const Vec3 front = orientation.getZVector();
			const Vec3 up = orientation.getYVector();
			m_device.setListenerOrientation(front.x, front.y, front.z, up.x, up.y, up.z);
		}

		for (PlayingSound & sound : m_playing_sounds)
		{
			if (sound.buffer_id == AudioDevice::INVALID_BUFFER_HANDLE) continue;
			if (sound.is_3d && sound.entity.isValid())
			{
				const DVec3 pos = m_world.getPosition((EntityRef)sound.entity);
				m_device.setSourcePosition(sound.buffer_id, pos);
			}

			Clip* clip_info = sound.clip;
			if (!clip_info->m_looped && m_device.isEnd(sound.buffer_id))
			{
				m_device.stop(sound.buffer_id);
				sound.buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
			}
		}
		m_device.update(time_delta);

		updateAnimationEvents();
	}


	bool isAmbientSound3D(EntityRef entity) override
	{
		return m_ambient_sounds[entity].is_3d;
	}


	void setAmbientSound3D(EntityRef entity, bool is_3d) override
	{
		m_ambient_sounds[entity].is_3d = is_3d;
	}

	void pauseAmbientSound(EntityRef entity) override {
		const i32 idx = m_ambient_sounds[entity].playing_sound;
		if (idx < 0) return;
		m_device.pause(m_playing_sounds[idx].buffer_id);
	}

	void resumeAmbientSound(EntityRef entity) override {
		const AmbientSound& as = m_ambient_sounds[entity];
		const i32 idx = as.playing_sound;
		if (idx < 0) return;
		m_device.play(m_playing_sounds[idx].buffer_id, as.clip->m_looped);
	}

	void startGame() override
	{
		m_animation_module = (AnimationModule*)m_world.getModule("animation");
		for (AmbientSound& sound : m_ambient_sounds)
		{
			if (sound.clip) sound.playing_sound = play(sound.entity, sound.clip, sound.is_3d);
		}
	}


	void stopGame() override
	{
		m_animation_module = nullptr;
		for (auto& i : m_playing_sounds)
		{
			if (i.buffer_id != AudioDevice::INVALID_BUFFER_HANDLE)
			{
				m_device.stop(i.buffer_id);
				i.buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
			}
		}

		for (AmbientSound& sound : m_ambient_sounds)
		{
			sound.playing_sound = -1;
		}
	}


	void createListener(EntityRef entity)
	{
		m_listener.entity = entity;
		m_world.onComponentCreated(entity, LISTENER_TYPE, this);
	}


	Path getAmbientSoundClip(EntityRef entity) override
	{
		AmbientSound& snd = m_ambient_sounds[entity];
		return snd.clip ? snd.clip->getPath() : Path();
	}


	void setAmbientSoundClip(EntityRef entity, const Path& clip) override
	{
		Clip* res = m_system.getEngine().getResourceManager().load<Clip>(clip);
		if (m_ambient_sounds[entity].clip) {
			m_ambient_sounds[entity].clip->decRefCount();
		}
		m_ambient_sounds[entity].clip = res;
	}


	void createEchoZone(EntityRef entity)
	{
		EchoZone& zone = m_echo_zones.insert(entity);
		zone.entity = entity;
		zone.delay = 500.0f;
		zone.radius = 10;
		m_world.onComponentCreated(entity, ECHO_ZONE_TYPE, this);
	}


	void destroyEchoZone(EntityRef entity)
	{
		int idx = m_echo_zones.find(entity);
		m_echo_zones.eraseAt(idx);
		m_world.onComponentDestroyed(entity, ECHO_ZONE_TYPE, this);
	}

	
	EchoZone& getEchoZone(EntityRef entity) override
	{
		return m_echo_zones[entity];
	}


	void createChorusZone(EntityRef entity)
	{
		ChorusZone& zone = m_chorus_zones.insert(entity);
		zone.entity = entity;
		zone.delay = 500.0f;
		zone.radius = 10;
		zone.depth = 1;
		zone.feedback = 0;
		zone.frequency = 1;
		zone.phase = 0;
		zone.wet_dry_mix = 0.5f;
		m_world.onComponentCreated(entity, CHORUS_ZONE_TYPE, this);
	}


	ChorusZone& getChorusZone(EntityRef entity) override
	{
		return m_chorus_zones[entity];
	}


	void destroyChorusZone(EntityRef entity)
	{
		int idx = m_chorus_zones.find(entity);
		m_chorus_zones.eraseAt(idx);
		m_world.onComponentDestroyed(entity, CHORUS_ZONE_TYPE, this);
	}


	void createAmbientSound(EntityRef entity)
	{
		AmbientSound& sound = m_ambient_sounds.insert(entity);
		sound.entity = entity;
		sound.clip = nullptr;
		sound.playing_sound = -1;
		m_world.onComponentCreated(entity, AMBIENT_SOUND_TYPE, this);
	}


	void destroyListener(EntityRef entity)
	{
		m_listener.entity = INVALID_ENTITY;
		m_world.onComponentDestroyed(entity, LISTENER_TYPE, this);
	}


	void destroyAmbientSound(EntityRef entity)
	{
		m_ambient_sounds.erase(entity);
		m_world.onComponentDestroyed(entity, AMBIENT_SOUND_TYPE, this);
	}


	void serialize(OutputMemoryStream& serializer) override
	{
		serializer.write(m_listener.entity);

		serializer.write(m_ambient_sounds.size());
		for (const AmbientSound& sound : m_ambient_sounds)
		{
			serializer.writeString(sound.clip ? sound.clip->getPath().c_str() : "");
			serializer.write(sound.entity);
			serializer.write(sound.is_3d);
		}

		serializer.write(m_echo_zones.size());
		for (EchoZone& zone : m_echo_zones)
		{
			serializer.write(zone);
		}

		serializer.write(m_chorus_zones.size());
		for (ChorusZone& zone : m_chorus_zones)
		{
			serializer.write(zone);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		serializer.read(m_listener.entity);
		m_listener.entity = entity_map.get(m_listener.entity);
		if (m_listener.entity.isValid()) {
			m_world.onComponentCreated((EntityRef)m_listener.entity, LISTENER_TYPE, this);
		}

		if (version < (i32)Version::CLIPS_REWORKED) {
			int dummy;
			serializer.read(dummy);
			ASSERT(dummy == 0);
		}

		i32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i) {
			AmbientSound sound;
			ASSERT(version >= (i32)Version::CLIPS_REWORKED);
			const char* path = serializer.readString();
			Clip* res = path[0] ? m_system.getEngine().getResourceManager().load<Clip>(Path(path)) : nullptr;
			sound.clip = res;
			serializer.read(sound.entity);
			sound.entity = entity_map.get(sound.entity);
			serializer.read(sound.is_3d);

			m_ambient_sounds.insert(sound.entity, sound);
			m_world.onComponentCreated(sound.entity, AMBIENT_SOUND_TYPE, this);
		}

		serializer.read(count);

		for (int i = 0; i < count; ++i) {
			EchoZone zone;
			serializer.read(zone);
			zone.entity = entity_map.get(zone.entity);
			m_echo_zones.insert(zone.entity, zone);
			m_world.onComponentCreated(zone.entity, ECHO_ZONE_TYPE, this);
		}

		serializer.read(count);

		for (int i = 0; i < count; ++i) {
			ChorusZone zone;
			serializer.read(zone);
			zone.entity = entity_map.get(zone.entity);

			m_chorus_zones.insert(zone.entity, zone);
			m_world.onComponentCreated(zone.entity, CHORUS_ZONE_TYPE, this);
		}
	}

	SoundHandle play(EntityRef entity, const Path& clip, bool is_3d) override {
		Clip* res = m_system.getEngine().getResourceManager().load<Clip>(clip);
		return play(entity, res, is_3d);
	}
	
	SoundHandle play(EntityRef entity, Clip* clip, bool is_3d) override {
		for (PlayingSound& sound : m_playing_sounds) {
			if (sound.buffer_id == AudioDevice::INVALID_BUFFER_HANDLE) {
				if (!clip->isReady()) return INVALID_SOUND_HANDLE;

				int flags = is_3d ? (int)AudioDevice::BufferFlags::IS3D : 0;
				if (is_3d && clip->getChannels() > 1) {
					logWarning(clip->getPath(), ": can not play sound with 2 channels as 3d");
					flags = 0;
				}
				auto buffer = m_device.createBuffer(clip->getData(), clip->getSize(), clip->getChannels(), clip->getSampleRate(), flags);
				if (buffer == AudioDevice::INVALID_BUFFER_HANDLE) return INVALID_SOUND_HANDLE;

				m_device.play(buffer, clip->m_looped);
				m_device.setVolume(buffer, clip->m_volume);

				const DVec3 pos = m_world.getPosition(entity);
				m_device.setSourcePosition(buffer, pos);

				sound.is_3d = is_3d;
				sound.buffer_id = buffer;
				sound.entity = entity;
				clip->incRefCount();
				sound.clip = clip;

				for (const EchoZone& zone : m_echo_zones) {
					const double dist2 = squaredLength(pos - m_world.getPosition(zone.entity));
					const double r2 = zone.radius * zone.radius;
					if (dist2 > r2) continue;

					const float w = float(dist2 / r2);
					m_device.setEcho(buffer, 1, 1 - w, zone.delay, zone.delay);
					break;
				}

				for (const ChorusZone& zone : m_chorus_zones) {
					const double dist2 = squaredLength(pos - m_world.getPosition(zone.entity));
					double r2 = zone.radius * zone.radius;
					if (dist2 > r2) continue;

					m_device.setChorus(buffer, 1, 1, 0, 1, zone.delay, 0);
					break;
				}

				return SoundHandle(&sound - m_playing_sounds);
			}
		}

		return INVALID_SOUND_HANDLE;
	}

	bool isEnd(SoundHandle sound_id) override {
		ASSERT(sound_id >= 0 && sound_id < (int)lengthOf(m_playing_sounds));
		return m_device.isEnd(m_playing_sounds[sound_id].buffer_id);
	}

	void stop(SoundHandle sound_id) override
	{
		ASSERT(sound_id >= 0 && sound_id < (int)lengthOf(m_playing_sounds));
		m_device.stop(m_playing_sounds[sound_id].buffer_id);
		m_playing_sounds[sound_id].buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
		if (m_playing_sounds[sound_id].clip) {
			m_playing_sounds[sound_id].clip->decRefCount();
			m_playing_sounds[sound_id].clip = nullptr;
		}
	}


	void setMasterVolume(float volume) override { m_device.setMasterVolume(volume); }


	void setVolume(SoundHandle sound_id, float volume) override
	{
		ASSERT(sound_id != AudioModule::INVALID_SOUND_HANDLE);
		ASSERT(sound_id >= 0 && sound_id < (int)lengthOf(m_playing_sounds));
		m_device.setVolume(m_playing_sounds[sound_id].buffer_id, volume);
	}

	void setFrequency(SoundHandle sound_id, u32 frequency) override {
		ASSERT(sound_id != AudioModule::INVALID_SOUND_HANDLE);
		ASSERT(sound_id >= 0 && sound_id < (int)lengthOf(m_playing_sounds));
		m_device.setFrequency(m_playing_sounds[sound_id].buffer_id, frequency);
	}

	void setEcho(SoundHandle sound_id, float wet_dry_mix, float feedback, float left_delay, float right_delay) override
	{
		ASSERT(sound_id >= 0 && sound_id < (int)lengthOf(m_playing_sounds));
		m_device.setEcho(m_playing_sounds[sound_id].buffer_id, wet_dry_mix, feedback, left_delay, right_delay);
	}

	World& getWorld() override { return m_world; }
	ISystem& getSystem() const override { return m_system; }

	AssociativeArray<EntityRef, AmbientSound> m_ambient_sounds;
	AssociativeArray<EntityRef, EchoZone> m_echo_zones;
	AssociativeArray<EntityRef, ChorusZone> m_chorus_zones;
	AudioDevice& m_device;
	Listener m_listener;
	IAllocator& m_allocator;
	World& m_world;
	AudioSystem& m_system;
	PlayingSound m_playing_sounds[AudioDevice::MAX_PLAYING_SOUNDS];
	AnimationModule* m_animation_module = nullptr;
};


UniquePtr<AudioModule> AudioModule::createInstance(AudioSystem& system,
	World& world,
	IAllocator& allocator)
{
	return UniquePtr<AudioModuleImpl>::create(allocator, system, world, allocator);
}

void AudioModule::reflect(Engine& engine) {
	LUMIX_MODULE(AudioModuleImpl, "audio")
		.LUMIX_FUNC(setMasterVolume)
		.function<(SoundHandle (AudioModule::*)(EntityRef, const Path&, bool))&AudioModule::play>("play", "AudioModule::play")
		.LUMIX_FUNC(stop)
		.LUMIX_FUNC(isEnd)
		.LUMIX_FUNC(setFrequency)
		.LUMIX_FUNC(setVolume)
		.LUMIX_FUNC(setEcho)
		.LUMIX_CMP(AmbientSound, "ambient_sound", "Audio / Ambient sound")
			.LUMIX_FUNC_EX(AudioModule::pauseAmbientSound, "pause")
			.LUMIX_FUNC_EX(AudioModule::resumeAmbientSound, "resume")
			.prop<&AudioModule::isAmbientSound3D, &AudioModule::setAmbientSound3D>("3D")
			.LUMIX_PROP(AmbientSoundClip, "Sound").resourceAttribute(Clip::TYPE)
		.LUMIX_CMP(Listener, "audio_listener", "Audio / Listener").icon(ICON_FA_HEADPHONES)
		.LUMIX_CMP(EchoZone, "echo_zone", "Audio / Echo zone")
			.var_prop<&AudioModule::getEchoZone, &EchoZone::radius>("Radius").minAttribute(0)
			.var_prop<&AudioModule::getEchoZone, &EchoZone::delay>("Delay (ms)").minAttribute(0)
		.LUMIX_CMP(ChorusZone, "chorus_zone", "Audio / Chorus zone")
			.var_prop<&AudioModule::getChorusZone, &ChorusZone::radius>("Radius").minAttribute(0)
			.var_prop<&AudioModule::getChorusZone, &ChorusZone::delay>("Delay (ms)").minAttribute(0)
	;
}



} // namespace Lumix