#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"


namespace Lumix
{


static void registerProperties(IAllocator& allocator)
{
	struct ClipIndexEnum : Reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return ((AudioScene*)cmp.scene)->getClipCount(); }
		const char* name(ComponentUID cmp, u32 idx) const override { return ((AudioScene*)cmp.scene)->getClipName(idx); }
	};

	using namespace Reflection;
	static auto audio_scene = scene("audio",
		component("ambient_sound",
			property("3D", &AudioScene::isAmbientSound3D, &AudioScene::setAmbientSound3D),
			property("Sound", LUMIX_PROP(AudioScene, AmbientSoundClipIndex), ClipIndexEnum())
		),
		component("audio_listener"),
		component("echo_zone",
			var_property("Radius", &AudioScene::getEchoZone, &EchoZone::radius, MinAttribute(0)),
			var_property("Delay (ms)", &AudioScene::getEchoZone, &EchoZone::delay, MinAttribute(0))
		),
		component("chorus_zone",
			var_property("Radius", &AudioScene::getChorusZone, &ChorusZone::radius, MinAttribute(0)),
			var_property("Delay (ms)", &AudioScene::getChorusZone, &ChorusZone::delay, MinAttribute(0))
		)
	);
	registerScene(audio_scene);
}


struct ClipManager final : ResourceManager
{
	explicit ClipManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator)
	{
	}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, Clip)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		LUMIX_DELETE(m_allocator, static_cast<Clip*>(&resource));
	}

	IAllocator& m_allocator;
};


struct AudioSystemImpl final : AudioSystem
{
	explicit AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
		, m_device(nullptr)
	{
	}


	~AudioSystemImpl()
	{
		AudioDevice::destroy(*m_device);
		m_manager.destroy();
	}


	void init() override
	{
		registerProperties(m_engine.getAllocator());
		AudioScene::registerLuaAPI(m_engine.getState());
		m_device = AudioDevice::create(m_engine);
		m_manager.create(Clip::TYPE, m_engine.getResourceManager());
	}


	Engine& getEngine() override { return m_engine; }
	AudioDevice& getDevice() override { return *m_device; }


	const char* getName() const override { return "audio"; }


	void createScenes(Universe& ctx) override
	{
		auto* scene = AudioScene::createInstance(*this, ctx, m_engine.getAllocator());
		ctx.addScene(scene);
	}


	void destroyScene(IScene* scene) override { AudioScene::destroyInstance(static_cast<AudioScene*>(scene)); }


	ClipManager m_manager;
	Engine& m_engine;
	AudioDevice* m_device;
};


LUMIX_PLUGIN_ENTRY(audio)
{
	return LUMIX_NEW(engine.getAllocator(), AudioSystemImpl)(engine);
}


} // namespace Lumix

