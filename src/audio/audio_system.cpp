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

namespace Reflection {
	//inline AudioScene::SoundHandle fromVariant(int i, Span<Variant> args, VariantTag<AudioScene::SoundHandle>) { return args[i].i; }
}

static void registerProperties(IAllocator& allocator)
{
	using namespace Reflection;
	static auto audio_scene = scene("audio",
		functions(
			LUMIX_FUNC(AudioScene::setMasterVolume),
			LUMIX_FUNC(AudioScene::play),
			LUMIX_FUNC(AudioScene::setVolume),
			LUMIX_FUNC(AudioScene::setEcho)
		),
		component("ambient_sound",
			property("3D", &AudioScene::isAmbientSound3D, &AudioScene::setAmbientSound3D),
			property("Sound", LUMIX_PROP(AudioScene, AmbientSoundClip), ResourceAttribute("OGG (*.ogg)", Clip::TYPE))
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
	{
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
		registerProperties(m_engine.getAllocator());
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

