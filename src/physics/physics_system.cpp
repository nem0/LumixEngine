#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "core/crc32.h"
#include "core/log.h"
#include "editor/world_editor.h"
#include "editor/property_descriptor.h"
#include "engine/engine.h"
#include "physics/physics_scene.h"
#include "physics/physics_system_impl.h"


namespace Lumix
{


static const uint32_t BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32_t MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32_t CONTROLLER_HASH = crc32("physical_controller");
static const uint32_t HEIGHTFIELD_HASH = crc32("physical_heightfield");


extern "C" IPlugin* createPlugin()
{
	return LUMIX_NEW(PhysicsSystem)();
}


struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};

IScene* PhysicsSystem::createScene(Universe& universe)
{
	return PhysicsScene::create(*this, universe, *m_impl->m_engine);
}


class AssertNullAllocator : public physx::PxAllocatorCallback
{
	public:
		virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override
		{
			void* ret = _aligned_malloc(size, 16);
			//g_log_info.log("PhysX") << "Allocated " << size << " bytes for " << typeName << " from " << filename << "(" << line << ")";
			ASSERT(ret);
			return ret;
		}
		virtual void deallocate(void* ptr) override
		{
			_aligned_free(ptr);
		}
};


bool PhysicsSystem::create(Engine& engine)
{
	engine.getWorldEditor()->registerProperty("box_rigid_actor", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)("dynamic", &PhysicsScene::getIsDynamic, &PhysicsScene::setIsDynamic));
	engine.getWorldEditor()->registerProperty("box_rigid_actor", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)("size", &PhysicsScene::getHalfExtents, &PhysicsScene::setHalfExtents));
	engine.getWorldEditor()->registerProperty("mesh_rigid_actor", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)("source", &PhysicsScene::getShapeSource, &PhysicsScene::setShapeSource, IPropertyDescriptor::FILE, "Physics (*.pda)"));
	engine.getWorldEditor()->registerProperty("physical_heightfield", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)("heightmap", &PhysicsScene::getHeightmap, &PhysicsScene::setHeightmap, IPropertyDescriptor::FILE, "Image (*.raw)"));
	engine.getWorldEditor()->registerProperty("physical_heightfield", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)("xz_scale", &PhysicsScene::getHeightmapXZScale, &PhysicsScene::setHeightmapXZScale));
	engine.getWorldEditor()->registerProperty("physical_heightfield", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)("y_scale", &PhysicsScene::getHeightmapYScale, &PhysicsScene::setHeightmapYScale));

	m_impl = LUMIX_NEW(PhysicsSystemImpl);
	m_impl->m_allocator = LUMIX_NEW(AssertNullAllocator)();
	m_impl->m_error_callback = LUMIX_NEW(CustomErrorCallback)();
	m_impl->m_engine = &engine;
	m_impl->m_foundation = PxCreateFoundation(
		PX_PHYSICS_VERSION,
		*m_impl->m_allocator,
		*m_impl->m_error_callback
	);

	m_impl->m_physics = PxCreatePhysics(
		PX_PHYSICS_VERSION,
		*m_impl->m_foundation,
		physx::PxTolerancesScale()
	);
	
	m_impl->m_controller_manager = PxCreateControllerManager(*m_impl->m_foundation);
	m_impl->m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_impl->m_foundation, physx::PxCookingParams());
	m_impl->connect2VisualDebugger();
	return true;
}


void PhysicsSystem::destroy()
{
	m_impl->m_controller_manager->release();
	m_impl->m_cooking->release();
	m_impl->m_physics->release();
	m_impl->m_foundation->release();
	LUMIX_DELETE(m_impl->m_allocator);
	LUMIX_DELETE(m_impl->m_error_callback);
	LUMIX_DELETE(m_impl);
	m_impl = 0;
}


bool PhysicsSystemImpl::connect2VisualDebugger()
{
	if(m_physics->getPvdConnectionManager() == NULL)
		return false;

	const char* pvd_host_ip = "127.0.0.1";
	int port = 5425;
	unsigned int timeout = 100; 
	physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

	PVD::PvdConnection* theConnection = physx::PxVisualDebuggerExt::createConnection(m_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
	return theConnection != NULL;
}


void CustomErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
{
	g_log_error.log("PhysX") << message;
}


} // !namespace Lumix



