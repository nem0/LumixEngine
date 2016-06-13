#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "engine/base_proxy_allocator.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/engine.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"
#include "engine/universe/universe.h"
#include <cstdlib>


namespace Lumix
{


	static const uint32 BOX_ACTOR_HASH = crc32("box_rigid_actor");
	static const uint32 MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
	static const uint32 CONTROLLER_HASH = crc32("physical_controller");
	static const uint32 HEIGHTFIELD_HASH = crc32("physical_heightfield");
	static const uint32 TEXTURE_HASH = crc32("TEXTURE");
	static const uint32 PHYSICS_HASH = crc32("PHYSICS");


	class PhysicsLayerPropertyDescriptor : public IEnumPropertyDescriptor
	{
	public:
		typedef int (PhysicsScene::*Getter)(ComponentIndex);
		typedef void (PhysicsScene::*Setter)(ComponentIndex, int);
		typedef int (PhysicsScene::*ArrayGetter)(ComponentIndex, int);
		typedef void (PhysicsScene::*ArraySetter)(ComponentIndex, int, int);

	public:
		PhysicsLayerPropertyDescriptor(const char* name,
			Getter _getter,
			Setter _setter,
			IAllocator& allocator)
			: IEnumPropertyDescriptor(allocator)
		{
			setName(name);
			m_single.getter = _getter;
			m_single.setter = _setter;
			m_type = ENUM;
		}


		PhysicsLayerPropertyDescriptor(const char* name,
			ArrayGetter _getter,
			ArraySetter _setter,
			IAllocator& allocator)
			: IEnumPropertyDescriptor(allocator)
		{
			setName(name);
			m_array.getter = _getter;
			m_array.setter = _setter;
			m_type = ENUM;
		}


		void set(ComponentUID cmp, int index, InputBlob& stream) const override
		{
			int value;
			stream.read(&value, sizeof(value));
			if (index == -1)
			{
				(static_cast<PhysicsScene*>(cmp.scene)->*m_single.setter)(cmp.index, value);
			}
			else
			{
				(static_cast<PhysicsScene*>(cmp.scene)->*m_array.setter)(cmp.index, index, value);
			}
		};


		void get(ComponentUID cmp, int index, OutputBlob& stream) const override
		{
			int value;
			if (index == -1)
			{
				value = (static_cast<PhysicsScene*>(cmp.scene)->*m_single.getter)(cmp.index);
			}
			else
			{
				value = (static_cast<PhysicsScene*>(cmp.scene)->*m_array.getter)(cmp.index, index);
			}
			stream.write(&value, sizeof(value));
		};


		int getEnumCount(IScene* scene, ComponentIndex) override
		{
			return static_cast<PhysicsScene*>(scene)->getCollisionsLayersCount();
		}


		const char* getEnumItemName(IScene* scene, ComponentIndex, int index) override
		{
			auto* phy_scene = static_cast<PhysicsScene*>(scene);
			return phy_scene->getCollisionLayerName(index);
		}


		void getEnumItemName(IScene* scene, ComponentIndex, int index, char* buf, int max_size) override {}

	private:
		union
		{
			struct
			{
				Getter getter;
				Setter setter;
			} m_single;
			struct
			{
				ArrayGetter getter;
				ArraySetter setter;
			} m_array;
		};
	};


	static void registerProperties(Lumix::IAllocator& allocator)
	{
		PropertyRegister::registerComponentType("box_rigid_actor");
		PropertyRegister::registerComponentType("physical_controller");
		PropertyRegister::registerComponentType("mesh_rigid_actor");
		PropertyRegister::registerComponentType("physical_heightfield");

		PropertyRegister::add("physical_controller",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)("Layer",
			&PhysicsScene::getControllerLayer,
			&PhysicsScene::setControllerLayer,
			allocator));

		PropertyRegister::add("box_rigid_actor",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)("Dynamic",
			&PhysicsScene::isDynamic,
			&PhysicsScene::setIsDynamic,
			allocator));
		PropertyRegister::add("box_rigid_actor",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)("Size",
			&PhysicsScene::getHalfExtents,
			&PhysicsScene::setHalfExtents,
			allocator));
		PropertyRegister::add("box_rigid_actor",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)("Layer",
			&PhysicsScene::getActorLayer,
			&PhysicsScene::setActorLayer,
			allocator));
		PropertyRegister::add("mesh_rigid_actor",
			LUMIX_NEW(allocator, FilePropertyDescriptor<PhysicsScene>)("Source",
			&PhysicsScene::getShapeSource,
			&PhysicsScene::setShapeSource,
			"Physics (*.pda)",
			allocator));
		PropertyRegister::add("mesh_rigid_actor",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)("Layer",
			&PhysicsScene::getActorLayer,
			&PhysicsScene::setActorLayer,
			allocator));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, ResourcePropertyDescriptor<PhysicsScene>)("Heightmap",
			&PhysicsScene::getHeightmap,
			&PhysicsScene::setHeightmap,
			"Image (*.raw)",
			TEXTURE_HASH,
			allocator));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("XZ scale",
			&PhysicsScene::getHeightmapXZScale,
			&PhysicsScene::setHeightmapXZScale,
			0.0f,
			FLT_MAX,
			0.0f,
			allocator));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Y scale",
			&PhysicsScene::getHeightmapYScale,
			&PhysicsScene::setHeightmapYScale,
			0.0f,
			FLT_MAX,
			0.0f,
			allocator));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)("Layer",
			&PhysicsScene::getHeightfieldLayer,
			&PhysicsScene::setHeightfieldLayer,
			allocator));
	}


	struct PhysicsSystemImpl : public PhysicsSystem
	{
		explicit PhysicsSystemImpl(Engine& engine)
			: m_allocator(engine.getAllocator())
			, m_engine(engine)
			, m_manager(*this, engine.getAllocator())
		{
			registerProperties(engine.getAllocator());
			m_manager.create(PHYSICS_HASH, engine.getResourceManager());
			PhysicsScene::registerLuaAPI(m_engine.getState());
		}

		bool create() override;
		IScene* createScene(Universe& universe) override;
		void destroyScene(IScene* scene) override;
		void destroy() override;

		physx::PxPhysics* getPhysics() override
		{
			return m_physics;
		}

		physx::PxCooking* getCooking() override
		{
			return m_cooking;
		}

		bool connect2VisualDebugger();

		physx::PxPhysics* m_physics;
		physx::PxFoundation* m_foundation;
		physx::PxControllerManager* m_controller_manager;
		physx::PxAllocatorCallback* m_physx_allocator;
		physx::PxErrorCallback* m_error_callback;
		physx::PxCooking* m_cooking;
		PhysicsGeometryManager m_manager;
		Engine& m_engine;
		BaseProxyAllocator m_allocator;
	};


	LUMIX_PLUGIN_ENTRY(physics)
	{
		return LUMIX_NEW(engine.getAllocator(), PhysicsSystemImpl)(engine);
	}

	
	struct CustomErrorCallback : public physx::PxErrorCallback
	{
		virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
	};


	IScene* PhysicsSystemImpl::createScene(Universe& ctx)
	{
		return PhysicsScene::create(*this, ctx, m_engine, m_allocator);
	}


	void PhysicsSystemImpl::destroyScene(IScene* scene)
	{
		PhysicsScene::destroy(static_cast<PhysicsScene*>(scene));
	}


	class AssertNullAllocator : public physx::PxAllocatorCallback
	{
	public:
		#ifdef _WIN32
			void* allocate(size_t size, const char*, const char*, int) override
			{
				void* ret = _aligned_malloc(size, 16);
				// g_log_info.log("Physics") << "Allocated " << size << " bytes for " << typeName << "
				// from " << filename << "(" << line << ")";
				ASSERT(ret);
				return ret;
			}
			void deallocate(void* ptr) override
			{
				_aligned_free(ptr);
			}
		#else
			void* allocate(size_t size, const char*, const char*, int) override
			{
				void* ret = aligned_alloc(16, size);
				// g_log_info.log("Physics") << "Allocated " << size << " bytes for " << typeName << "
				// from " << filename << "(" << line << ")";
				ASSERT(ret);
				return ret;
			}
			void deallocate(void* ptr) override
			{
				free(ptr);
			}
		#endif
	};


	bool PhysicsSystemImpl::create()
	{
		m_physx_allocator = LUMIX_NEW(m_allocator, AssertNullAllocator);
		m_error_callback = LUMIX_NEW(m_allocator, CustomErrorCallback);
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

		physx::PxTolerancesScale scale;
		m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_foundation, physx::PxCookingParams(scale));
		connect2VisualDebugger();
		return true;
	}


	void PhysicsSystemImpl::destroy()
	{
		m_cooking->release();
		m_physics->release();
		m_foundation->release();
		LUMIX_DELETE(m_allocator, m_physx_allocator);
		LUMIX_DELETE(m_allocator, m_error_callback);
	}


	bool PhysicsSystemImpl::connect2VisualDebugger()
	{
		if (m_physics->getPvdConnectionManager() == nullptr) return false;

		const char* pvd_host_ip = "127.0.0.1";
		int port = 5425;
		unsigned int timeout = 100;
		physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

		auto* theConnection = physx::PxVisualDebuggerExt::createConnection(
			m_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
		return theConnection != nullptr;
	}


	void CustomErrorCallback::reportError(physx::PxErrorCode::Enum, const char* message, const char*, int)
	{
		g_log_error.log("Physics") << message;
	}


} // namespace Lumix



