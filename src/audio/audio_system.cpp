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
			&AudioScene::getClipName,
			allocator));

	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<AudioScene>)("3D",
			&AudioScene::isAmbientSound3D,
			&AudioScene::setAmbientSound3D,
			allocator));

	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Radius",
			&AudioScene::getEchoZoneRadius,
			&AudioScene::setEchoZoneRadius,
			0.01f,
			FLT_MAX,
			0.1f,
			allocator));
	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Delay (ms)",
			&AudioScene::getEchoZoneDelay,
			&AudioScene::setEchoZoneDelay,
			0.01f,
			FLT_MAX,
			100.0f,
			allocator));
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
	}


	Engine& getEngine() override { return m_engine; }
	AudioDevice& getDevice() override { return *m_device; }
	ClipManager& getClipManager() override { return m_manager; }


	bool create() override
	{
		m_device = AudioDevice::create(m_engine);
		if (!m_device) return false;
		m_manager.create(CLIP_HASH, m_engine.getResourceManager());
		return true;
	}


	void destroy() override
	{
		AudioDevice::destroy(*m_device);
		m_manager.destroy();
	}


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

