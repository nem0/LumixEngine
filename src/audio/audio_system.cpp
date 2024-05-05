#include "core/profiler.hpp"

#include "audio_system.hpp"
#include "audio_device.hpp"
#include "audio_module.hpp"
#include "clip.hpp"
#include "engine/engine.hpp"
#include "engine/plugin.hpp"
#include "engine/resource_manager.hpp"
#include "engine/world.hpp"


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
		, m_allocator(engine.getAllocator(), "audio")
		, m_manager(m_allocator)
	{
		AudioModule::reflect(engine);
	}


	~AudioSystemImpl()
	{
		m_manager.destroy();
	}

	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

	void initBegin() override
	{
		PROFILE_FUNCTION();
		m_device = AudioDevice::create(m_engine, m_allocator);
		m_manager.create(Clip::TYPE, m_engine.getResourceManager());
	}


	Engine& getEngine() override { return m_engine; }
	AudioDevice& getDevice() override { return *m_device; }


	const char* getName() const override { return "audio"; }


	void createModules(World& world) override
	{
		UniquePtr<AudioModule> module = AudioModule::createInstance(*this, world, m_allocator);
		world.addModule(module.move());
	}


	Engine& m_engine;
	TagAllocator m_allocator;
	ClipManager m_manager;
	UniquePtr<AudioDevice> m_device;
};


LUMIX_PLUGIN_ENTRY(audio) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), AudioSystemImpl)(engine);
}


} // namespace Lumix

