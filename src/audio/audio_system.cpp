#include "audio_system.h"
#include "audio_device.h"
#include "audio_scene.h"
#include "clip_manager.h"
#include "core/crc32.h"
#include "core/path.h"
#include "core/resource_manager.h"
#include "engine/engine.h"
#include "engine/iplugin.h"


namespace Lumix
{


struct AudioSystemImpl : public AudioSystem
{
	AudioSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_manager(engine.getAllocator())
	{
	}


	ClipManager& getClipManager() override { return m_manager; }


	bool create() override
	{
		bool result = Audio::init(m_engine);
		m_manager.create(crc32("CLIP"), m_engine.getResourceManager());
		return result;
	}


	void destroy() override
	{
		Audio::shutdown();
		m_manager.destroy();
	}


	const char* getName() const override { return "audio"; }


	IScene* createScene(UniverseContext& ctx)
	{
		return AudioScene::createInstance(*this, *ctx.m_universe, m_engine.getAllocator());
	}


	void destroyScene(IScene* scene) { AudioScene::destroyInstance(static_cast<AudioScene*>(scene)); }


	ClipManager m_manager;
	Engine& m_engine;
};


} // namespace Lumix


extern "C" LUMIX_LIBRARY_EXPORT Lumix::IPlugin* createPlugin(Lumix::Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), Lumix::AudioSystemImpl)(engine);
}
