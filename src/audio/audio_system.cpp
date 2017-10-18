#include "audio_system.h"
#include "animation/animation_scene.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip_manager.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/properties.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "renderer/render_scene.h"


namespace Lumix
{


static const ResourceType CLIP_TYPE("clip");


static void registerProperties(IAllocator& allocator)
{
	using namespace Properties;
	static auto ambient_sound = component("ambient_sound",
		property("3D", &AudioScene::isAmbientSound3D, &AudioScene::setAmbientSound3D),
		dyn_enum_property("Sound", &AudioScene::getAmbientSoundClipIndex, &AudioScene::setAmbientSoundClipIndex, &AudioScene::getClipCount, &AudioScene::getClipName)
	);
	Properties::registerComponent(&ambient_sound);
	
	static auto audio_listener = component("audio_listener");
	Properties::registerComponent(&audio_listener);

	static auto echo_zone = component("echo_zone",
		property("Radius", &AudioScene::getEchoZoneRadius, &AudioScene::setEchoZoneRadius,
			MinAttribute(0)),
		property("Delay (ms)", &AudioScene::getEchoZoneDelay, &AudioScene::setEchoZoneDelay,
			MinAttribute(0))
	);
	Properties::registerComponent(&echo_zone);
}


struct AudioSystemImpl LUMIX_FINAL : public AudioSystem
{
	explicit AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
		, m_device(nullptr)
	{
		registerProperties(engine.getAllocator());
		AudioScene::registerLuaAPI(m_engine.getState());
		m_device = AudioDevice::create(m_engine);
		m_manager.create(CLIP_TYPE, m_engine.getResourceManager());
	}


	~AudioSystemImpl()
	{
		AudioDevice::destroy(*m_device);
		m_manager.destroy();
	}


	Engine& getEngine() override { return m_engine; }
	AudioDevice& getDevice() override { return *m_device; }
	ClipManager& getClipManager() override { return m_manager; }


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

