#include "audio_device.h"
#include "core/path.h"
#include "engine/engine.h"
#include "engine/iplugin.h"


namespace Lumix
{


struct AudioSystemImpl : public IPlugin
{
	AudioSystemImpl(Engine& engine)
		: m_engine(engine)
	{
	}


	bool create() override
	{
		return Audio::init(m_engine, m_engine.getAllocator());
	}


	void destroy() override
	{
		Audio::shutdown();
	}


	const char* getName() const override { return "audio"; }

	IScene* createScene(UniverseContext&) { return nullptr; }
	void destroyScene(IScene* scene) { ASSERT(false); }

	Engine& m_engine;
};


} // namespace Lumix


extern "C" LUMIX_LIBRARY_EXPORT Lumix::IPlugin* createPlugin(Lumix::Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), Lumix::AudioSystemImpl)(engine);
}
