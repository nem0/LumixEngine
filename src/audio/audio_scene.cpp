#include "audio_scene.h"
#include "audio_device.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/iallocator.h"
#include "engine/lua_wrapper.h"
#include "engine/matrix.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/engine.h"
#include "lua_script/lua_script_system.h"
#include "engine/universe/universe.h"


namespace Lumix
{


enum class AudioSceneVersion : int
{
	ECHO_ZONES,
	REFACTOR,

	LAST
};


static const ComponentType LISTENER_TYPE = PropertyRegister::getComponentType("audio_listener");
static const ComponentType AMBIENT_SOUND_TYPE = PropertyRegister::getComponentType("ambient_sound");
static const ComponentType ECHO_ZONE_TYPE = PropertyRegister::getComponentType("echo_zone");
static const uint32 CLIP_RESOURCE_HASH = crc32("CLIP");


struct Listener
{
	Entity entity;
};


struct EchoZone
{
	Entity entity;
	float radius;
	float delay;
};


struct AmbientSound
{
	Entity entity;
	AudioScene::ClipInfo* clip;
	bool is_3d;
	int playing_sound;
};


struct PlayingSound
{
	AudioDevice::BufferHandle buffer_id;
	Entity entity;
	float time;
	AudioScene::ClipInfo* clip;
};


struct AudioSceneImpl : public AudioScene
{
	AudioSceneImpl(AudioSystem& system, Universe& context, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(context)
		, m_clips(allocator)
		, m_system(system)
		, m_device(system.getDevice())
		, m_ambient_sounds(allocator)
		, m_echo_zones(allocator)
	{
		m_listener.entity = INVALID_ENTITY;
		for (auto& i : m_playing_sounds)
		{
			i.entity = INVALID_ENTITY;
			i.buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
		}
	}


	~AudioSceneImpl()
	{
		clearClips();
	}


	int playSound(Entity entity, const char* clip_name, bool is_3d)
	{
		auto* clip = getClipInfo(clip_name);
		if (clip) return play(entity, clip, is_3d);

		return -1;
	}


	void update(float time_delta, bool paused) override
	{
		if (m_listener.entity != INVALID_ENTITY)
		{
			auto pos = m_universe.getPosition(m_listener.entity);
			m_device.setListenerPosition(pos.x, pos.y, pos.z);
			Matrix orientation = m_universe.getRotation(m_listener.entity).toMatrix();
			auto front = orientation.getZVector();
			auto up = orientation.getYVector();
			m_device.setListenerOrientation(front.x, front.y, front.z, up.x, up.y, up.z);
		}

		for (int i = 0; i < lengthOf(m_playing_sounds); ++i)
		{
			auto& sound = m_playing_sounds[i];
			if (sound.buffer_id == AudioDevice::INVALID_BUFFER_HANDLE) continue;

			auto pos = m_universe.getPosition(sound.entity);
			m_device.setSourcePosition(sound.buffer_id, pos.x, pos.y, pos.z);
			sound.time += time_delta;

			auto* clip_info = sound.clip;
			if (!clip_info->looped && sound.time > clip_info->clip->getLengthSeconds())
			{
				m_device.stop(sound.buffer_id);
				m_playing_sounds[i].buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
			}
		}
		m_device.update(time_delta);
	}


	bool isAmbientSound3D(ComponentHandle cmp) override
	{
		return m_ambient_sounds[{cmp.index}].is_3d;
	}


	void setAmbientSound3D(ComponentHandle cmp, bool is_3d) override
	{
		m_ambient_sounds[{cmp.index}].is_3d = is_3d;
	}


	void startGame() override
	{
		for (int i = 0; i < m_ambient_sounds.size(); ++i)
		{
			auto& sound = m_ambient_sounds.at(i);
			if (sound.clip) sound.playing_sound = play(sound.entity, sound.clip, sound.is_3d);
		}
	}


	void stopGame() override
	{
		for (auto& i : m_playing_sounds)
		{
			if (i.buffer_id != AudioDevice::INVALID_BUFFER_HANDLE)
			{
				m_device.stop(i.buffer_id);
				i.buffer_id = AudioDevice::INVALID_BUFFER_HANDLE;
			}
		}

		for (int i = 0; i < m_ambient_sounds.size(); ++i)
		{
			m_ambient_sounds.at(i).playing_sound = -1;
		}
	}


	ComponentHandle createListener(Entity entity)
	{
		if (m_listener.entity != INVALID_ENTITY)
		{
			g_log_warning.log("Audio") << "Listener already exists";
			return INVALID_COMPONENT;
		}

		m_listener.entity = entity;
		m_universe.addComponent(entity, LISTENER_TYPE, this, {0});
		return {0};
	}


	ClipInfo* getAmbientSoundClip(ComponentHandle cmp) override
	{
		return m_ambient_sounds[{cmp.index}].clip;
	}


	int getClipInfoIndex(ClipInfo* info) override
	{
		for (int i = 0; i < m_clips.size(); ++i)
		{
			if (m_clips[i] == info) return i;
		}
		return -1;
	}


	int getAmbientSoundClipIndex(ComponentHandle cmp) override
	{
		return m_clips.indexOf(m_ambient_sounds[{cmp.index}].clip);
	}


	void setAmbientSoundClipIndex(ComponentHandle cmp, int index) override
	{
		m_ambient_sounds[{cmp.index}].clip = m_clips[index];
	}


	void setAmbientSoundClip(ComponentHandle cmp, ClipInfo* clip) override
	{
		m_ambient_sounds[{cmp.index}].clip = clip;
	}


	ComponentHandle createEchoZone(Entity entity)
	{
		EchoZone zone;
		zone.entity = entity;
		zone.delay = 500.0f;
		zone.radius = 10;
		m_echo_zones.insert(entity, zone);
		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, ECHO_ZONE_TYPE, this, cmp);
		return cmp;
	}


	float getEchoZoneDelay(ComponentHandle cmp) override
	{
		return m_echo_zones[{cmp.index}].delay;
	}


	void setEchoZoneDelay(ComponentHandle cmp, float delay) override
	{
		m_echo_zones[{cmp.index}].delay = delay;
	}


	float getEchoZoneRadius(ComponentHandle cmp) override
	{
		return m_echo_zones[{cmp.index}].radius;
	}
	
	
	void setEchoZoneRadius(ComponentHandle cmp, float radius) override
	{
		m_echo_zones[{cmp.index}].radius = radius;
	}


	void destroyEchoZone(ComponentHandle component)
	{
		Entity entity = {component.index};
		int idx = m_echo_zones.find(entity);
		m_echo_zones.eraseAt(idx);
		m_universe.destroyComponent(entity, ECHO_ZONE_TYPE, this, component);
	}


	ComponentHandle createAmbientSound(Entity entity)
	{
		AmbientSound sound;
		sound.entity = entity;
		sound.clip = nullptr;
		sound.playing_sound = -1;
		m_ambient_sounds.insert(entity, sound);
		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, AMBIENT_SOUND_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createComponent(ComponentType type, Entity entity) override;


	void destroyListener(ComponentHandle component)
	{
		ASSERT(component.index == 0);
		auto entity = m_listener.entity;
		m_listener.entity = INVALID_ENTITY;
		m_universe.destroyComponent(entity, LISTENER_TYPE, this, component);
	}


	void destroyAmbientSound(ComponentHandle component)
	{
		Entity entity = {component.index};
		m_ambient_sounds.erase(entity);
		m_universe.destroyComponent(entity, AMBIENT_SOUND_TYPE, this, component);
	}


	void destroyComponent(ComponentHandle component, ComponentType type) override;


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_listener.entity);
		serializer.write(m_clips.size());
		for (auto* clip : m_clips)
		{
			serializer.write(clip != nullptr);
			if (!clip) continue;

			serializer.write(clip->looped);
			serializer.writeString(clip->name);
			serializer.writeString(clip->clip->getPath().c_str());
		}

		serializer.write(m_ambient_sounds.size());
		for (int i = 0; i < m_ambient_sounds.size(); ++i)
		{
			const auto& sound = m_ambient_sounds.at(i);
			serializer.write(m_clips.indexOf(sound.clip));
			serializer.write(sound.entity);
		}

		serializer.write(m_echo_zones.size());
		for (int i = 0; i < m_echo_zones.size(); ++i)
		{
			serializer.write(m_echo_zones.at(i));
		}
	}


	void clearClips()
	{
		for (auto* clip : m_clips)
		{
			clip->clip->getResourceManager().get(CLIP_RESOURCE_HASH)->unload(*clip->clip);
			LUMIX_DELETE(m_allocator, clip);
		}
		m_clips.clear();
	}


	void deserialize(InputBlob& serializer, int version) override
	{
		clearClips();

		serializer.read(m_listener.entity);
		if (m_listener.entity != INVALID_ENTITY)
		{
			m_universe.addComponent(m_listener.entity, LISTENER_TYPE, this, {0});
		}

		int count = 0;
		serializer.read(count);
		m_clips.resize(count);
		for (int i = 0; i < count; ++i)
		{
			bool is_valid;
			serializer.read(is_valid);
			if (!is_valid)
			{
				m_clips[i] = nullptr;
				continue;
			}

			auto* clip = LUMIX_NEW(m_allocator, ClipInfo);
			m_clips[i] = clip;

			serializer.read(clip->looped);
			serializer.readString(clip->name, lengthOf(clip->name));
			clip->name_hash = crc32(clip->name);
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, lengthOf(path));

			clip->clip = static_cast<Clip*>(m_system.getClipManager().load(Path(path)));
		}

		ComponentHandle dummy_cmp;
		if (version <= (int)AudioSceneVersion::REFACTOR) serializer.read(dummy_cmp);
		serializer.read(count);
		m_ambient_sounds.clear();
		for (int i = 0; i < count; ++i)
		{
			AmbientSound sound;
			int clip_idx;
			serializer.read(clip_idx);
			if (clip_idx >= 0) sound.clip = m_clips[clip_idx];
			if (version <= (int)AudioSceneVersion::REFACTOR) serializer.read(dummy_cmp);
			serializer.read(sound.entity);

			ComponentHandle cmp = {sound.entity.index};
			m_ambient_sounds.insert(sound.entity, sound);
			m_universe.addComponent(sound.entity, AMBIENT_SOUND_TYPE, this, cmp);
		}

		if (version > (int)AudioSceneVersion::ECHO_ZONES)
		{
			serializer.read(count);
			m_echo_zones.clear();

			for (int i = 0; i < count; ++i)
			{
				EchoZone zone;
				serializer.read(zone);
				if (version <= (int)AudioSceneVersion::REFACTOR) serializer.read(dummy_cmp);

				m_echo_zones.insert(zone.entity, zone);
				m_universe.addComponent(zone.entity, ECHO_ZONE_TYPE, this, {zone.entity.index});
			}
		}
	}


	int getVersion() const override { return (int)AudioSceneVersion::LAST; }


	bool ownComponentType(ComponentType type) const override
	{
		return type == LISTENER_TYPE || type == AMBIENT_SOUND_TYPE || type == ECHO_ZONE_TYPE;
	}


	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		if (type == LISTENER_TYPE)
		{
			ComponentHandle listener_cmp = { 0 };
			return m_listener.entity == entity ? listener_cmp : INVALID_COMPONENT;
		}

		if (type == AMBIENT_SOUND_TYPE)
		{
			if (m_ambient_sounds.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		if (type == ECHO_ZONE_TYPE)
		{
			int idx = m_echo_zones.find(entity);
			if (idx < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		return INVALID_COMPONENT;
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

		for (int i = 0; i < m_ambient_sounds.size(); ++i)
		{
			auto& sound = m_ambient_sounds.at(i);
			if (sound.clip == info)
			{
				sound.clip = nullptr;
				sound.playing_sound = -1;
			}
		}

		auto* clip = info->clip;
		if (clip)
		{
			clip->getResourceManager().get(CLIP_RESOURCE_HASH)->unload(*clip);
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


	ClipInfo* getClipInfo(int index) override
	{
		return m_clips[index];
	}


	void setClip(int clip_id, const Path& path) override
	{
		auto* clip = m_clips[clip_id]->clip;
		if (clip)
		{
			clip->getResourceManager().get(CLIP_RESOURCE_HASH)->unload(*clip);
		}
		auto* new_res = m_system.getClipManager().load(path);
		m_clips[clip_id]->clip = static_cast<Clip*>(new_res);
	}


	SoundHandle play(Entity entity, ClipInfo* clip_info, bool is_3d) override
	{
		for (int i = 0; i < lengthOf(m_playing_sounds); ++i)
		{
			if (m_playing_sounds[i].buffer_id == AudioDevice::INVALID_BUFFER_HANDLE)
			{
				auto* clip = clip_info->clip;
				if (!clip->isReady()) return -1;

				int flags = is_3d ? (int)AudioDevice::BufferFlags::IS3D : 0;
				auto buffer = m_device.createBuffer(clip->getData(),
					clip->getSize(),
					clip->getChannels(),
					clip->getSampleRate(),
					flags);
				if (buffer == AudioDevice::INVALID_BUFFER_HANDLE) return -1;
				m_device.play(buffer, clip_info->looped);

				auto pos = m_universe.getPosition(entity);
				m_device.setSourcePosition(buffer, pos.x, pos.y, pos.z);

				auto& sound = m_playing_sounds[i];
				sound.buffer_id = buffer;
				sound.entity = entity;
				sound.time = 0;
				sound.clip = clip_info;
				
				for (int j = 0; j < m_echo_zones.size(); ++j)
				{
					const auto& zone = m_echo_zones.at(j);
					float dist2 = (pos - m_universe.getPosition(zone.entity)).squaredLength();
					float r2 = zone.radius * zone.radius;
					if (dist2 > r2) continue;

					float w = dist2 / r2;
					m_device.setEcho(buffer, 1, 1 - w, zone.delay, zone.delay);
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


	void setVolume(SoundHandle sound_id, float volume) override
	{
		if (sound_id == AudioScene::INVALID_SOUND_HANDLE) return;
		ASSERT(sound_id >= 0 && sound_id < lengthOf(m_playing_sounds));
		m_device.setVolume(m_playing_sounds[sound_id].buffer_id, volume);
	}


	void setEcho(SoundHandle sound_id,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) override
	{
		ASSERT(sound_id >= 0 && sound_id < lengthOf(m_playing_sounds));
		m_device.setEcho(m_playing_sounds[sound_id].buffer_id, wet_dry_mix, feedback, left_delay, right_delay);
	}

	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	AssociativeArray<Entity, AmbientSound> m_ambient_sounds;
	AssociativeArray<Entity, EchoZone> m_echo_zones;
	AudioDevice& m_device;
	Listener m_listener;
	IAllocator& m_allocator;
	Universe& m_universe;
	Array<ClipInfo*> m_clips;
	AudioSystem& m_system;
	PlayingSound m_playing_sounds[AudioDevice::MAX_PLAYING_SOUNDS];
};


static struct
{
	ComponentType type;
	ComponentHandle(AudioSceneImpl::*creator)(Entity);
	void (AudioSceneImpl::*destroyer)(ComponentHandle);
} COMPONENT_INFOS[] = {
	{ LISTENER_TYPE, &AudioSceneImpl::createListener, &AudioSceneImpl::destroyListener },
	{ AMBIENT_SOUND_TYPE, &AudioSceneImpl::createAmbientSound, &AudioSceneImpl::destroyAmbientSound },
	{ ECHO_ZONE_TYPE, &AudioSceneImpl::createEchoZone, &AudioSceneImpl::destroyEchoZone }
};


ComponentHandle AudioSceneImpl::createComponent(ComponentType type, Entity entity)
{
	for(auto& i : COMPONENT_INFOS)
	{
		if(i.type == type)
		{
			return (this->*i.creator)(entity);
		}
	}

	return INVALID_COMPONENT;
}


void AudioSceneImpl::destroyComponent(ComponentHandle component, ComponentType type)
{
	for(auto& i : COMPONENT_INFOS)
	{
		if(i.type == type)
		{
			(this->*i.destroyer)(component);
			return;
		}
	}
}


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

	#undef REGISTER_FUNCTION
}




} // namespace Lumix