#include "core/allocator.h"
#include "core/geometry.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/tag_allocator.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "engine/world.h"
#include "gui_ng/gui_ng_module.h"
#include "gui_ng_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include <utility>

namespace Lumix {

struct GUINGSystemImpl : GUINGSystem {
	explicit GUINGSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "gui_ng")
		, m_render_plugin(*this)
	{}

	Engine& getEngine() override { return m_engine; }

	const char* getName() const override { return "gui_ng"; }

	void serialize(OutputMemoryStream& stream) const override {}

	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

	void initEnd() override {
		auto* renderer = (Renderer*)m_engine.getSystemManager().getSystem("renderer");
		if (renderer) renderer->addPlugin(m_render_plugin);
	}

	void shutdownStarted() override {
		auto* renderer = (Renderer*)m_engine.getSystemManager().getSystem("renderer");
		if (renderer) renderer->removePlugin(m_render_plugin);
	}

	void createModules(World& world) override {
		UniquePtr<GUINGModule> module = GUINGModule::createInstance(*this, world, m_allocator);
		world.addModule(std::move(module));
	}

	struct RenderPlugin : Lumix::RenderPlugin {
		RenderPlugin(GUINGSystemImpl& system)
			: m_system(system)
		{}

		RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
			Renderer& renderer = pipeline.getRenderer();
			renderer.setRenderTargets(Span(&input, 1), gbuffer.DS, gpu::FramebufferFlags::READONLY_DEPTH);
			PipelineType type = pipeline.getType();
			if (type != PipelineType::GAME_VIEW) return input;
			GUINGModule* module = (GUINGModule*)pipeline.getModule()->getWorld().getModule("gui_ng");
			if (module) {
				const Viewport& vp = pipeline.getViewport();
				Vec2 size = Vec2((float)vp.w, (float)vp.h);
				module->render(pipeline, size);
			}
			return input;
		}

		GUINGSystemImpl& m_system;
	};

private:
	Engine& m_engine;
	TagAllocator m_allocator;
	RenderPlugin m_render_plugin;
};

UniquePtr<ISystem> createGUINGSystem(Engine& engine, IAllocator& allocator) {
	return UniquePtr<GUINGSystemImpl>::create(allocator, engine);
}

} // namespace Lumix

LUMIX_PLUGIN_ENTRY(gui_ng) {
	return LUMIX_NEW(engine.getAllocator(), Lumix::GUINGSystemImpl)(engine);
}