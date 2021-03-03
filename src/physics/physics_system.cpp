#include "physics/physics_system.h"

#include <foundation/PxAllocatorCallback.h>
#include <foundation/PxErrorCallback.h>
#include <pvd/PxPvd.h>
#include <pvd/PxPvdTransport.h>
#include <PxFoundation.h>
#include <PxPhysics.h>
#include <PxPhysicsVersion.h>
#include <vehicle/PxVehicleSDK.h>

#include "cooking/PxCooking.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "physics/physics_geometry.h"
#include "physics/physics_scene.h"
#include "renderer/texture.h"


namespace Lumix
{
	struct CustomErrorCallback : physx::PxErrorCallback
	{
		void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
		{
			logError(message);
		}
	};


	struct AssertNullAllocator : physx::PxAllocatorCallback
	{
		AssertNullAllocator(IAllocator& source)
			: source(source)
		{}

		void* allocate(size_t size, const char*, const char*, int) override
		{
			void* ret = source.allocate_aligned(size, 16);
			// logInfo("Physics") << "Allocated " << size << " bytes for " << typeName << "
			// from " << filename << "(" << line << ")";
			ASSERT(ret);
			return ret;
		}
		
		void deallocate(void* ptr) override { source.deallocate_aligned(ptr); }

		IAllocator& source;
	};


	struct PhysicsGeometryManager final : ResourceManager
	{
		PhysicsGeometryManager(PhysicsSystem& system, IAllocator& allocator)
			: ResourceManager(allocator)
			, m_allocator(allocator)
			, m_system(system)
		{}

		Resource* createResource(const Path& path) override {
			return LUMIX_NEW(m_allocator, PhysicsGeometry)(path, *this, m_system, m_allocator);
		}

		void destroyResource(Resource& resource) override {
			LUMIX_DELETE(m_allocator, static_cast<PhysicsGeometry*>(&resource));
		}

		IAllocator& m_allocator;
		PhysicsSystem& m_system;
	};

	static int LUA_raycast(lua_State* L)
	{
		auto* scene = LuaWrapper::checkArg<PhysicsScene*>(L, 1);
		Vec3 origin = LuaWrapper::checkArg<Vec3>(L, 2);
		Vec3 dir = LuaWrapper::checkArg<Vec3>(L, 3);
		const int layer = lua_gettop(L) > 3 ? LuaWrapper::checkArg<int>(L, 4) : -1;
		RaycastHit hit;
		if (scene->raycastEx(origin, dir, FLT_MAX, hit, INVALID_ENTITY, layer))
		{
			LuaWrapper::push(L, hit.entity != INVALID_ENTITY);
			LuaWrapper::pushEntity(L, hit.entity, &scene->getUniverse());
			LuaWrapper::push(L, hit.position);
			LuaWrapper::push(L, hit.normal);
			return 4;
		}
		LuaWrapper::push(L, false);
		return 1;
	}

	struct PhysicsSystemImpl final : PhysicsSystem
	{
		explicit PhysicsSystemImpl(Engine& engine)
			: m_allocator(engine.getAllocator())
			, m_engine(engine)
			, m_manager(*this, engine.getAllocator())
			, m_physx_allocator(m_allocator)
		{
			PhysicsScene::reflect();
			m_layers.count = 2;
			memset(m_layers.names, 0, sizeof(m_layers.names));
			for (u32 i = 0; i < lengthOf(m_layers.names); ++i)
			{
				copyString(m_layers.names[i], "Layer");
				char tmp[3];
				toCString(i, Span(tmp));
				catString(m_layers.names[i], tmp);
				m_layers.filter[i] = 1 << i;
			}
			
			m_manager.create(PhysicsGeometry::TYPE, engine.getResourceManager());
			LuaWrapper::createSystemFunction(engine.getState(), "Physics", "raycast", &LUA_raycast);

			m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_physx_allocator, m_error_callback);

			#ifdef LUMIX_DEBUG
				if (connect2VisualDebugger()) {
					logInfo("PhysX debugger connected");
				}
			#endif

			m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(), false, m_pvd);
			LUMIX_FATAL(m_physics);

			physx::PxTolerancesScale scale;
			m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_foundation, physx::PxCookingParams(scale));

			if (!PxInitVehicleSDK(*m_physics)) {
				LUMIX_FATAL(false);
			}
			physx::PxVehicleSetBasisVectors(physx::PxVec3(0, 1, 0), physx::PxVec3(0, 0, -1));
			physx::PxVehicleSetUpdateMode(physx::PxVehicleUpdateMode::eVELOCITY_CHANGE);
		}


		~PhysicsSystemImpl()
		{
			physx::PxCloseVehicleSDK();
			m_cooking->release();
			m_physics->release();
			if (m_pvd) {
				m_pvd->disconnect();
				m_pvd->release();
				m_pvd_transport->release();
			}
			m_foundation->release();
		}

		u32 getVersion() const override { return 0; }

		void serialize(OutputMemoryStream& serializer) const override {
			serializer.write(m_layers.count);
			serializer.write(m_layers.names);
			serializer.write(m_layers.filter);
		}

		bool deserialize(u32 version, InputMemoryStream& serializer) override {
			if (version != 0) return false;

			serializer.read(m_layers.count);
			serializer.read(m_layers.names);
			serializer.read(m_layers.filter);
			return true;
		}

		void createScenes(Universe& universe) override
		{
			UniquePtr<PhysicsScene> scene = PhysicsScene::create(*this, universe, m_engine, m_allocator);
			universe.addScene(scene.move());
		}

		physx::PxPhysics* getPhysics() override
		{
			return m_physics;
		}

		physx::PxCooking* getCooking() override
		{
			return m_cooking;
		}
	
		CollisionLayers& getCollisionLayers() override { return m_layers; }

		bool connect2VisualDebugger()
		{
			m_pvd = PxCreatePvd(*m_foundation);
			if (!m_pvd) return false;
			
			//physx::PxPvdTransport* transport = physx::PxDefaultPvdFileTransportCreate("physx.pvd");
			m_pvd_transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 100);
			return m_pvd->connect(*m_pvd_transport, physx::PxPvdInstrumentationFlag::eALL);
		}


		int getCollisionsLayersCount() const override { return m_layers.count; }
		void addCollisionLayer() override { m_layers.count = minimum(lengthOf(m_layers.names), m_layers.count + 1); }
		void removeCollisionLayer() override { m_layers.count = maximum(0, m_layers.count - 1); }
		void setCollisionLayerName(int index, const char* name) override { copyString(m_layers.names[index], name); }
		const char* getCollisionLayerName(int index) override { return m_layers.names[index]; }
		bool canLayersCollide(int layer1, int layer2) override { return (m_layers.filter[layer1] & (1 << layer2)) != 0; }

		void setLayersCanCollide(int layer1, int layer2, bool can_collide) override {
			if (can_collide) {
				m_layers.filter[layer1] |= 1 << layer2;
				m_layers.filter[layer2] |= 1 << layer1;
			}
			else {
				m_layers.filter[layer1] &= ~(1 << layer2);
				m_layers.filter[layer2] &= ~(1 << layer1);
			}
		}


		IAllocator& m_allocator;
		physx::PxPhysics* m_physics;
		physx::PxFoundation* m_foundation;
		physx::PxControllerManager* m_controller_manager;
		AssertNullAllocator m_physx_allocator;
		CustomErrorCallback m_error_callback;
		physx::PxCooking* m_cooking;
		PhysicsGeometryManager m_manager;
		Engine& m_engine;
		CollisionLayers m_layers;
		physx::PxPvd* m_pvd = nullptr;
		physx::PxPvdTransport* m_pvd_transport = nullptr;
	};


	LUMIX_PLUGIN_ENTRY(physics)
	{
		return LUMIX_NEW(engine.getAllocator(), PhysicsSystemImpl)(engine);
	}


} // namespace Lumix



