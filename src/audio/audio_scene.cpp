#include "audio_scene.h"
#include "animation/animation_scene.h"
#include "audio_device.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/iallocator.h"
#include "engine/lua_wrapper.h"
#include "engine/matrix.h"
#include "engine/reflection.h"
#include "engine/resource_manager_base.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "lua_script/lua_script_system.h"

namespace Lumix
{


static const ComponentType LISTENER_TYPE = Reflection::getComponentType("audio_listener");
static const ComponentType AMBIENT_SOUND_TYPE = Reflection::getComponentType("ambient_sound");
static const ComponentType ECHO_ZONE_TYPE = Reflection::getComponentType("echo_zone");
static const ComponentType CHORUS_ZONE_TYPE = Reflection::getComponentType("chorus_zone");


enum class AudioSceneVersion : int
{
	CHORUS = 0,

	LAST
};


struct Listener
{
	EntityPtr entity;
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

struct AmbientSound
{
	EntityRef entity;
	AudioScene::ClipInfo* clip;
	bool is_3d;
	int playing_sound;
};


struct PlayingSound
{
	AudioDevice::BufferHandle buffer_id;
	EntityPtr entity;
	AudioScene::ClipInfo* clip;
	bool is_3d;
};


struct AudioSceneImpl final : public AudioScene
{
	AudioSceneImpl(AudioSystem& system, Universe& context, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(context)
		, m_clips(allocator)
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
		context.registerComponentType(LISTENER_TYPE
			, this
			, &AudioSceneImpl::createListener
			, &AudioSceneImpl::destroyListener
			, &AudioSceneImpl::serializeListener
			, &AudioSceneImpl::deserializeListener);
		context.registerComponentType(AMBIENT_SOUND_TYPE
			, this
			, &AudioSceneImpl::createAmbientSound
			, &AudioSceneImpl::destroyAmbientSound
			, &AudioSceneImpl::serializeAmbientSound
			, &AudioSceneImpl::deserializeAmbientSound);
		context.registerComponentType(ECHO_ZONE_TYPE
			, this
			, &AudioSceneImpl::createEchoZone
			, &AudioSceneImpl::destroyEchoZone
			, &AudioSceneImpl::serializeEchoZone
			, &AudioSceneImpl::deserializeEchoZone);
		context.registerComponentType(CHORUS_ZONE_TYPE
			, this
			, &AudioSceneImpl::createChorusZone
			, &AudioSceneImpl::destroyChorusZone
			, &AudioSceneImpl::serializeChorusZone
			, &AudioSceneImpl::deserializeChorusZone);
	}


	void serialize(ISerializer& serializer) override
	{
		serializer.write("count", m_clips.size());
		for (ClipInfo* clip : m_clips)
		{
			serializer.write("valid", clip != nullptr);
			if (!clip) continue;

			serializer.write("volume", clip->volume);
			serializer.write("looped", clip->looped);
			serializer.write("name", clip->name);
			serializer.write("path", clip->clip->getPath().c_str());
		}
	}


	void deserialize(IDeserializer& serializer) override
	{
		int count;
		serializer.read(&count);
		m_clips.resize(count);
		for (int i = 0; i < count; ++i)
		{
			bool valid;
			serializer.read(&valid);
			if (!valid)
			{
				m_clips[i] = nullptr;
				continue;
			}

			auto* clip = LUMIX_NEW(m_allocator, ClipInfo);
			m_clips[i] = clip;
			serializer.read(&clip->volume);
			serializer.read(&clip->looped);
			serializer.read(clip->name, lengthOf(clip->name));
			clip->name_hash = crc32(clip->name);
			char path[MAX_PATH_LENGTH];
			serializer.read(path, lengthOf(path));
			clip->clip = path[0] ? static_cast<Clip*>(m_system.getClipManager().load(Path(path))) : nullptr;
		}
	}


	void serializeEchoZone(ISerializer& serializer, EntityRef entity)
	{
		EchoZone& zone = m_echo_zones[entity];
		serializer.write("radius", zone.radius);
		serializer.write("delay", zone.delay);
	}


	void deserializeEchoZone(IDeserializer& serializer, EntityRef entity, int /*scene_version*/)
	{
		EchoZone& zone = m_echo_zones.insert(entity);
		zone.entity = entity;
		serializer.read(&zone.radius);
		serializer.read(&zone.delay);
		m_universe.onComponentCreated(entity, ECHO_ZONE_TYPE, this);
	}

	void serializeChorusZone(ISerializer& serializer, EntityRef entity)
	{
		ChorusZone& zone = m_chorus_zones[entity];
		serializer.write("radius", zone.radius);
		serializer.write("delay", zone.delay);
		serializer.write("depth", zone.depth);
		serializer.write("feedback", zone.feedback);
		serializer.write("frequency", zone.frequency);
		serializer.write("phase", zone.phase);
		serializer.write("wet_dry_mix", zone.wet_dry_mix);
	}


	void deserializeChorusZone(IDeserializer& serializer, EntityRef entity, int /*scene_version*/)
	{
		ChorusZone& zone = m_chorus_zones.insert(entity);
		zone.entity = entity;
		serializer.read(&zone.radius);
		serializer.read(&zone.delay);
		serializer.read(&zone.depth);
		serializer.read(&zone.feedback);
		serializer.read(&zone.frequency);
		serializer.read(&zone.phase);
		serializer.read(&zone.wet_dry_mix);
		m_universe.onComponentCreated(entity, CHORUS_ZONE_TYPE, this);
	}

	void serializeAmbientSound(ISerializer& serializer, EntityRef entity)
	{
		AmbientSound& sound = m_ambient_sounds[entity];
		serializer.write("clip", m_clips.indexOf(sound.clip));
		serializer.write("is_3d", sound.is_3d);
	}


	void deserializeAmbientSound(IDeserializer& serializer, EntityRef entity, int /*scene_version*/)
	{
		AmbientSound& sound = m_ambient_sounds.insert(entity);
		sound.playing_sound = -1;
		sound.entity = entity;
		int clip;
		serializer.read(&clip);
		serializer.read(&sound.is_3d);
		sound.clip = clip >= 0 ? m_clips[clip] : nullptr;
		m_universe.onComponentCreated(entity, AMBIENT_SOUND_TYPE, this);
	}


	void serializeListener(ISerializer&, EntityRef) {}


	void deserializeListener(IDeserializer&, EntityRef entity, int /*scene_version*/)
	{
		m_listener.entity = entity;
		m_universe.onComponentCreated(entity, LISTENER_TYPE, this);
	}


	int getVersion() const override { return (int)AudioSceneVersion::LAST; }


	void clear() override
	{
		for (auto* clip : m_clips)
		{
			clip->clip->getResourceManager().unload(*clip->clip);
			LUMIX_DELETE(m_allocator, clip);
		}
		m_clips.clear();
		m_ambient_sounds.clear();
		m_echo_zones.clear();
		m_chorus_zones.clear();
	}


	int playSound(EntityRef entity, const char* clip_name, bool is_3d)
	{
		auto* clip = getClipInfo(clip_name);
		if (clip) return play(entity, clip, is_3d);

		return -1;
	}

	void updateAnimationEvents()
	{
		if (!m_animation_scene) return;
		
		InputBlob blob(m_animation_scene->getEventStream());
		u32 sound_type = crc32("sound");
		while (blob.getPosition() < blob.getSize())
		{
			u32 type;
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
		}
	}

	void update(float time_delta, bool paused) override
	{
		if (m_listener.entity.isValid())
		{
			const EntityRef listener = (EntityRef) m_listener.entity;
			const Vec3 pos = m_universe.getPosition(listener);
			m_device.setListenerPosition(pos.x, pos.y, pos.z);
			const Matrix orientation = m_universe.getRotation(listener).toMatrix();
			const Vec3 front = orientation.getZVector();
			const Vec3 up = orientation.getYVector();
			m_device.setListenerOrientation(front.x, front.y, front.z, up.x, up.y, up.z);
		}

		for (int i = 0; i < lengthOf(m_playing_sounds); ++i)
		{
			auto& sound = m_playing_sounds[i];
			if (sound.buffer_id == AudioDevice::INVALID_BUFFER_HANDLE) continue;
			if (!sound.entity.isValid()) continue;

			if (sound.is_3d)
			{
				auto pos = m_universe.getPosition((EntityRef)sound.entity);
				m_device.setSourcePosition(sound.buffer_id, pos.x, pos.y, pos.z);
			}

			auto* clip_info = sound.clip;
			if (!clip_info->looped && m_device.isEnd(sound.buffer_id))
			{
				m_device.stop(sound.buffer_id);
				m_playing_sounds[i].buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
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


	void startGame() override
	{
		m_animation_scene = (AnimationScene*)m_universe.getScene(crc32("animation"));
		for (AmbientSound& sound : m_ambient_sounds)
		{
			if (sound.clip) sound.playing_sound = play(sound.entity, sound.clip, sound.is_3d);
		}
	}


	void stopGame() override
	{
		m_animation_scene = nullptr;
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
		m_universe.onComponentCreated(entity, LISTENER_TYPE, this);
	}


	ClipInfo* getAmbientSoundClip(EntityRef entity) override
	{
		return m_ambient_sounds[entity].clip;
	}


	int getClipInfoIndex(ClipInfo* info) override
	{
		for (int i = 0; i < m_clips.size(); ++i)
		{
			if (m_clips[i] == info) return i;
		}
		return -1;
	}


	int getAmbientSoundClipIndex(EntityRef entity) override
	{
		return m_clips.indexOf(m_ambient_sounds[entity].clip);
	}


	void setAmbientSoundClipIndex(EntityRef entity, int index) override
	{
		m_ambient_sounds[entity].clip = m_clips[index];
	}


	void setAmbientSoundClip(EntityRef entity, ClipInfo* clip) override
	{
		m_ambient_sounds[entity].clip = clip;
	}


	void createEchoZone(EntityRef entity)
	{
		EchoZone& zone = m_echo_zones.insert(entity);
		zone.entity = entity;
		zone.delay = 500.0f;
		zone.radius = 10;
		m_universe.onComponentCreated(entity, ECHO_ZONE_TYPE, this);
	}


	float getEchoZoneDelay(EntityRef entity) override
	{
		return m_echo_zones[entity].delay;
	}


	void setEchoZoneDelay(EntityRef entity, float delay) override
	{
		m_echo_zones[entity].delay = delay;
	}


	float getEchoZoneRadius(EntityRef entity) override
	{
		return m_echo_zones[entity].radius;
	}
	
	
	void setEchoZoneRadius(EntityRef entity, float radius) override
	{
		m_echo_zones[entity].radius = radius;
	}


	void destroyEchoZone(EntityRef entity)
	{
		int idx = m_echo_zones.find(entity);
		m_echo_zones.eraseAt(idx);
		m_universe.onComponentDestroyed(entity, ECHO_ZONE_TYPE, this);
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
		m_universe.onComponentCreated(entity, CHORUS_ZONE_TYPE, this);
	}


	float getChorusZoneDelay(EntityRef entity) override
	{
		return m_chorus_zones[entity].delay;
	}


	void setChorusZoneDelay(EntityRef entity, float delay) override
	{
		m_chorus_zones[entity].delay = delay;
	}


	float getChorusZoneRadius(EntityRef entity) override
	{
		return m_chorus_zones[entity].radius;
	}


	void setChorusZoneRadius(EntityRef entity, float radius) override
	{
		m_chorus_zones[entity].radius = radius;
	}


	void destroyChorusZone(EntityRef entity)
	{
		int idx = m_chorus_zones.find(entity);
		m_chorus_zones.eraseAt(idx);
		m_universe.onComponentDestroyed(entity, CHORUS_ZONE_TYPE, this);
	}


	void createAmbientSound(EntityRef entity)
	{
		AmbientSound& sound = m_ambient_sounds.insert(entity);
		sound.entity = entity;
		sound.clip = nullptr;
		sound.playing_sound = -1;
		m_universe.onComponentCreated(entity, AMBIENT_SOUND_TYPE, this);
	}


	void destroyListener(EntityRef entity)
	{
		m_listener.entity = INVALID_ENTITY;
		m_universe.onComponentDestroyed(entity, LISTENER_TYPE, this);
	}


	void destroyAmbientSound(EntityRef entity)
	{
		m_ambient_sounds.erase(entity);
		m_universe.onComponentDestroyed(entity, AMBIENT_SOUND_TYPE, this);
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_listener.entity);
		serializer.write(m_clips.size());
		for (auto* clip : m_clips)
		{
			serializer.write(clip != nullptr);
			if (!clip) continue;

			serializer.write(clip->volume);
			serializer.write(clip->looped);
			serializer.writeString(clip->name);
			serializer.writeString(clip->clip->getPath().c_str());
		}

		serializer.write(m_ambient_sounds.size());
		for (const AmbientSound& sound : m_ambient_sounds)
		{
			serializer.write(m_clips.indexOf(sound.clip));
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


	void deserialize(InputBlob& serializer) override
	{
		clear();

		serializer.read(m_listener.entity);
		if (m_listener.entity.isValid()) {
			m_universe.onComponentCreated((EntityRef)m_listener.entity, LISTENER_TYPE, this);
		}

		int count = 0;
		serializer.read(count);
		m_clips.resize(count);
		for (int i = 0; i < count; ++i) {
			bool is_valid;
			serializer.read(is_valid);
			if (!is_valid) {
				m_clips[i] = nullptr;
				continue;
			}

			auto* clip = LUMIX_NEW(m_allocator, ClipInfo);
			m_clips[i] = clip;
			clip->volume = 1;
			serializer.read(clip->volume);
			serializer.read(clip->looped);
			serializer.readString(clip->name, lengthOf(clip->name));
			clip->name_hash = crc32(clip->name);
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, lengthOf(path));

			clip->clip = static_cast<Clip*>(m_system.getClipManager().load(Path(path)));
		}

		serializer.read(count);
		for (int i = 0; i < count; ++i) {
			AmbientSound sound;
			int clip_idx;
			serializer.read(clip_idx);
			if (clip_idx >= 0) sound.clip = m_clips[clip_idx];
			serializer.read(sound.entity);
			serializer.read(sound.is_3d);

			m_ambient_sounds.insert(sound.entity, sound);
			m_universe.onComponentCreated(sound.entity, AMBIENT_SOUND_TYPE, this);
		}

		serializer.read(count);

		for (int i = 0; i < count; ++i) {
			EchoZone zone;
			serializer.read(zone);

			m_echo_zones.insert(zone.entity, zone);
			m_universe.onComponentCreated(zone.entity, ECHO_ZONE_TYPE, this);
		}

		serializer.read(count);

		for (int i = 0; i < count; ++i) {
			ChorusZone zone;
			serializer.read(zone);

			m_chorus_zones.insert(zone.entity, zone);
			m_universe.onComponentCreated(zone.entity, CHORUS_ZONE_TYPE, this);
		}
	}


	int getClipCount() const override { return m_clips.size(); }


	const char* getClipName(int index) override { return m_clips[index]->name; }


	void addClip(const char* name, const Path& path) override
	{
		auto* clip = LUMIX_NEW(m_allocator, ClipInfo);
		copyString(clip->name, name);
		clip->name_hash = crc32(name);
		clip->clip = static_cast<Clip*>(m_system.getClipManager().load(path));
		clip->looped = false;
		clip->volume = 1;
		m_clips.push(clip);
	}


	void removeClip(ClipInfo* info) override
	{
		for (auto& i : m_playing_sounds)
		{
			if (i.clip == info && i.buffer_id != AudioDevice::INVALID_BUFFER_HANDLE)
			{
				m_device.stop(i.buffer_id);
				i.buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
			}
		}

		for (AmbientSound& sound : m_ambient_sounds)
		{
			if (sound.clip == info)
			{
				sound.clip = nullptr;
				sound.playing_sound = -1;
			}
		}

		auto* clip = info->clip;
		if (clip)
		{
			clip->getResourceManager().unload(*clip);
		}
		LUMIX_DELETE(m_allocator, info);
		m_clips.eraseItem(info);
	}


	ClipInfo* getClipInfo(const char* name) override
	{
		auto hash = crc32(name);
		for (auto* i : m_clips)
		{
			if (i->name_hash == hash) return i;
		}

		return nullptr;
	}


	ClipInfo* getClipInfo(u32 hash) override
	{
		for (auto* i : m_clips)
		{
			if (i->name_hash == hash) return i;
		}

		return nullptr;
	}


	ClipInfo* getClipInfo(int index) override
	{
		if (index >= m_clips.size()) return nullptr;
		return m_clips[index];
	}


	void setClip(int clip_id, const Path& path) override
	{
		auto* clip = m_clips[clip_id]->clip;
		if (clip)
		{
			clip->getResourceManager().unload(*clip);
		}
		auto* new_res = m_system.getClipManager().load(path);
		m_clips[clip_id]->clip = static_cast<Clip*>(new_res);
	}


	SoundHandle play(EntityRef entity, ClipInfo* clip_info, bool is_3d) override
	{
		for (int i = 0; i < lengthOf(m_playing_sounds); ++i)
		{
			if (m_playing_sounds[i].buffer_id == AudioDevice::INVALID_BUFFER_HANDLE)
			{
				auto* clip = clip_info->clip;
				if (!clip->isReady()) return -1;

				int flags = is_3d ? (int)AudioDevice::BufferFlags::IS3D : 0;
				auto buffer = m_device.createBuffer(
					clip->getData(), clip->getSize(), clip->getChannels(), clip->getSampleRate(), flags);
				if (buffer == AudioDevice::INVALID_BUFFER_HANDLE) return -1;
				m_device.play(buffer, clip_info->looped);
				m_device.setVolume(buffer, clip_info->volume);

				Vec3 pos = m_universe.getPosition(entity);
				m_device.setSourcePosition(buffer, pos.x, pos.y, pos.z);

				auto& sound = m_playing_sounds[i];
				sound.is_3d = is_3d;
				sound.buffer_id = buffer;
				sound.entity = entity;
				sound.clip = clip_info;

				for (const EchoZone& zone : m_echo_zones)
				{
					float dist2 = (pos - m_universe.getPosition(zone.entity)).squaredLength();
					float r2 = zone.radius * zone.radius;
					if (dist2 > r2) continue;

					float w = dist2 / r2;
					m_device.setEcho(buffer, 1, 1 - w, zone.delay, zone.delay);
					break;
				}

				for (const ChorusZone& zone : m_chorus_zones)
				{
					float dist2 = (pos - m_universe.getPosition(zone.entity)).squaredLength();
					float r2 = zone.radius * zone.radius;
					if (dist2 > r2) continue;

					m_device.setChorus(buffer, 1, 1, 0, 1, zone.delay, 0);
					break;
				}

				return i;
			}
		}

		return INVALID_SOUND_HANDLE;
	}


	void stop(SoundHandle sound_id) override
	{
		ASSERT(sound_id >= 0 && sound_id < lengthOf(m_playing_sounds));
		m_device.stop(m_playing_sounds[sound_id].buffer_id);
		m_playing_sounds[sound_id].buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
	}


	void setMasterVolume(float volume) { m_device.setMasterVolume(volume); }


	void setVolume(SoundHandle sound_id, float volume) override
	{
		if (sound_id == AudioScene::INVALID_SOUND_HANDLE) return;
		ASSERT(sound_id >= 0 && sound_id < lengthOf(m_playing_sounds));
		m_device.setVolume(m_playing_sounds[sound_id].buffer_id, volume);
	}


	void setEcho(SoundHandle sound_id, float wet_dry_mix, float feedback, float left_delay, float right_delay) override
	{
		ASSERT(sound_id >= 0 && sound_id < lengthOf(m_playing_sounds));
		m_device.setEcho(m_playing_sounds[sound_id].buffer_id, wet_dry_mix, feedback, left_delay, right_delay);
	}

	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	AssociativeArray<EntityRef, AmbientSound> m_ambient_sounds;
	AssociativeArray<EntityRef, EchoZone> m_echo_zones;
	AssociativeArray<EntityRef, ChorusZone> m_chorus_zones;
	AudioDevice& m_device;
	Listener m_listener;
	IAllocator& m_allocator;
	Universe& m_universe;
	Array<ClipInfo*> m_clips;
	AudioSystem& m_system;
	PlayingSound m_playing_sounds[AudioDevice::MAX_PLAYING_SOUNDS];
	AnimationScene* m_animation_scene = nullptr;
};


AudioScene* AudioScene::createInstance(AudioSystem& system,
	Universe& universe,
	IAllocator& allocator)
{
	return LUMIX_NEW(allocator, AudioSceneImpl)(system, universe, allocator);
}


void AudioScene::destroyInstance(AudioScene* scene)
{
	LUMIX_DELETE(static_cast<AudioSceneImpl*>(scene)->m_allocator, scene);
}


void AudioScene::registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(F) \
		do { \
		auto f = &LuaWrapper::wrapMethod<AudioSceneImpl, decltype(&AudioSceneImpl::F), &AudioSceneImpl::F>; \
		LuaWrapper::createSystemFunction(L, "Audio", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(setEcho);
	REGISTER_FUNCTION(playSound);
	REGISTER_FUNCTION(setVolume);
	REGISTER_FUNCTION(setMasterVolume);

	#undef REGISTER_FUNCTION
}




} // namespace Lumix