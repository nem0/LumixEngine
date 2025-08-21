#include "gui_system.h"
#include "engine/engine.h"
#include "core/allocator.h"
#include "engine/input_system.h"
#include "core/math.h"
#include "core/path.h"
#include "core/profiler.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_module.h"
#include "gui/sprite.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/render_module.h"
#include "renderer/texture.h"


namespace Lumix
{


struct GUISystemImpl;


struct SpriteManager final : ResourceManager
{
	SpriteManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator)
	{
	}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, Sprite)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		LUMIX_DELETE(m_allocator, static_cast<Sprite*>(&resource));
	}

	IAllocator& m_allocator;
};


struct GUISystemImpl final : GUISystem
{
	static const char* getTextHAlignName(int index)
	{
		switch ((GUIModule::TextHAlign) index)
		{
			case GUIModule::TextHAlign::LEFT: return "left";
			case GUIModule::TextHAlign::RIGHT: return "right";
			case GUIModule::TextHAlign::CENTER: return "center";
		}
		ASSERT(false);
		return "Unknown";
	}

	explicit GUISystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "gui")
		, m_interface(nullptr)
		, m_sprite_manager(m_allocator)
		, m_render_plugin(*this)
	{
		GUIModule::reflect();
		LUMIX_GLOBAL_FUNC(GUISystem::enableCursor);
		m_sprite_manager.create(Sprite::TYPE, m_engine.getResourceManager());
	}

	struct RenderPlugin : Lumix::RenderPlugin {
		RenderPlugin(GUISystemImpl& system)
			: m_system(system)
		{
		}

		RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
			Renderer& renderer = pipeline.getRenderer();
			renderer.setRenderTargets(Span(&input, 1), gbuffer.DS, gpu::FramebufferFlags::READONLY_DEPTH);
			PipelineType type = pipeline.getType();
			if (type != PipelineType::GAME_VIEW && type != PipelineType::SCENE_VIEW) return input;
			auto* module = (GUIModule*)pipeline.getModule()->getWorld().getModule("gui");
			Vec2 size = m_system.m_interface->getSize();
			module->render(pipeline, size, true, type == PipelineType::SCENE_VIEW);
			return input;
		}

		GUISystemImpl& m_system;
	};

	void initEnd() override {
		auto* renderer = (Renderer*)m_engine.getSystemManager().getSystem("renderer");
		renderer->addPlugin(m_render_plugin);
	}

	void shutdownStarted() override {
		auto* renderer = (Renderer*)m_engine.getSystemManager().getSystem("renderer");
		renderer->removePlugin(m_render_plugin);
	}

	~GUISystemImpl() { m_sprite_manager.destroy(); }

	Engine& getEngine() override { return m_engine; }

	void createModules(World& world) override
	{
		UniquePtr<GUIModule> module = GUIModule::createInstance(*this, world, m_allocator);
		world.addModule(module.move());
	}

	void setCursor(os::CursorType type) override {
		if (m_interface) m_interface->setCursor(type);
	}

	void enableCursor(bool enable) override {
		if (m_interface) m_interface->enableCursor(enable);
	}


	void setInterface(Interface* interface) override
	{
		m_interface = interface;
	}


	void stopGame() override
	{
		Pipeline* pipeline = m_interface->getPipeline();
		pipeline->clearDraw2D();
	}


	const char* getName() const override { return "gui"; }

	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

	TagAllocator m_allocator;
	Engine& m_engine;
	SpriteManager m_sprite_manager;
	Interface* m_interface;
	RenderPlugin m_render_plugin;
};


LUMIX_PLUGIN_ENTRY(gui) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), GUISystemImpl)(engine);
}


} // namespace Lumix
