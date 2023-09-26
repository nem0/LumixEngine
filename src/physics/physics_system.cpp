#include "physics/physics_system.h"

#include <cooking/PxCooking.h>
#include <foundation/PxAllocatorCallback.h>
#include <foundation/PxErrorCallback.h>
#include <foundation/PxIO.h>
#include <pvd/PxPvd.h>
#include <pvd/PxPvdTransport.h>
#include <PxFoundation.h>
#include <PxMaterial.h>
#include <PxPhysics.h>
#include <PxPhysicsVersion.h>
#include <vehicle/PxVehicleSDK.h>

#include "engine/engine.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/world.h"
#include "physics/physics_resources.h"
#include "physics/physics_module.h"
#include "renderer/texture.h"


namespace Lumix
{

	struct OutputStream final : physx::PxOutputStream {
		explicit OutputStream(IOutputStream& blob)
			: blob(blob) {}

		physx::PxU32 write(const void* src, physx::PxU32 count) override {
			blob.write(src, count);
			return count;
		}

		IOutputStream& blob;
	};

	struct CustomErrorCallback : physx::PxErrorCallback
	{
		void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
		{
			logError(message);
		}
	};


	struct PhysxAllocator : physx::PxAllocatorCallback {
		PhysxAllocator(IAllocator& source)
			: source(source)
		{}

		void* allocate(size_t size, const char*, const char*, int) override {
			void* ret = source.allocate(size, 16);
			// logInfo("Physics") << "Allocated " << size << " bytes for " << typeName << "
			// from " << filename << "(" << line << ")";
			//ASSERT(ret);
			return ret;
		}
		
		void deallocate(void* ptr) override { source.deallocate(ptr); }

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
		auto* module = LuaWrapper::checkArg<PhysicsModule*>(L, 1);
		Vec3 origin = LuaWrapper::checkArg<Vec3>(L, 2);
		Vec3 dir = LuaWrapper::checkArg<Vec3>(L, 3);
		const int layer = lua_gettop(L) > 3 ? LuaWrapper::checkArg<int>(L, 4) : -1;
		RaycastHit hit;
		if (module->raycastEx(origin, dir, FLT_MAX, hit, INVALID_ENTITY, layer))
		{
			LuaWrapper::push(L, hit.entity != INVALID_ENTITY);
			LuaWrapper::pushEntity(L, hit.entity, &module->getWorld());
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
			: m_allocator(engine.getAllocator(), "physics")
			, m_engine(engine)
			, m_geometry_manager(*this, m_allocator)
			, m_material_manager(*this, m_allocator)
			, m_physx_allocator(m_allocator)
		{
			PhysicsModule::reflect();
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
			
			m_material_manager.create(PhysicsMaterial::TYPE, engine.getResourceManager());
			m_geometry_manager.create(PhysicsGeometry::TYPE, engine.getResourceManager());
			LuaWrapper::createSystemFunction(engine.getState(), "Physics", "raycast", &LUA_raycast);

			m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_physx_allocator, m_error_callback);

			#ifdef LUMIX_DEBUG
				if (connect2VisualDebugger()) {
					logInfo("PhysX debugger connected");
				}
			#endif

			m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(), false, m_pvd);
			ASSERT(m_physics);

			if (!PxInitVehicleSDK(*m_physics)) {
				ASSERT(false);
			}
			physx::PxVehicleSetBasisVectors(physx::PxVec3(0, 1, 0), physx::PxVec3(0, 0, -1));
			physx::PxVehicleSetUpdateMode(physx::PxVehicleUpdateMode::eVELOCITY_CHANGE);
		}


		~PhysicsSystemImpl()
		{
			m_material_manager.destroy();
			m_geometry_manager.destroy();
			physx::PxCloseVehicleSDK();
			m_physics->release();
			if (m_pvd) {
				m_pvd->disconnect();
				m_pvd->release();
				m_pvd_transport->release();
			}
			m_foundation->release();
		}

		void serialize(OutputMemoryStream& serializer) const override {
			serializer.write(m_layers.count);
			serializer.write(m_layers.names);
			serializer.write(m_layers.filter);
		}

		bool deserialize(i32 version, InputMemoryStream& serializer) override {
			if (version != 0) return false;

			serializer.read(m_layers.count);
			serializer.read(m_layers.names);
			serializer.read(m_layers.filter);
			return true;
		}

		void createModules(World& world) override {
			UniquePtr<PhysicsModule> module = PhysicsModule::create(*this, world, m_engine, m_allocator);
			world.addModule(module.move());
		}

		physx::PxPhysics* getPhysics() override { return m_physics; }
		CollisionLayers& getCollisionLayers() override { return m_layers; }

		bool connect2VisualDebugger()
		{
			m_pvd = PxCreatePvd(*m_foundation);
			if (!m_pvd) return false;
			
			//physx::PxPvdTransport* transport = physx::PxDefaultPvdFileTransportCreate("physx.pvd");
			m_pvd_transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 100);
			bool connected = m_pvd->connect(*m_pvd_transport, physx::PxPvdInstrumentationFlag::eALL);
			if (!connected) {
				m_pvd_transport->release();
				m_pvd->release();
				m_pvd = nullptr;
			}
			return connected;
		}

		bool cookTriMesh(Span<const Vec3> verts, Span<const u32> indices, IOutputStream& blob) override {

			physx::PxTriangleMeshDesc mesh_desc;
			mesh_desc.points.count = verts.length();
			mesh_desc.points.stride = sizeof(physx::PxVec3);
			mesh_desc.points.data = verts.begin();

			mesh_desc.triangles.count = indices.length() / 3;
			mesh_desc.triangles.stride = 3 * sizeof(physx::PxU32);
			mesh_desc.triangles.data = indices.begin();

			OutputStream writeBuffer(blob);
			physx::PxCookingParams params{physx::PxTolerancesScale()};
			return PxCookTriangleMesh(params, mesh_desc, writeBuffer);
		}

		bool cookConvex(Span<const Vec3> verts, IOutputStream& blob) override {
			physx::PxConvexMeshDesc mesh_desc;
			mesh_desc.points.count = verts.length();
			mesh_desc.points.stride = sizeof(Vec3);
			mesh_desc.points.data = verts.begin();
			mesh_desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

			OutputStream writeBuffer(blob);
			physx::PxCookingParams params{physx::PxTolerancesScale()};
			return PxCookConvexMesh(params, mesh_desc, writeBuffer);
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


		TagAllocator m_allocator;
		physx::PxPhysics* m_physics;
		physx::PxFoundation* m_foundation;
		physx::PxControllerManager* m_controller_manager;
		PhysxAllocator m_physx_allocator;
		CustomErrorCallback m_error_callback;
		PhysicsGeometryManager m_geometry_manager;
		PhysicsMaterialManager m_material_manager;
		Engine& m_engine;
		CollisionLayers m_layers;
		physx::PxPvd* m_pvd = nullptr;
		physx::PxPvdTransport* m_pvd_transport = nullptr;
	};

	LUMIX_PLUGIN_ENTRY(physics) {
		PROFILE_FUNCTION();
		return LUMIX_NEW(engine.getAllocator(), PhysicsSystemImpl)(engine);
	}


} // namespace Lumix



