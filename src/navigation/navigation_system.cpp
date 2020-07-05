#include "navigation_scene.h"
#include "animation/animation_scene.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/reflection.h"
#include "engine/universe.h"
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


static void registerLuaAPI(lua_State* L);


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


struct NavigationSystem final : IPlugin
{
	explicit NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{
		ASSERT(s_instance == nullptr);
		s_instance = this;
		dtAllocSetCustom(&detourAlloc, &detourFree);
		rcAllocSetCustom(&recastAlloc, &recastFree);
		registerLuaAPI(m_engine.getState());
		registerProperties();
		// register flags
		Material::getCustomFlag("no_navigation");
		Material::getCustomFlag("nonwalkable");
	}


	~NavigationSystem()
	{
		s_instance = nullptr;
	}

	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(u32 version, InputMemoryStream& stream) override { return version == 0; }

	static void detourFree(void* ptr)
	{
		s_instance->m_allocator.deallocate(ptr);
	}


	static void* detourAlloc(size_t size, dtAllocHint hint)
	{
		return s_instance->m_allocator.allocate(size);
	}


	static void recastFree(void* ptr)
	{
		s_instance->m_allocator.deallocate(ptr);
	}


	static void* recastAlloc(size_t size, rcAllocHint hint)
	{
		return s_instance->m_allocator.allocate(size);
	}


	static NavigationSystem* s_instance;


	void registerProperties();
	const char* getName() const override { return "navigation"; }
	void createScenes(Universe& universe) override;
	void destroyScene(IScene* scene) override;

	IAllocator& m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


void NavigationSystem::registerProperties()
{
	using namespace Reflection;
	static auto navigation_scene = scene("navigation",
		functions(
			LUMIX_FUNC(NavigationScene::load)
		),
		component("navmesh_zone", 
			var_property("Extents", &NavigationScene::getZone, &NavmeshZone::extents)
		),
		component("navmesh_agent",
			functions(
				LUMIX_FUNC(NavigationScene::navigate)
			),
			property("Radius", LUMIX_PROP(NavigationScene, AgentRadius),
				MinAttribute(0)),
			property("Height", LUMIX_PROP(NavigationScene, AgentHeight),
				MinAttribute(0)),
			property("Use root motion", &NavigationScene::useAgentRootMotion, &NavigationScene::setUseAgentRootMotion)
			//property("Get root motion from animation", LUMIX_PROP_FULL(NavigationScene, isGettingRootMotionFromAnim, setIsGettingRootMotionFromAnim))
		)
	);
	registerScene(navigation_scene);
}


void NavigationSystem::createScenes(Universe& universe)
{
	NavigationScene* scene = NavigationScene::create(m_engine, *this, universe, m_allocator);
	universe.addScene(scene);
}


void NavigationSystem::destroyScene(IScene* scene)
{
	LUMIX_DELETE(m_allocator, scene);
}



LUMIX_PLUGIN_ENTRY(navigation)
{
	return LUMIX_NEW(engine.getAllocator(), NavigationSystem)(engine);
}


static void registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<&NavigationScene::name>; \
			LuaWrapper::createSystemFunction(L, "Navigation", #name, f); \
		} while(false) \

	REGISTER_FUNCTION(generateNavmesh);
	REGISTER_FUNCTION(navigate);
	REGISTER_FUNCTION(setActorActive);
	REGISTER_FUNCTION(cancelNavigation);
	REGISTER_FUNCTION(debugDrawNavmesh);
	REGISTER_FUNCTION(debugDrawCompactHeightfield);
	REGISTER_FUNCTION(debugDrawHeightfield);
	REGISTER_FUNCTION(debugDrawPath);
	REGISTER_FUNCTION(debugDrawContours);
	REGISTER_FUNCTION(save);
	REGISTER_FUNCTION(load);
	REGISTER_FUNCTION(setGeneratorParams);
	REGISTER_FUNCTION(getAgentSpeed);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix
