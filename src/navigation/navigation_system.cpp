#include "navigation_scene.h"
#include "animation/animation_scene.h"
#include "engine/engine.h"
#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/universe.h"
#include "navigation/navigation_scene.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include <DetourAlloc.h>
#include <RecastAlloc.h>


namespace Lumix
{


enum class NavigationSceneVersion : int
{
	LATEST
};


struct Agent
{
	enum Flags : u32
	{
		USE_ROOT_MOTION = 1 << 0,
		GET_ROOT_MOTION_FROM_ANIM_CONTROLLER = 1 << 1
	};

	EntityRef entity;
	float radius;
	float height;
	int agent;
	bool is_finished;
	u32 flags = 0;
	Vec3 root_motion = {0, 0, 0};
	float speed = 0;
	float yaw_diff = 0;
	float stop_distance = 0;
};


struct NavigationSystem final : IPlugin {
	explicit NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{
		ASSERT(s_instance == nullptr);
		s_instance = this;
		dtAllocSetCustom(&detourAlloc, &detourFree);
		rcAllocSetCustom(&recastAlloc, &recastFree);
		NavigationScene::reflect();
		// register flags
		Material::getCustomFlag("no_navigation");
		Material::getCustomFlag("nonwalkable");
	}


	~NavigationSystem() { s_instance = nullptr; }

	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(u32 version, InputMemoryStream& stream) override { return version == 0; }

	static void detourFree(void* ptr) { s_instance->m_allocator.deallocate(ptr); }
	static void* detourAlloc(size_t size, dtAllocHint hint) { return s_instance->m_allocator.allocate(size); }
	static void recastFree(void* ptr) { s_instance->m_allocator.deallocate(ptr); }
	static void* recastAlloc(size_t size, rcAllocHint hint) { return s_instance->m_allocator.allocate(size); }

	const char* getName() const override { return "navigation"; }
	void createScenes(Universe& universe) override;

	static NavigationSystem* s_instance;

	IAllocator& m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


void NavigationSystem::createScenes(Universe& universe)
{
	UniquePtr<NavigationScene> scene = NavigationScene::create(m_engine, *this, universe, m_allocator);
	universe.addScene(scene.move());
}


LUMIX_PLUGIN_ENTRY(navigation)
{
	return LUMIX_NEW(engine.getAllocator(), NavigationSystem)(engine);
}


} // namespace Lumix
