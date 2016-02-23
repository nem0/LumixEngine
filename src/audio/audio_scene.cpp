#include "audio_scene.h"
#include "audio_device.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/iallocator.h"
#include "core/lua_wrapper.h"
#include "core/matrix.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "lua_script/lua_script_system.h"
#include "universe/universe.h"


namespace Lumix
{


enum class AudioSceneVersion : int
{
	ECHO_ZONES,

	LAST
};


static const uint32 LISTENER_HASH = crc32("audio_listener");
static const uint32 AMBIENT_SOUND_HASH = crc32("ambient_sound");
static const uint32 ECHO_ZONE_HASH = crc32("echo_zone");
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
	ComponentIndex component;
};


struct AmbientSound
{
	Entity entity;
	ComponentIndex component;
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
		m_last_echo_zone_id = 0;
		m_last_ambient_sound_id = 0;
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


	int playSound(int entity, const char* clip_name, bool is_3d)
	{
		auto* clip = getClipInfo(clip_name);
		if (clip) return play(entity, clip, is_3d);

		return -1;
	}


	void sendMessage(uint32 type, void*) override
	{
		static const uint32 register_hash = crc32("registerLuaAPI");
		if (type == register_hash)
		{
			registerLuaAPI();
		}
	}


	void registerLuaAPI()
	{
		auto* scene = m_universe.getScene(crc32("lua_script"));
		if (!scene) return;

		auto* script_scene = static_cast<LuaScriptScene*>(scene);
		lua_State* L = script_scene->getGlobalState();

		#define REGISTER_FUNCTION(F) \
			do { \
			auto f = &LuaWrapper::wrapMethod<AudioSceneImpl, decltype(&AudioSceneImpl::F), &AudioSceneImpl::F>; \
			if (lua_getglobal(L, "Audio") == LUA_TNIL) \
			{ \
				lua_pop(L, 1); \
				lua_newtable(L); \
				lua_setglobal(L, "Audio"); \
				lua_getglobal(L, "Audio"); \
			} \
			lua_pushcfunction(L, f); \
			lua_setfield(L, -2, #F); \
			} while(false) \

		REGISTER_FUNCTION(setEcho);
		REGISTER_FUNCTION(playSound);
		REGISTER_FUNCTION(setVolume);

		#undef REGISTER_FUNCTION
	}


	void update(float time_delta, bool paused) override
	{
		if (m_listener.entity != INVALID_ENTITY)
		{
			auto pos = m_universe.getPosition(m_listener.entity);
			m_device.setListenerPosition(pos.x, pos.y, pos.z);
			Matrix orientation;
			m_universe.getRotation(m_listener.entity).toMatrix(orientation);
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


	bool isAmbientSound3D(ComponentIndex cmp) override
	{
		return m_ambient_sounds[getAmbientSoundIdx(cmp)].is_3d;
	}


	void setAmbientSound3D(ComponentIndex cmp, bool is_3d) override
	{
		m_ambient_sounds[getAmbientSoundIdx(cmp)].is_3d = is_3d;
	}


	void startGame() override
	{
		for (auto& i : m_ambient_sounds)
		{
			if (i.clip) i.playing_sound = play(i.entity, i.clip, i.is_3d);
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

		for (auto& i : m_ambient_sounds)
		{
			i.playing_sound = -1;
		}
	}


	ComponentIndex createListener(Entity entity)
	{
		if (m_listener.entity != INVALID_ENTITY)
		{
			g_log_warning.log("Audio") << "Listener already exists";
			return INVALID_COMPONENT;
		}

		m_listener.entity = entity;
		m_universe.addComponent(entity, LISTENER_HASH, this, 0);
		return 0;
	}


	ClipInfo* getAmbientSoundClip(ComponentIndex cmp) override
	{
		return m_ambient_sounds[getAmbientSoundIdx(cmp)].clip;
	}


	int getClipInfoIndex(ClipInfo* info) override
	{
		for (int i = 0; i < m_clips.size(); ++i)
		{
			if (m_clips[i] == info) return i;
		}
		return -1;
	}


	int getAmbientSoundClipIndex(ComponentIndex cmp) override
	{
		return m_clips.indexOf(m_ambient_sounds[getAmbientSoundIdx(cmp)].clip);
	}


	void setAmbientSoundClipIndex(ComponentIndex cmp, int index) override
	{
		m_ambient_sounds[getAmbientSoundIdx(cmp)].clip = m_clips[index];
	}


	void setAmbientSoundClip(ComponentIndex cmp, ClipInfo* clip) override
	{
		m_ambient_sounds[getAmbientSoundIdx(cmp)].clip = clip;
	}


	ComponentIndex createEchoZone(Entity entity)
	{
		auto& zone = m_echo_zones.emplace();
		zone.entity = entity;
		zone.component = ++m_last_echo_zone_id;
		zone.delay = 500.0f;
		zone.radius = 10;
		m_universe.addComponent(entity, ECHO_ZONE_HASH, this, zone.component);
		return zone.component;
	}


	float getEchoZoneDelay(ComponentIndex cmp) override
	{
		return m_echo_zones[getEchoZoneIdx(cmp)].delay;
	}


	void setEchoZoneDelay(ComponentIndex cmp, float delay) override
	{
		m_echo_zones[getEchoZoneIdx(cmp)].delay = delay;
	}


	float getEchoZoneRadius(ComponentIndex cmp) override
	{
		return m_echo_zones[getEchoZoneIdx(cmp)].radius;
	}
	
	
	void setEchoZoneRadius(ComponentIndex cmp, float radius) override
	{
		m_echo_zones[getEchoZoneIdx(cmp)].radius = radius;
	}


	int getEchoZoneIdx(ComponentIndex component)
	{
		for(int i = 0, c = m_echo_zones.size(); i < c; ++i)
		{
			if (m_echo_zones[i].component == component) return i;
		}
		return -1;
	}


	void destroyEchoZone(ComponentIndex component)
	{
		int idx = getEchoZoneIdx(component);
		auto entity = m_echo_zones[idx].entity;
		m_echo_zones.eraseFast(idx);
		m_universe.destroyComponent(entity, ECHO_ZONE_HASH, this, component);

	}


	ComponentIndex createAmbientSound(Entity entity)
	{
		auto& sound = m_ambient_sounds.emplace();
		sound.component = ++m_last_ambient_sound_id;
		sound.entity = entity;
		sound.clip = nullptr;
		sound.playing_sound = -1;
		m_universe.addComponent(entity, AMBIENT_SOUND_HASH, this, sound.component);
		return sound.component;
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override;


	int getAmbientSoundIdx(ComponentIndex component) const
	{
		for (int i = 0, c = m_ambient_sounds.size(); i < c; ++i)
		{
			if (m_ambient_sounds[i].component == component) return i;
		}
		return -1;
	}


	void destroyListener(ComponentIndex component)
	{
		ASSERT(component == 0);
		auto entity = m_listener.entity;
		m_listener.entity = INVALID_ENTITY;
		m_universe.destroyComponent(entity, LISTENER_HASH, this, component);
	}


	void destroyAmbientSound(ComponentIndex component)
	{
		int idx = getAmbientSoundIdx(component);
		auto entity = m_ambient_sounds[idx].entity;
		m_ambient_sounds.eraseFast(idx);
		m_universe.destroyComponent(entity, AMBIENT_SOUND_HASH, this, component);
	}


	void destroyComponent(ComponentIndex component, uint32 type) override;


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

		serializer.write(m_last_ambient_sound_id);
		serializer.write(m_ambient_sounds.size());
		for (auto& i : m_ambient_sounds)
		{
			serializer.write(m_clips.indexOf(i.clip));
			serializer.write(i.component);
			serializer.write(i.entity);
		}

		serializer.write(m_echo_zones.size());
		for (auto& i : m_echo_zones)
		{
			serializer.write(i);
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
			m_universe.addComponent(m_listener.entity, LISTENER_HASH, this, 0);
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

		serializer.read(m_last_ambient_sound_id);
		serializer.read(count);
		m_ambient_sounds.resize(count);
		for (int i = 0; i < count; ++i)
		{
			auto& sound = m_ambient_sounds[i];

			int clip_idx;
			serializer.read(clip_idx);
			if (clip_idx >= 0) sound.clip = m_clips[clip_idx];
			serializer.read(sound.component);
			serializer.read(sound.entity);

			m_universe.addComponent(sound.entity, AMBIENT_SOUND_HASH, this, sound.component);
		}

		if (version > (int)AudioSceneVersion::ECHO_ZONES)
		{
			serializer.read(count);
			m_echo_zones.resize(count);

			for (auto& i : m_echo_zones)
			{
				serializer.read(i);
				m_universe.addComponent(i.entity, ECHO_ZONE_HASH, this, i.component);
			}
		}
	}


	int getVersion() const override { return (int)AudioSceneVersion::LAST; }


	bool ownComponentType(uint32 type) const override
	{
		return type == LISTENER_HASH || type == AMBIENT_SOUND_HASH;
	}


	ComponentIndex getComponent(Entity entity, uint32 type) override
	{
		if (type == LISTENER_HASH) return m_listener.entity == entity ? 0 : INVALID_COMPONENT;

		for (auto& i : m_ambient_sounds)
		{
			if (i.entity == entity) return i.component;
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

		for (auto& i : m_ambient_sounds)
		{
			if (i.clip == info)
			{
				i.clip = nullptr;
				i.playing_sound = -1;
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
				
				for (auto& zone : m_echo_zones)
				{
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
		float right_delay)
	{
		ASSERT(sound_id >= 0 && sound_id < lengthOf(m_playing_sounds));
		m_device.setEcho(m_playing_sounds[sound_id].buffer_id, wet_dry_mix, feedback, left_delay, right_delay);
	}

	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	int m_last_ambient_sound_id;
	Array<AmbientSound> m_ambient_sounds;
	Array<EchoZone> m_echo_zones;
	int m_last_echo_zone_id;
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
	uint32 type;
	ComponentIndex(AudioSceneImpl::*creator)(Entity);
	void (AudioSceneImpl::*destroyer)(ComponentIndex);
} COMPONENT_INFOS[] = {
	{ LISTENER_HASH, &AudioSceneImpl::createListener, &AudioSceneImpl::destroyListener },
	{ AMBIENT_SOUND_HASH, &AudioSceneImpl::createAmbientSound, &AudioSceneImpl::destroyAmbientSound },
	{ ECHO_ZONE_HASH, &AudioSceneImpl::createEchoZone, &AudioSceneImpl::destroyEchoZone }
};


ComponentIndex AudioSceneImpl::createComponent(uint32 type, Entity entity)
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


void AudioSceneImpl::destroyComponent(ComponentIndex component, uint32 type)
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


} // namespace Lumix