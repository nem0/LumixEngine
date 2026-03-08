#include "core/allocator.h"
#include "core/geometry.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/tag_allocator.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "engine/world.h"
#include "gui_ng/gui_ng_module.h"
#include "gui_ng/ui_resource.h"
#include "gui_ng_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include <utility>

namespace Lumix {

struct UISystemImpl : UISystem {
	ui::DocumentResourceManager m_ui_document_manager;

	explicit UISystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "ui")
		, m_ui_document_manager(m_allocator)
		, m_render_plugin(*this)
	{
		UIModule::reflect();
		m_ui_document_manager.create(ui::DocumentResource::TYPE, m_engine.getResourceManager());
	}

	Engine& getEngine() override { return m_engine; }

	const char* getName() const override { return "ui"; }

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
		UniquePtr<UIModule> module = UIModule::createInstance(*this, world, m_allocator);
		world.addModule(std::move(module));
	}

	struct RenderPlugin : Lumix::RenderPlugin {
		RenderPlugin(UISystemImpl& system)
			: m_system(system)
		{}

		RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
			Renderer& renderer = pipeline.getRenderer();
			renderer.setRenderTargets(Span(&input, 1), gbuffer.DS, gpu::FramebufferFlags::READONLY_DEPTH);
			PipelineType type = pipeline.getType();
			if (type != PipelineType::GAME_VIEW && type != PipelineType::SCENE_VIEW) return input;
			UIModule* module = (UIModule*)pipeline.getModule()->getWorld().getModule("ui");
			if (module) {
				const Viewport& vp = pipeline.getViewport();
				Vec2 size = Vec2((float)vp.w, (float)vp.h);
				if (type == PipelineType::GAME_VIEW) {
					module->render(pipeline, size);
				}
				module->render3D(pipeline);
			}
			return input;
		}

		UISystemImpl& m_system;
	};

private:
	Engine& m_engine;
	TagAllocator m_allocator;
	RenderPlugin m_render_plugin;
};

UniquePtr<ISystem> createUISystem(Engine& engine, IAllocator& allocator) {
	return UniquePtr<UISystemImpl>::create(allocator, engine);
}

} // namespace Lumix

LUMIX_PLUGIN_ENTRY(gui_ng) {
	return LUMIX_NEW(engine.getAllocator(), Lumix::UISystemImpl)(engine);
}