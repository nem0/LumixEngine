#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "core/base_proxy_allocator.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/property_descriptor.h"
#include "renderer/render_scene.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"


namespace Lumix
{


static const uint32_t BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32_t MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32_t CONTROLLER_HASH = crc32("physical_controller");
static const uint32_t HEIGHTFIELD_HASH = crc32("physical_heightfield");



struct PhysicsSystemImpl : public PhysicsSystem
{
	PhysicsSystemImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, m_manager(*this, engine.getAllocator())
	{
		m_manager.create(ResourceManager::PHYSICS, engine.getResourceManager());
		registerProperties();
	}

	virtual bool create() override;
	virtual IScene* createScene(UniverseContext& universe) override;
	virtual void destroyScene(IScene* scene) override;
	virtual void destroy() override;

	virtual physx::PxControllerManager* getControllerManager() override
	{
		return m_controller_manager;
	}

	virtual physx::PxPhysics* getPhysics() override
	{
		return m_physics;
	}

	virtual physx::PxCooking* getCooking() override
	{
		return m_cooking;
	}
	
	bool connect2VisualDebugger();
	void registerProperties();

	physx::PxPhysics*			m_physics;
	physx::PxFoundation*		m_foundation;
	physx::PxControllerManager*	m_controller_manager;
	physx::PxAllocatorCallback*	m_physx_allocator;
	physx::PxErrorCallback*		m_error_callback;
	physx::PxCooking*			m_cooking;
	PhysicsGeometryManager		m_manager;
	class Engine&				m_engine;
	class BaseProxyAllocator	m_allocator;
};


extern "C" LUMIX_PHYSICS_API IPlugin* createPlugin(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), PhysicsSystemImpl)(engine);
}


struct EditorPlugin : public WorldEditor::Plugin
{
	EditorPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	virtual bool showGizmo(ComponentUID cmp) override
	{
		PhysicsScene* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		if (cmp.type == CONTROLLER_HASH)
		{
			auto* scene =
				static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			float height = phy_scene->getControllerHeight(cmp.index);
			float radius = phy_scene->getControllerRadius(cmp.index);

			Universe& universe = scene->getUniverse();
			Vec3 pos = universe.getPosition(cmp.entity);
			Vec3 up(0, (height + 2 * radius), 0);
			scene->addDebugCylinder(
				pos - up * 0.5f, up, radius, Vec3(1, 0, 0), 0);
			return true;
		}

		if (cmp.type == BOX_ACTOR_HASH)
		{
			auto* scene =
				static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			Vec3 extents = phy_scene->getHalfExtents(cmp.index);

			Universe& universe = scene->getUniverse();
			Matrix mtx = universe.getMatrix(cmp.entity);

			scene->addDebugCube(mtx.getTranslation(),
								mtx.getXVector() * extents.x,
								mtx.getYVector() * extents.y,
								mtx.getZVector() * extents.z,
								Vec3(1, 0, 0),
								0);
			return true;

		}

		return false;
	}

	WorldEditor& m_editor;
};



struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};

IScene* PhysicsSystemImpl::createScene(UniverseContext& ctx)
{
	return PhysicsScene::create(*this, *ctx.m_universe, m_engine, m_allocator);
}


void PhysicsSystemImpl::destroyScene(IScene* scene)
{
	PhysicsScene::destroy(static_cast<PhysicsScene*>(scene));
}


class AssertNullAllocator : public physx::PxAllocatorCallback
{
	public:
		virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override
		{
			
			void* ret = _aligned_malloc(size, 16);
			// g_log_info.log("PhysX") << "Allocated " << size << " bytes for " << typeName << "
			// from " << filename << "(" << line << ")";
			ASSERT(ret);
			return ret;
		}
		virtual void deallocate(void* ptr) override
		{
			_aligned_free(ptr);
		}
};


void PhysicsSystemImpl::registerProperties()
{
	m_engine.registerComponentType("box_rigid_actor", "Physics Box");
	m_engine.registerComponentType("physical_controller",
									"Physics Controller");
	m_engine.registerComponentType("mesh_rigid_actor", "Physics Mesh");
	m_engine.registerComponentType("physical_heightfield",
									"Physics Heightfield");

	IAllocator& allocator = m_engine.getAllocator();
	m_engine.registerProperty(
		"box_rigid_actor",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
			"dynamic",
			&PhysicsScene::isDynamic,
			&PhysicsScene::setIsDynamic,
			allocator));
	m_engine.registerProperty(
		"box_rigid_actor",
		LUMIX_NEW(allocator, Vec3PropertyDescriptor<PhysicsScene>)(
			"size",
			&PhysicsScene::getHalfExtents,
			&PhysicsScene::setHalfExtents,
			allocator));
	m_engine.registerProperty(
		"mesh_rigid_actor",
		LUMIX_NEW(allocator, FilePropertyDescriptor<PhysicsScene>)(
			"source",
			&PhysicsScene::getShapeSource,
			&PhysicsScene::setShapeSource,
			"Physics (*.pda)",
			allocator));
	m_engine.registerProperty(
		"physical_heightfield",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<PhysicsScene>)(
			"heightmap",
			&PhysicsScene::getHeightmap,
			&PhysicsScene::setHeightmap,
			"Image (*.raw)",
			Lumix::ResourceManager::TEXTURE,
			allocator));
	m_engine.registerProperty(
		"physical_heightfield",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)(
			"xz_scale",
			&PhysicsScene::getHeightmapXZScale,
			&PhysicsScene::setHeightmapXZScale,
			0.0f,
			FLT_MAX,
			0.0f,
			allocator));
	m_engine.registerProperty(
		"physical_heightfield",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)(
			"y_scale",
			&PhysicsScene::getHeightmapYScale,
			&PhysicsScene::setHeightmapYScale,
			0.0f,
			FLT_MAX,
			0.0f,
			allocator));
}


bool PhysicsSystemImpl::create()
{
	m_physx_allocator = m_allocator.newObject<AssertNullAllocator>();
	m_error_callback = m_allocator.newObject<CustomErrorCallback>();
	m_foundation = PxCreateFoundation(
		PX_PHYSICS_VERSION,
		*m_physx_allocator,
		*m_error_callback
	);

	m_physics = PxCreatePhysics(
		PX_PHYSICS_VERSION,
		*m_foundation,
		physx::PxTolerancesScale()
	);
	
	m_controller_manager = PxCreateControllerManager(*m_foundation);
	m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_foundation, physx::PxCookingParams());
	connect2VisualDebugger();
	return true;
}


void PhysicsSystemImpl::destroy()
{
	m_controller_manager->release();
	m_cooking->release();
	m_physics->release();
	m_foundation->release();
	m_allocator.deleteObject(m_physx_allocator);
	m_allocator.deleteObject(m_error_callback);
}


bool PhysicsSystemImpl::connect2VisualDebugger()
{
	if(m_physics->getPvdConnectionManager() == nullptr)
		return false;

	const char* pvd_host_ip = "127.0.0.1";
	int port = 5425;
	unsigned int timeout = 100; 
	physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

	PVD::PvdConnection* theConnection = physx::PxVisualDebuggerExt::createConnection(m_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
	return theConnection != nullptr;
}


void CustomErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
{
	g_log_error.log("PhysX") << message;
}


extern "C" LUMIX_PHYSICS_API void setWorldEditor(Lumix::WorldEditor& editor)
{
	auto* plugin = LUMIX_NEW(editor.getAllocator(), EditorPlugin)(editor);
	editor.addPlugin(*plugin);
}


} // !namespace Lumix



