#include "gui_system.hpp"
#include "engine/engine.hpp"
#include "core/allocator.hpp"
#include "engine/input_system.hpp"
#include "core/math.hpp"
#include "core/path.hpp"
#include "core/profiler.hpp"
#include "engine/reflection.hpp"
#include "engine/resource_manager.hpp"
#include "engine/world.hpp"
#include "gui/gui_module.hpp"
#include "gui/sprite.hpp"
#include "renderer/font.hpp"
#include "renderer/material.hpp"
#include "renderer/pipeline.hpp"
#include "renderer/renderer.hpp"
#include "renderer/render_module.hpp"
#include "renderer/texture.hpp"


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
	{
		GUIModule::reflect();
		LUMIX_GLOBAL_FUNC(GUISystem::enableCursor);
		m_sprite_manager.create(Sprite::TYPE, m_engine.getResourceManager());
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
		
		if (!m_interface) return;

		auto* pipeline = m_interface->getPipeline();
		pipeline->addCustomCommandHandler("renderIngameGUI")
			.callback.bind<&GUISystemImpl::pipelineCallback>(this);
	}


	void pipelineCallback()
	{
		if (!m_interface) return;

		Pipeline* pipeline = m_interface->getPipeline();
		auto* module = (GUIModule*)pipeline->getModule()->getWorld().getModule("gui");
		Vec2 size = m_interface->getSize();
		module->render(*pipeline, size, true);
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
};


LUMIX_PLUGIN_ENTRY(gui) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), GUISystemImpl)(engine);
}


} // namespace Lumix
