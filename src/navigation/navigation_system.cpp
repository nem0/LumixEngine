#include "navigation_scene.h"
#include "navigation_scene.h"
#include "animation/animation_scene.h"
#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/iallocator.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/profiler.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "engine/vec.h"
#include "lua_script/lua_script_system.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include <DetourAlloc.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <RecastAlloc.h>
#include <cmath>


namespace Lumix
{


enum class NavigationSceneVersion : int
{
	USE_ROOT_MOTION,
	ROOT_MOTION_FROM_ANIM,

	LATEST
};


static const ComponentType NAVMESH_AGENT_TYPE = PropertyRegister::getComponentType("navmesh_agent");
static const ComponentType ANIM_CONTROLLER_TYPE = PropertyRegister::getComponentType("anim_controller");
static const int CELLS_PER_TILE_SIDE = 256;
static const float CELL_SIZE = 0.3f;
static void registerLuaAPI(lua_State* L);


struct Agent
{
	enum Flags : u32
	{
		USE_ROOT_MOTION = 1 << 0,
		GET_ROOT_MOTION_FROM_ANIM_CONTROLLER = 1 << 1
	};

	Entity entity;
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


struct NavigationSystem LUMIX_FINAL : public IPlugin
{
	NavigationSystem(Engine& engine)
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

	BaseProxyAllocator m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


void NavigationSystem::registerProperties()
{
	using namespace PropertyRegister;
	static auto navmesh_agent = component("navmesh_agent",
		property("Radius", &NavigationScene::getAgentRadius, &NavigationScene::setAgentRadius,
			MinAttribute(0)),
		property("Height", &NavigationScene::getAgentHeight, &NavigationScene::setAgentHeight,
			MinAttribute(0)),
		property("Use root motion", &NavigationScene::useAgentRootMotion, &NavigationScene::setUseAgentRootMotion),
		property("Get root motion from animation", &NavigationScene::isGettingRootMotionFromAnim, &NavigationScene::setIsGettingRootMotionFromAnim)
	);
	PropertyRegister::registerComponent(&navmesh_agent);
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
			auto f = &LuaWrapper::wrapMethod<NavigationScene, decltype(&NavigationScene::name), &NavigationScene::name>; \
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
	REGISTER_FUNCTION(getPolygonCount);
	REGISTER_FUNCTION(debugDrawContours);
	REGISTER_FUNCTION(generateTile);
	REGISTER_FUNCTION(save);
	REGISTER_FUNCTION(load);
	REGISTER_FUNCTION(setGeneratorParams);
	REGISTER_FUNCTION(getAgentSpeed);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix
