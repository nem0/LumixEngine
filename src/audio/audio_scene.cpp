#include "audio_scene.h"
#include "core/crc32.h"
#include "core/iallocator.h"


namespace Lumix
{


static const uint32 SOURCE_HASH = crc32("source");
static const uint32 LISTENER_HASH = crc32("listener");


struct Listener
{
	Entity entity;
};


struct Source
{
	Entity entity;
};


struct AudioSceneImpl : public AudioScene
{
	AudioSceneImpl(IPlugin& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_system(system)
	{
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override
	{
		ASSERT(false);
		TODO("todo");
	}


	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		ASSERT(false);
		TODO("todo");
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


	void update(float time_delta) override
	{
		ASSERT(false);
		TODO("todo");
	}


	bool ownComponentType(uint32 type) const override
	{
		return type == SOURCE_HASH || type == LISTENER_HASH;
	}


	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }


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


} // namespace Lumix