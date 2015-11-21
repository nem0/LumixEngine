#include "audio_scene.h"
#include "audio_device.h"
#include "audio_system.h"
#include "clip_manager.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/iallocator.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32 LISTENER_HASH = crc32("audio_listener");
static const uint32 CLIP_RESOURCE_HASH = crc32("CLIP");
static const int MAX_PLAYING_SOUNDS = 256;


struct Listener
{
	Entity entity;
};


struct Source
{
	Entity entity;
	ComponentIndex component;
};


struct PlayingSound
{
	Lumix::Audio::BufferHandle buffer_id;
	Entity entity;
};


struct ClipInfo
{
	Clip* clip;
	char name[30];
	uint32 name_hash;
	int index;
};


struct AudioSceneImpl : public AudioScene
{
	AudioSceneImpl(AudioSystem& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_sources(allocator)
		, m_clips(allocator)
		, m_system(system)
	{
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


	void update(float time_delta) override
	{
		if (m_listener.entity != INVALID_ENTITY)
		{
			auto pos = m_universe.getPosition(m_listener.entity);
			Audio::setListenerPosition(0, pos.x, pos.y, pos.z);
		}
		for (auto& i : m_playing_sounds)
		{
			if (i.buffer_id == Audio::INVALID_BUFFER_HANDLE) continue;
			auto pos = m_universe.getPosition(i.entity);
			Audio::setSourcePosition(i.buffer_id, pos.x, pos.y, pos.z);
		}
		Audio::update(time_delta);
	}


	ComponentIndex createListener(Entity entity)
	{
		m_listener.entity = entity;
		m_universe.addComponent(entity, LISTENER_HASH, this, 0);
		return 0;
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override
	{
		if (type == LISTENER_HASH && m_listener.entity == INVALID_ENTITY)
		{
			return createListener(entity);
		}
		return INVALID_COMPONENT;
	}


	int getSourceIndex(ComponentIndex component)
	{
		for (int i = 0, c = m_sources.size(); i < c; ++i)
		{
			if (m_sources[i].component == component) return i;
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
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_listener.entity);
		serializer.write(m_clips.size());
		for (auto* clip : m_clips)
		{
			serializer.write(clip != nullptr);
			if (!clip) continue;
			
			serializer.write(clip->index);
			serializer.writeString(clip->name);
			serializer.writeString(clip->clip->getPath().c_str());
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

			serializer.read(clip->index);
			serializer.readString(clip->name, Lumix::lengthOf(clip->name));
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, Lumix::lengthOf(path));

			clip->clip = static_cast<Clip*>(m_system.getClipManager().load(Lumix::Path(path)));
		}
	}


	bool ownComponentType(uint32 type) const override { return type == LISTENER_HASH; }


	int getClipCount() const override { return m_clips.size(); }


	int addClip(const char* name, const Lumix::Path& path) override
	{
		auto* clip = LUMIX_NEW(m_allocator, ClipInfo);
		copyString(clip->name, name);
		clip->name_hash = crc32(name);
		clip->clip = static_cast<Clip*>(m_system.getClipManager().load(path));
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
		auto* clip = m_clips[clip_id]->clip;
		if (clip)
		{
			clip->getResourceManager().get(CLIP_RESOURCE_HASH)->unload(*clip);
		}
		LUMIX_DELETE(m_allocator, clip);
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
		auto* clip = m_clips[clip_id]->clip;
		if (!clip) return -1;

		auto buffer = Audio::createBuffer(clip->getData(),
			clip->getSize(),
			clip->getChannels(),
			clip->getSampleRate(),
			(int)Audio::BufferFlags::IS3D);
		if (!buffer) return -1;
		Audio::play(buffer);

		auto pos = m_universe.getPosition(entity);
		Audio::setSourcePosition(buffer, pos.x, pos.y, pos.z);

		for (int i = 0; i < Lumix::lengthOf(m_playing_sounds); ++i)
		{
			if (m_playing_sounds[i].buffer_id == Audio::INVALID_BUFFER_HANDLE)
			{
				auto& sound = m_playing_sounds[i];
				sound.buffer_id = buffer;
				sound.entity = entity;
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

	Lumix::Array<Source> m_sources;
	Listener m_listener;
	IAllocator& m_allocator;
	Universe& m_universe;
	Array<ClipInfo*> m_clips;
	AudioSystem& m_system;
	PlayingSound m_playing_sounds[MAX_PLAYING_SOUNDS];
};


AudioScene* AudioScene::createInstance(AudioSystem& system,
	Engine& engine,
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