#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "engine/base_proxy_allocator.h"
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


	static const ResourceType TEXTURE_TYPE("texture");
	static const ResourceType PHYSICS_TYPE("physics");


	class D6MotionDescriptor : public IEnumPropertyDescriptor
	{
	public:
		typedef PhysicsScene::D6Motion (PhysicsScene::*Getter)(ComponentHandle);
		typedef void (PhysicsScene::*Setter)(ComponentHandle, PhysicsScene::D6Motion);
		typedef PhysicsScene::D6Motion(PhysicsScene::*ArrayGetter)(ComponentHandle, int);
		typedef void (PhysicsScene::*ArraySetter)(ComponentHandle, int, PhysicsScene::D6Motion);

	public:
		D6MotionDescriptor(const char* name, Getter _getter, Setter _setter)
		{
			setName(name);
			m_single.getter = _getter;
			m_single.setter = _setter;
			m_type = ENUM;
		}


		D6MotionDescriptor(const char* name, ArrayGetter _getter, ArraySetter _setter)
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
				(static_cast<PhysicsScene*>(cmp.scene)->*m_single.setter)(cmp.handle, (PhysicsScene::D6Motion)value);
			}
			else
			{
				(static_cast<PhysicsScene*>(cmp.scene)->*m_array.setter)(cmp.handle, index, (PhysicsScene::D6Motion)value);
			}
		};


		void get(ComponentUID cmp, int index, OutputBlob& stream) const override
		{
			PhysicsScene::D6Motion value;
			if (index == -1)
			{
				value = (static_cast<PhysicsScene*>(cmp.scene)->*m_single.getter)(cmp.handle);
			}
			else
			{
				value = (static_cast<PhysicsScene*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
			}
			stream.write(&value, sizeof(value));
		};


		int getEnumCount(IScene* scene, ComponentHandle) override
		{
			return 3;
		}


		const char* getEnumItemName(IScene* scene, ComponentHandle, int index) override
		{
			switch ((PhysicsScene::D6Motion)index)
			{
				case PhysicsScene::D6Motion::LOCKED: return "locked";
				case PhysicsScene::D6Motion::LIMITED: return "limited";
				case PhysicsScene::D6Motion::FREE: return "free";
				default: ASSERT(false); return "Unknown";
			}
		}


		void getEnumItemName(IScene* scene, ComponentHandle, int index, char* buf, int max_size) override {}

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


	class PhysicsLayerPropertyDescriptor : public IEnumPropertyDescriptor
	{
	public:
		typedef int (PhysicsScene::*Getter)(ComponentHandle);
		typedef void (PhysicsScene::*Setter)(ComponentHandle, int);
		typedef int (PhysicsScene::*ArrayGetter)(ComponentHandle, int);
		typedef void (PhysicsScene::*ArraySetter)(ComponentHandle, int, int);

	public:
		PhysicsLayerPropertyDescriptor(const char* name, Getter _getter, Setter _setter)
		{
			setName(name);
			m_single.getter = _getter;
			m_single.setter = _setter;
			m_type = ENUM;
		}


		PhysicsLayerPropertyDescriptor(const char* name, ArrayGetter _getter, ArraySetter _setter)
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
				(static_cast<PhysicsScene*>(cmp.scene)->*m_single.setter)(cmp.handle, value);
			}
			else
			{
				(static_cast<PhysicsScene*>(cmp.scene)->*m_array.setter)(cmp.handle, index, value);
			}
		};


		void get(ComponentUID cmp, int index, OutputBlob& stream) const override
		{
			int value;
			if (index == -1)
			{
				value = (static_cast<PhysicsScene*>(cmp.scene)->*m_single.getter)(cmp.handle);
			}
			else
			{
				value = (static_cast<PhysicsScene*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
			}
			stream.write(&value, sizeof(value));
		};


		int getEnumCount(IScene* scene, ComponentHandle) override
		{
			return static_cast<PhysicsScene*>(scene)->getCollisionsLayersCount();
		}


		const char* getEnumItemName(IScene* scene, ComponentHandle, int index) override
		{
			auto* phy_scene = static_cast<PhysicsScene*>(scene);
			return phy_scene->getCollisionLayerName(index);
		}


		void getEnumItemName(IScene* scene, ComponentHandle, int index, char* buf, int max_size) override {}

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
		PropertyRegister::add("ragdoll",
			LUMIX_NEW(allocator, BlobPropertyDescriptor<PhysicsScene>)(
				"data", &PhysicsScene::getRagdollData, &PhysicsScene::setRagdollData));
		PropertyRegister::add("ragdoll",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)(
				"Layer", &PhysicsScene::getRagdollLayer, &PhysicsScene::setRagdollLayer));

		PropertyRegister::add("sphere_rigid_actor",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)(
				"Radius", &PhysicsScene::getSphereRadius, &PhysicsScene::setSphereRadius, 0.0f, FLT_MAX, 0.0f));
		PropertyRegister::add("sphere_rigid_actor",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
				"Dynamic", &PhysicsScene::isDynamic, &PhysicsScene::setIsDynamic));

		PropertyRegister::add("capsule_rigid_actor",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)(
				"Radius", &PhysicsScene::getCapsuleRadius, &PhysicsScene::setCapsuleRadius, 0.0f, FLT_MAX, 0.0f));
		PropertyRegister::add("capsule_rigid_actor",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)(
				"Height", &PhysicsScene::getCapsuleHeight, &PhysicsScene::setCapsuleHeight, 0.0f, FLT_MAX, 0.0f));
		PropertyRegister::add("capsule_rigid_actor",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
				"Dynamic", &PhysicsScene::isDynamic, &PhysicsScene::setIsDynamic));

		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, EntityPropertyDescriptor<PhysicsScene>)(
				"Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Axis position", &PhysicsScene::getJointAxisPosition, &PhysicsScene::setJointAxisPosition));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Axis direction", &PhysicsScene::getJointAxisDirection, &PhysicsScene::setJointAxisDirection));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, D6MotionDescriptor)(
				"X motion", &PhysicsScene::getD6JointXMotion, &PhysicsScene::setD6JointXMotion));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, D6MotionDescriptor)(
				"Y motion", &PhysicsScene::getD6JointYMotion, &PhysicsScene::setD6JointYMotion));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, D6MotionDescriptor)(
				"Z motion", &PhysicsScene::getD6JointZMotion, &PhysicsScene::setD6JointZMotion));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, D6MotionDescriptor)(
				"Swing 1", &PhysicsScene::getD6JointSwing1Motion, &PhysicsScene::setD6JointSwing1Motion));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, D6MotionDescriptor)(
				"Swing 2", &PhysicsScene::getD6JointSwing2Motion, &PhysicsScene::setD6JointSwing2Motion));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, D6MotionDescriptor)(
				"Twist", &PhysicsScene::getD6JointTwistMotion, &PhysicsScene::setD6JointTwistMotion));
		PropertyRegister::add("d6_joint",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Linear limit",
				&PhysicsScene::getD6JointLinearLimit,
				&PhysicsScene::setD6JointLinearLimit,
				0.0f,
				FLT_MAX,
				0.0f));
		auto* d6_swing_limit_prop = LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, PhysicsScene>)(
			"Swing limit", &PhysicsScene::getD6JointSwingLimit, &PhysicsScene::setD6JointSwingLimit);
		d6_swing_limit_prop->setIsInRadians(true);
		PropertyRegister::add("d6_joint", d6_swing_limit_prop);
		auto* d6_twist_limit_prop = LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, PhysicsScene>)(
			"Twist limit", &PhysicsScene::getD6JointTwistLimit, &PhysicsScene::setD6JointTwistLimit);
		d6_twist_limit_prop->setIsInRadians(true);
		PropertyRegister::add("d6_joint", d6_twist_limit_prop);

		PropertyRegister::add("spherical_joint",
			LUMIX_NEW(allocator, EntityPropertyDescriptor<PhysicsScene>)(
				"Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody));
		PropertyRegister::add("spherical_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Axis position", &PhysicsScene::getJointAxisPosition, &PhysicsScene::setJointAxisPosition));
		PropertyRegister::add("spherical_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Axis direction", &PhysicsScene::getJointAxisDirection, &PhysicsScene::setJointAxisDirection));
		PropertyRegister::add("spherical_joint",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
				"Use limit", &PhysicsScene::getSphericalJointUseLimit, &PhysicsScene::setSphericalJointUseLimit));
		PropertyRegister::add("spherical_joint",
			&(LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, PhysicsScene>)(
				  "Limit", &PhysicsScene::getSphericalJointLimit, &PhysicsScene::setSphericalJointLimit))
				 ->setIsInRadians(true));

		PropertyRegister::add("distance_joint",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Damping",
				&PhysicsScene::getDistanceJointDamping,
				&PhysicsScene::setDistanceJointDamping,
				0.0f,
				FLT_MAX,
				0.0f));
		PropertyRegister::add("distance_joint",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Stiffness",
				&PhysicsScene::getDistanceJointStiffness,
				&PhysicsScene::setDistanceJointStiffness,
				0.0f,
				FLT_MAX,
				0.0f));
		PropertyRegister::add("distance_joint",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Tolerance",
				&PhysicsScene::getDistanceJointTolerance,
				&PhysicsScene::setDistanceJointTolerance,
				0.0f,
				FLT_MAX,
				0.0f));
		PropertyRegister::add("distance_joint",
			LUMIX_NEW(allocator, EntityPropertyDescriptor<PhysicsScene>)(
				"Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody));
		PropertyRegister::add("distance_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, PhysicsScene>)(
				"Limits", &PhysicsScene::getDistanceJointLimits, &PhysicsScene::setDistanceJointLimits));

		PropertyRegister::add("hinge_joint",
			LUMIX_NEW(allocator, EntityPropertyDescriptor<PhysicsScene>)(
				"Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody));
		PropertyRegister::add("hinge_joint",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Damping",
				&PhysicsScene::getHingeJointDamping,
				&PhysicsScene::setHingeJointDamping,
				0.0f,
				FLT_MAX,
				0.0f));
		PropertyRegister::add("hinge_joint",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("Stiffness",
				&PhysicsScene::getHingeJointStiffness,
				&PhysicsScene::setHingeJointStiffness,
				0.0f,
				FLT_MAX,
				0.0f));
		PropertyRegister::add("hinge_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Axis position", &PhysicsScene::getJointAxisPosition, &PhysicsScene::setJointAxisPosition));
		PropertyRegister::add("hinge_joint",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Axis direction", &PhysicsScene::getJointAxisDirection, &PhysicsScene::setJointAxisDirection));
		PropertyRegister::add("hinge_joint",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
				"Use limit", &PhysicsScene::getHingeJointUseLimit, &PhysicsScene::setHingeJointUseLimit));
		PropertyRegister::add("hinge_joint",
			&(LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, PhysicsScene>)(
				  "Limit", &PhysicsScene::getHingeJointLimit, &PhysicsScene::setHingeJointLimit))
				 ->setIsInRadians(true));

		PropertyRegister::add("physical_controller",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)(
				"Layer", &PhysicsScene::getControllerLayer, &PhysicsScene::setControllerLayer));

		PropertyRegister::add("box_rigid_actor",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
				"Dynamic", &PhysicsScene::isDynamic, &PhysicsScene::setIsDynamic));
		PropertyRegister::add("box_rigid_actor",
			LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)(
				"Size", &PhysicsScene::getHalfExtents, &PhysicsScene::setHalfExtents));
		PropertyRegister::add("box_rigid_actor",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)(
				"Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer));
		PropertyRegister::add("mesh_rigid_actor",
			LUMIX_NEW(allocator, ResourcePropertyDescriptor<PhysicsScene>)("Source",
				&PhysicsScene::getShapeSource,
				&PhysicsScene::setShapeSource,
				"Physics (*.phy)",
				PHYSICS_TYPE));
		PropertyRegister::add("mesh_rigid_actor",
			LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)(
				"Dynamic", &PhysicsScene::isDynamic, &PhysicsScene::setIsDynamic));
		PropertyRegister::add("mesh_rigid_actor",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)(
				"Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, ResourcePropertyDescriptor<PhysicsScene>)(
				"Heightmap", &PhysicsScene::getHeightmap, &PhysicsScene::setHeightmap, "Image (*.raw)", TEXTURE_TYPE));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("XZ scale",
				&PhysicsScene::getHeightmapXZScale,
				&PhysicsScene::setHeightmapXZScale,
				0.0f,
				FLT_MAX,
				0.0f));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)(
				"Y scale", &PhysicsScene::getHeightmapYScale, &PhysicsScene::setHeightmapYScale, 0.0f, FLT_MAX, 0.0f));
		PropertyRegister::add("physical_heightfield",
			LUMIX_NEW(allocator, PhysicsLayerPropertyDescriptor)(
				"Layer", &PhysicsScene::getHeightfieldLayer, &PhysicsScene::setHeightfieldLayer));
	}


	struct CustomErrorCallback : public physx::PxErrorCallback
	{
		void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
		{
			g_log_error.log("Physics") << message;
		}
	};


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
			void deallocate(void* ptr) override { _aligned_free(ptr); }
		#else
			void* allocate(size_t size, const char*, const char*, int) override
			{
				void* ret = aligned_alloc(16, size);
				// g_log_info.log("Physics") << "Allocated " << size << " bytes for " << typeName << "
				// from " << filename << "(" << line << ")";
				ASSERT(ret);
				return ret;
			}
			void deallocate(void* ptr) override { free(ptr); }
		#endif
	};


	struct PhysicsSystemImpl LUMIX_FINAL : public PhysicsSystem
	{
		explicit PhysicsSystemImpl(Engine& engine)
			: m_allocator(engine.getAllocator())
			, m_engine(engine)
			, m_manager(*this, engine.getAllocator())
		{
			registerProperties(engine.getAllocator());
			m_manager.create(PHYSICS_TYPE, engine.getResourceManager());
			PhysicsScene::registerLuaAPI(m_engine.getState());

			m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_physx_allocator, m_error_callback);

			m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale());

			physx::PxTolerancesScale scale;
			m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_foundation, physx::PxCookingParams(scale));
			connect2VisualDebugger();
		}


		~PhysicsSystemImpl()
		{
			m_cooking->release();
			m_physics->release();
			m_foundation->release();
		}


		void createScenes(Universe& universe) override
		{
			auto* scene = PhysicsScene::create(*this, universe, m_engine, m_allocator);
			universe.addScene(scene);
		}


		void destroyScene(IScene* scene) override { PhysicsScene::destroy(static_cast<PhysicsScene*>(scene)); }


		physx::PxPhysics* getPhysics() override
		{
			return m_physics;
		}

		physx::PxCooking* getCooking() override
		{
			return m_cooking;
		}

		bool connect2VisualDebugger()
		{
			if (m_physics->getPvdConnectionManager() == nullptr) return false;

			const char* pvd_host_ip = "127.0.0.1";
			int port = 5425;
			unsigned int timeout = 100;
			physx::PxVisualDebuggerConnectionFlags connectionFlags =
				physx::PxVisualDebuggerExt::getAllConnectionFlags();

			auto* connection = physx::PxVisualDebuggerExt::createConnection(
				m_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
			return connection != nullptr;
		}

		physx::PxPhysics* m_physics;
		physx::PxFoundation* m_foundation;
		physx::PxControllerManager* m_controller_manager;
		AssertNullAllocator m_physx_allocator;
		CustomErrorCallback m_error_callback;
		physx::PxCooking* m_cooking;
		PhysicsGeometryManager m_manager;
		Engine& m_engine;
		BaseProxyAllocator m_allocator;
	};


	LUMIX_PLUGIN_ENTRY(physics)
	{
		return LUMIX_NEW(engine.getAllocator(), PhysicsSystemImpl)(engine);
	}


} // namespace Lumix



