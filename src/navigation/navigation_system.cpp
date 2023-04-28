#include "navigation_scene.h"
#include "animation/animation_scene.h"
#include "engine/engine.h"
#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/world.h"
#include "navigation/navigation_scene.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include <DetourAlloc.h>
#include <RecastAlloc.h>


namespace Lumix
{


enum class NavigationModuleVersion : int
{
	LATEST
};


struct NavigationSystem final : ISystem {
	explicit NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{
		ASSERT(s_instance == nullptr);
		s_instance = this;
		dtAllocSetCustom(&detourAlloc, &detourFree);
		rcAllocSetCustom(&recastAlloc, &recastFree);
		NavigationModule::reflect();
		// register flags
		Material::getCustomFlag("no_navigation");
		Material::getCustomFlag("nonwalkable");
	}


	~NavigationSystem() { s_instance = nullptr; }

	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

	static void detourFree(void* ptr) { s_instance->m_allocator.deallocate(ptr); }
	static void* detourAlloc(size_t size, dtAllocHint hint) { return s_instance->m_allocator.allocate(size); }
	static void recastFree(void* ptr) { s_instance->m_allocator.deallocate(ptr); }
	static void* recastAlloc(size_t size, rcAllocHint hint) { return s_instance->m_allocator.allocate(size); }

	const char* getName() const override { return "navigation"; }
	void createModules(World& world) override;

	static NavigationSystem* s_instance;

	IAllocator& m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


void NavigationSystem::createModules(World& world)
{
	UniquePtr<NavigationModule> module = NavigationModule::create(m_engine, *this, world, m_allocator);
	world.addModule(module.move());
}


LUMIX_PLUGIN_ENTRY(navigation)
{
	return LUMIX_NEW(engine.getAllocator(), NavigationSystem)(engine);
}


} // namespace Lumix
