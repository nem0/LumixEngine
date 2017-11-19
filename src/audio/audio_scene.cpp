#include "audio_scene.h"
#include "animation/animation_scene.h"
#include "audio_device.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/iallocator.h"
#include "engine/lua_wrapper.h"
#include "engine/matrix.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "lua_script/lua_script_system.h"


namespace Lumix
{


static const ComponentType LISTENER_TYPE = Reflection::getComponentType("audio_listener");
static const ComponentType AMBIENT_SOUND_TYPE = Reflection::getComponentType("ambient_sound");
static const ComponentType ECHO_ZONE_TYPE = Reflection::getComponentType("echo_zone");
static const ResourceType CLIP_RESOURCE_TYPE("clip");


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
	AudioScene::ClipInfo* clip;
	bool is_3d;
};


struct AudioSceneImpl LUMIX_FINAL : public AudioScene
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
		context.registerComponentType(LISTENER_TYPE, this, &AudioSceneImpl::serializeListener, &AudioSceneImpl::deserializeListener);
		context.registerComponentType(AMBIENT_SOUND_TYPE, this, &AudioSceneImpl::serializeAmbientSound, &AudioSceneImpl::deserializeAmbientSound);
		context.registerComponentType(ECHO_ZONE_TYPE, this, &AudioSceneImpl::serializeEchoZone, &AudioSceneImpl::deserializeEchoZone);
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


	void serializeEchoZone(ISerializer& serializer, ComponentHandle cmp)
	{
		EchoZone& zone = m_echo_zones[{cmp.index}];
		serializer.write("radius", zone.radius);
		serializer.write("delay", zone.delay);
	}


	void deserializeEchoZone(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		EchoZone& zone = m_echo_zones.insert(entity);
		zone.entity = entity;
		serializer.read(&zone.radius);
		serializer.read(&zone.delay);
		m_universe.addComponent(entity, ECHO_ZONE_TYPE, this, {entity.index});
	}


	void serializeAmbientSound(ISerializer& serializer, ComponentHandle cmp)
	{
		AmbientSound& sound = m_ambient_sounds[{cmp.index}];
		serializer.write("clip", m_clips.indexOf(sound.clip));
		serializer.write("is_3d", sound.is_3d);
	}


	void deserializeAmbientSound(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		AmbientSound& sound = m_ambient_sounds.insert(entity);
		sound.playing_sound = -1;
		sound.entity = entity;
		int clip;
		serializer.read(&clip);
		serializer.read(&sound.is_3d);
		sound.clip = clip >= 0 ? m_clips[clip] : nullptr;
		m_universe.addComponent(entity, AMBIENT_SOUND_TYPE, this, {entity.index});
	}


	void serializeListener(ISerializer&, ComponentHandle) {}


	void deserializeListener(IDeserializer&, Entity entity, int /*scene_version*/)
	{
		m_listener.entity = entity;
		m_universe.addComponent(entity, LISTENER_TYPE, this, {0});
	}


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
	}


	int playSound(Entity entity, const char* clip_name, bool is_3d)
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
			ComponentHandle cmp;
			blob.read(type);
			blob.read(cmp);
			blob.read(size);
			if (type == sound_type)
			{
				SoundAnimationEvent event;
				blob.read(event);
				ClipInfo* clip = getClipInfo(event.clip);
				Entity entity = m_animation_scene->getControllerEntity(cmp);
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

			if (sound.is_3d)
			{
				auto pos = m_universe.getPosition(sound.entity);
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
		EchoZone& zone = m_echo_zones.insert(entity);
		zone.entity = entity;
		zone.delay = 500.0f;
		zone.radius = 10;
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
		AmbientSound& sound = m_ambient_sounds.insert(entity);
		sound.entity = entity;
		sound.clip = nullptr;
		sound.playing_sound = -1;
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
	}


	void deserialize(InputBlob& serializer) override
	{
		clear();

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
		for (int i = 0; i < count; ++i)
		{
			AmbientSound sound;
			int clip_idx;
			serializer.read(clip_idx);
			if (clip_idx >= 0) sound.clip = m_clips[clip_idx];
			serializer.read(sound.entity);
			serializer.read(sound.is_3d);

			ComponentHandle cmp = {sound.entity.index};
			m_ambient_sounds.insert(sound.entity, sound);
			m_universe.addComponent(sound.entity, AMBIENT_SOUND_TYPE, this, cmp);
		}

		serializer.read(count);

		for (int i = 0; i < count; ++i)
		{
			EchoZone zone;
			serializer.read(zone);

			m_echo_zones.insert(zone.entity, zone);
			m_universe.addComponent(zone.entity, ECHO_ZONE_TYPE, this, {zone.entity.index});
		}
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


	SoundHandle play(Entity entity, ClipInfo* clip_info, bool is_3d) override
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

	AssociativeArray<Entity, AmbientSound> m_ambient_sounds;
	AssociativeArray<Entity, EchoZone> m_echo_zones;
	AudioDevice& m_device;
	Listener m_listener;
	IAllocator& m_allocator;
	Universe& m_universe;
	Array<ClipInfo*> m_clips;
	AudioSystem& m_system;
	PlayingSound m_playing_sounds[AudioDevice::MAX_PLAYING_SOUNDS];
	AnimationScene* m_animation_scene = nullptr;
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
	REGISTER_FUNCTION(setMasterVolume);

	#undef REGISTER_FUNCTION
}




} // namespace Lumix