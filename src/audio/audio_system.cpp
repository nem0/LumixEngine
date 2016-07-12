#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip_manager.h"
#include "engine/crc32.h"
#include "engine/path.h"
#include "engine/resource_manager.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "engine/property_register.h"
#include "engine/property_descriptor.h"
#include "renderer/render_scene.h"


static const Lumix::uint32 CLIP_HASH = Lumix::crc32("CLIP");


namespace Lumix
{


static void registerProperties(Lumix::IAllocator& allocator)
{
	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, EnumPropertyDescriptor<AudioScene>)("Sound",
			&AudioScene::getAmbientSoundClipIndex,
			&AudioScene::setAmbientSoundClipIndex,
			&AudioScene::getClipCount,
			&AudioScene::getClipName));

	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<AudioScene>)(
			"3D", &AudioScene::isAmbientSound3D, &AudioScene::setAmbientSound3D));

	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Radius",
			&AudioScene::getEchoZoneRadius,
			&AudioScene::setEchoZoneRadius,
			0.01f,
			FLT_MAX,
			0.1f));
	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Delay (ms)",
			&AudioScene::getEchoZoneDelay,
			&AudioScene::setEchoZoneDelay,
			0.01f,
			FLT_MAX,
			100.0f));
}


struct AudioSystemImpl : public AudioSystem
{
	explicit AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
		, m_device(nullptr)
	{
		registerProperties(engine.getAllocator());
		AudioScene::registerLuaAPI(m_engine.getState());
		m_device = AudioDevice::create(m_engine);
		m_manager.create(CLIP_HASH, m_engine.getResourceManager());
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


	IScene* createScene(Universe& ctx) override
	{
		return AudioScene::createInstance(*this, ctx, m_engine.getAllocator());
	}


	void destroyScene(IScene* scene) override { AudioScene::destroyInstance(static_cast<AudioScene*>(scene)); }


	ClipManager m_manager;
	Engine& m_engine;
	AudioDevice* m_device;
};


LUMIX_PLUGIN_ENTRY(audio)
{
	return LUMIX_NEW(engine.getAllocator(), Lumix::AudioSystemImpl)(engine);
}


} // namespace Lumix

