#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"


namespace Lumix
{


struct ClipManager final : ResourceManager {
	explicit ClipManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator)
	{}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, Clip)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		LUMIX_DELETE(m_allocator, static_cast<Clip*>(&resource));
	}

	IAllocator& m_allocator;
};


struct AudioSystemImpl final : AudioSystem {
	explicit AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
	{
		AudioScene::reflect(engine);
	}


	~AudioSystemImpl()
	{
		m_manager.destroy();
	}

	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(u32 version, InputMemoryStream& stream) override { return version == 0; }

	void init() override
	{
		m_device = AudioDevice::create(m_engine);
		m_manager.create(Clip::TYPE, m_engine.getResourceManager());
	}


	Engine& getEngine() override { return m_engine; }
	AudioDevice& getDevice() override { return *m_device; }


	const char* getName() const override { return "audio"; }


	void createScenes(Universe& ctx) override
	{
		UniquePtr<AudioScene> scene = AudioScene::createInstance(*this, ctx, m_engine.getAllocator());
		ctx.addScene(scene.move());
	}


	ClipManager m_manager;
	Engine& m_engine;
	UniquePtr<AudioDevice> m_device;
};


LUMIX_PLUGIN_ENTRY(audio)
{
	return LUMIX_NEW(engine.getAllocator(), AudioSystemImpl)(engine);
}


} // namespace Lumix

