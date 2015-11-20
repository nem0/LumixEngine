#include "audio_scene.h"
#include "audio_device.h"
#include "clip_manager.h"
#include "core/crc32.h"
#include "core/iallocator.h"
#include "universe/universe.h"


namespace Lumix
{


namespace Audio
{


static const uint32 SOURCE_HASH = crc32("source");
static const uint32 LISTENER_HASH = crc32("listener");
static const int MAX_LISTENERS_COUNT = 1;


struct Listener
{
	Entity entity;
};


struct Source
{
	Entity entity;
	ComponentIndex component;
};


struct AudioSceneImpl : public AudioScene
{
	AudioSceneImpl(IPlugin& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_system(system)
		, m_sources(allocator)
	{
		m_last_source_id = 0;
		for(auto& l : m_listeners)
		{
			l.entity = INVALID_ENTITY;
		}
	}


	void update(float time_delta) override
	{
		if(m_listeners[0].entity != INVALID_ENTITY)
		{
			auto pos = m_universe.getPosition(m_listeners[0].entity);
			setListenerPosition(0, pos.x, pos.y, pos.z);
		}
	}


	void createListener(Entity entity)
	{
		m_listeners[0].entity = entity;
		m_universe.addComponent(entity, LISTENER_HASH, this, 0);
	}


	void createSource(Entity entity)
	{
		auto& source = m_sources.pushEmpty();
		source.entity = entity;
		source.component = m_last_source_id;
		++m_last_source_id;
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override
	{
		if(type == LISTENER_HASH && m_listeners[0].entity == INVALID_ENTITY)
		{
			createListener(entity);
		}
		else if(type == SOURCE_HASH)
		{
			createSource(entity);
		}
	}


	int getSourceIndex(ComponentIndex component)
	{
		for(int i = 0, c = m_sources.size(); i < c; ++i)
		{
			if(m_sources[i].component == component) return i;
		}
		return -1;
	}


	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		if(type == LISTENER_HASH)
		{
			auto entity = m_listeners[component].entity;
			m_listeners[component].entity = INVALID_ENTITY;
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == SOURCE_HASH)
		{
			int index = getSourceIndex(component);
			auto entity = m_sources[index].entity;
			m_sources.eraseFast(index);
			m_universe.destroyComponent(entity, type, this, component);
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		ASSERT(false);
		TODO("todo");
	}


	void deserialize(InputBlob& serializer, int version) override
	{
		ASSERT(false);
		TODO("todo");
	}


	bool ownComponentType(uint32 type) const override
	{
		return type == SOURCE_HASH || type == LISTENER_HASH;
	}


	int getClipCount() const override
	{
		ASSERT(false);
		TODO("todo");
	}


	int addClip(const char* name, const Lumix::Path& path) override
	{
		ASSERT(false);
		TODO("todo");
		return -1;
	}


	void removeClip(int clip_id) override
	{
		ASSERT(false);
		TODO("todo");
	}


	int getClipID(const char* name) override
	{
		ASSERT(false);
		TODO("todo");
		return -1;
	}


	int play(Entity entity, int clip_id) override
	{
		ASSERT(false);
		TODO("todo");
	}


	void stop(int sound_id) override
			{
		ASSERT(false);
		TODO("todo");
	}


	void setVolume(int sound_id, float volume) override
	{
		ASSERT(false);
		TODO("todo");
	}


	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	Lumix::Array<Source> m_sources;
	int m_last_source_id;
	Listener m_listeners[MAX_LISTENERS_COUNT];
	IAllocator& m_allocator;
	Universe& m_universe;
	IPlugin& m_system;
};


static AudioScene* createInstance(IPlugin& system, Engine& engine, Universe& universe, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, AudioSceneImpl)(system, universe, allocator);
}


static void destroyInstance(AudioScene* scene)
{
	LUMIX_DELETE(static_cast<AudioSceneImpl*>(scene)->m_allocator, scene);
}


} //namespace Audio


} // namespace Lumix