#include "audio_scene.h"
#include "audio_device.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/iallocator.h"
#include "core/matrix.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32 LISTENER_HASH = crc32("audio_listener");
static const uint32 AMBIENT_SOUND_HASH = crc32("ambient_sound");
static const uint32 CLIP_RESOURCE_HASH = crc32("CLIP");
static const int MAX_PLAYING_SOUNDS = 256;


struct Listener
{
	Entity entity;
};


struct AmbientSound
{
	Entity entity;
	ComponentIndex component;
	int clip_id;
	int playing_sound;
};


struct PlayingSound
{
	Lumix::Audio::BufferHandle buffer_id;
	Entity entity;
	float time;
	int clip_id;
};


struct ClipInfo
{
	Clip* clip;
	char name[30];
	uint32 name_hash;
	bool looped;
	int index;
};


struct AudioSceneImpl : public AudioScene
{
	AudioSceneImpl(AudioSystem& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_clips(allocator)
		, m_system(system)
		, m_ambient_sounds(allocator)
	{
		m_last_ambient_sound_id = 0;
		m_listener.entity = INVALID_ENTITY;
		for (auto& i : m_playing_sounds)
		{
			i.entity = INVALID_ENTITY;
			i.buffer_id = Audio::INVALID_BUFFER_HANDLE;
		}
	}


	~AudioSceneImpl() 
	{
		clearClips();
	}


	bool isClipLooped(int clip_id) override
	{
		return m_clips[clip_id]->looped;
	}


	void setClipLooped(int clip_id, bool looped) override
	{
		m_clips[clip_id]->looped = looped;
	}


	void update(float time_delta) override
	{
		if (m_listener.entity != INVALID_ENTITY)
		{
			auto pos = m_universe.getPosition(m_listener.entity);
			Audio::setListenerPosition(pos.x, pos.y, pos.z);
			Matrix orientation;
			m_universe.getRotation(m_listener.entity).toMatrix(orientation);
			auto front = orientation.getZVector();
			auto up = orientation.getYVector();
			Audio::setListenerOrientation(front.x, front.y, front.z, up.x, up.y, up.z);
		}
		
		for (int i = 0; i < Lumix::lengthOf(m_playing_sounds); ++i)
		{
			auto& sound = m_playing_sounds[i];
			if (sound.buffer_id == Audio::INVALID_BUFFER_HANDLE) continue;

			auto pos = m_universe.getPosition(sound.entity);
			Audio::setSourcePosition(sound.buffer_id, pos.x, pos.y, pos.z);
			sound.time += time_delta;

			auto* clip_info = m_clips[sound.clip_id];
			if (!clip_info->looped && sound.time > clip_info->clip->getLengthSeconds())
			{
				Audio::stop(sound.buffer_id);
				m_playing_sounds[i].buffer_id = Audio::INVALID_BUFFER_HANDLE;
			}
		}
		Audio::update(time_delta);
	}


	void startGame() override
	{
		for (auto& i : m_ambient_sounds)
		{
			if (i.clip_id >= 0) i.playing_sound = play(i.entity, i.clip_id);
		}
	}


	void stopGame() override
	{
		for (auto& i : m_playing_sounds)
		{
			if (i.buffer_id != Audio::INVALID_BUFFER_HANDLE)
			{
				Audio::stop(i.buffer_id);
				i.buffer_id = Audio::INVALID_BUFFER_HANDLE;
			}
		}

		for (auto& i : m_ambient_sounds)
		{
			i.playing_sound = -1;
		}
	}


	ComponentIndex createListener(Entity entity)
	{
		m_listener.entity = entity;
		m_universe.addComponent(entity, LISTENER_HASH, this, 0);
		return 0;
	}


	int getAmbientSoundClipId(ComponentIndex cmp) override
	{
		return m_ambient_sounds[getAmbientSoundIdx(cmp)].clip_id;
	}


	void setAmbientSoundClipId(ComponentIndex cmp, int id) override
	{
		m_ambient_sounds[getAmbientSoundIdx(cmp)].clip_id = id;;
	}
	

	ComponentIndex createAmbientSound(Entity entity)
	{
		auto& sound = m_ambient_sounds.pushEmpty();
		sound.component = ++m_last_ambient_sound_id;
		sound.entity = entity;
		sound.clip_id = -1;
		sound.playing_sound = -1;
		m_universe.addComponent(entity, AMBIENT_SOUND_HASH, this, sound.component);
		return sound.component;
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override
	{
		if (type == LISTENER_HASH && m_listener.entity == INVALID_ENTITY)
		{
			return createListener(entity);
		}
		else if (type == AMBIENT_SOUND_HASH)
		{
			return createAmbientSound(entity);
		}
		return INVALID_COMPONENT;
	}


	int getAmbientSoundIdx(ComponentIndex component) const
	{
		for (int i = 0, c = m_ambient_sounds.size(); i < c; ++i)
		{
			if (m_ambient_sounds[i].component == component) return i;
		}
		return -1;
	}


	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		if (type == LISTENER_HASH)
		{
			ASSERT(component == 0);
			auto entity = m_listener.entity;
			m_listener.entity = INVALID_ENTITY;
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == AMBIENT_SOUND_HASH)
		{
			int idx = getAmbientSoundIdx(component);
			auto entity = m_ambient_sounds[idx].entity;
			m_ambient_sounds.eraseFast(idx);
			m_universe.destroyComponent(entity, type, this, component);
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_listener.entity);
		serializer.write(m_clips.size());
		for (auto* clip : m_clips)
		{
			serializer.write(clip != nullptr);
			if (!clip) continue;
			
			serializer.write(clip->looped);
			serializer.write(clip->index);
			serializer.writeString(clip->name);
			serializer.writeString(clip->clip->getPath().c_str());
		}

		serializer.write(m_last_ambient_sound_id);
		serializer.write(m_ambient_sounds.size());
		for (auto& i : m_ambient_sounds)
		{
			serializer.write(i.clip_id);
			serializer.write(i.component);
			serializer.write(i.entity);
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


	void deserialize(InputBlob& serializer, int) override
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
			serializer.read(clip->index);
			serializer.readString(clip->name, Lumix::lengthOf(clip->name));
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, Lumix::lengthOf(path));

			clip->clip = static_cast<Clip*>(m_system.getClipManager().load(Lumix::Path(path)));
		}
		
		serializer.read(m_last_ambient_sound_id);
		serializer.read(count);
		m_ambient_sounds.resize(count);
		for (int i = 0; i < count; ++i)
		{
			auto& sound = m_ambient_sounds[i];

			serializer.read(sound.clip_id);
			serializer.read(sound.component);
			serializer.read(sound.entity);
			sound.clip_id = -1;
		}
	}


	bool ownComponentType(uint32 type) const override
	{
		return type == LISTENER_HASH || type == AMBIENT_SOUND_HASH;
	}


	int getClipCount() const override { return m_clips.size(); }


	int addClip(const char* name, const Lumix::Path& path) override
	{
		auto* clip = LUMIX_NEW(m_allocator, ClipInfo);
		copyString(clip->name, name);
		clip->name_hash = crc32(name);
		clip->clip = static_cast<Clip*>(m_system.getClipManager().load(path));
		clip->looped = false;
		for (int i = 0; i < m_clips.size(); ++i)
		{
			if (!m_clips[i])
			{
				m_clips[i] = clip;
				return i;
			}
		}
		m_clips.push(clip);
		return m_clips.size() - 1;
	}


	void setClipName(int clip_id, const char* clip_name) override
	{
		Lumix::copyString(m_clips[clip_id]->name, clip_name);
	}


	const char* getClipName(int clip_id) override { return m_clips[clip_id]->name; }


	void removeClip(int clip_id) override
	{
		for (auto& i : m_playing_sounds)
		{
			if (i.clip_id == clip_id)
			{
				Audio::stop(i.buffer_id);
				i.buffer_id = Audio::INVALID_BUFFER_HANDLE;
			}
		}

		for (auto& i : m_ambient_sounds)
		{
			if (i.clip_id == clip_id)
			{
				i.clip_id = -1;
				i.playing_sound = -1;
			}
		}

		auto* clip = m_clips[clip_id]->clip;
		if (clip)
		{
			clip->getResourceManager().get(CLIP_RESOURCE_HASH)->unload(*clip);
		}
		LUMIX_DELETE(m_allocator, m_clips[clip_id]);
		m_clips[clip_id] = nullptr;
	}


	void setClip(int clip_id, const Lumix::Path& path) override
	{
		auto* clip = m_clips[clip_id]->clip;
		if (clip)
		{
			clip->getResourceManager().get(CLIP_RESOURCE_HASH)->unload(*clip);
		}
		auto* new_res = m_system.getClipManager().load(path);
		m_clips[clip_id]->clip = static_cast<Clip*>(new_res);
	}


	bool isClipIDValid(int clip_id) override
	{
		return m_clips[clip_id] != nullptr;
	}


	Clip* getClip(int clip_id) override { return m_clips[clip_id]->clip; }


	int getClipID(const char* name) override
	{
		uint32 hash = crc32(name);
		for (int i = 0; i < m_clips.size(); ++i)
		{
			if (m_clips[i] && m_clips[i]->name_hash == hash) return i;
		}

		return -1;
	}


	SoundHandle play(Entity entity, int clip_id) override
	{
		for (int i = 0; i < Lumix::lengthOf(m_playing_sounds); ++i)
		{
			if (m_playing_sounds[i].buffer_id == Audio::INVALID_BUFFER_HANDLE)
			{
				if (!m_clips[clip_id]) return -1;
				auto* clip = m_clips[clip_id]->clip;
				if (!clip->isReady()) return -1;

				int flags = (int)Audio::BufferFlags::IS3D;
				auto buffer = Audio::createBuffer(clip->getData(),
					clip->getSize(),
					clip->getChannels(),
					clip->getSampleRate(),
					flags);
				if (!buffer) return -1;
				Audio::play(buffer, m_clips[clip_id]->looped);

				auto pos = m_universe.getPosition(entity);
				Audio::setSourcePosition(buffer, pos.x, pos.y, pos.z);

				auto& sound = m_playing_sounds[i];
				sound.buffer_id = buffer;
				sound.entity = entity;
				sound.time = 0;
				sound.clip_id = clip_id;
				return i;
			}
		}

		return INVALID_SOUND_HANDLE;
	}


	void stop(SoundHandle sound_id) override
	{
		ASSERT(sound_id >= 0 && sound_id < Lumix::lengthOf(m_playing_sounds));
		Audio::stop(m_playing_sounds[sound_id].buffer_id);
		m_playing_sounds[sound_id].buffer_id = Audio::INVALID_BUFFER_HANDLE;
	}


	void setVolume(SoundHandle sound_id, float volume) override
	{
		ASSERT(sound_id >= 0 && sound_id < Lumix::lengthOf(m_playing_sounds));
		Audio::setVolume(m_playing_sounds[sound_id].buffer_id, volume);
	}


	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	int m_last_ambient_sound_id;
	Array<AmbientSound> m_ambient_sounds;
	Listener m_listener;
	IAllocator& m_allocator;
	Universe& m_universe;
	Array<ClipInfo*> m_clips;
	AudioSystem& m_system;
	PlayingSound m_playing_sounds[MAX_PLAYING_SOUNDS];
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


} // namespace Lumix