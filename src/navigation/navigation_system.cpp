#include "engine/lumix.h"

#include "core/math.h"
#include "core/profiler.h"

#include "navigation_module.h"
#include "engine/engine.h"
#include "engine/world.h"
#include "navigation/navigation_module.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include <DetourAlloc.h>
#include <RecastAlloc.h>


namespace Lumix
{


struct NavigationSystem final : ISystem {
	explicit NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "navigation")
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
	static void* detourAlloc(size_t size, dtAllocHint hint) { return s_instance->m_allocator.allocate(size, 8); }
	static void recastFree(void* ptr) { s_instance->m_allocator.deallocate(ptr); }
	static void* recastAlloc(size_t size, rcAllocHint hint) { return s_instance->m_allocator.allocate(size, 8); }

	const char* getName() const override { return "navigation"; }
	void createModules(World& world) override;

	static NavigationSystem* s_instance;

	TagAllocator m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


void NavigationSystem::createModules(World& world)
{
	UniquePtr<NavigationModule> module = NavigationModule::create(m_engine, *this, world, m_allocator);
	world.addModule(module.move());
}


LUMIX_PLUGIN_ENTRY(navigation) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), NavigationSystem)(engine);
}


} // namespace Lumix
