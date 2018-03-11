#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "engine/base_proxy_allocator.h"
#include "engine/log.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/universe/universe.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "renderer/texture.h"
#include <cstdlib>


namespace Lumix
{
	static void registerProperties(IAllocator& allocator)
	{
		using namespace Reflection;

		static auto dynamicTypeDesc = enumDesciptor<PhysicsScene::DynamicType>(
			LUMIX_ENUM_VALUE(PhysicsScene::DynamicType::DYNAMIC),
			LUMIX_ENUM_VALUE(PhysicsScene::DynamicType::STATIC),
			LUMIX_ENUM_VALUE(PhysicsScene::DynamicType::KINEMATIC)
		);
		registerEnum(dynamicTypeDesc);

		static auto d6MotionNameDesc = enumDesciptor<PhysicsScene::D6Motion>(
			LUMIX_ENUM_VALUE(PhysicsScene::D6Motion::LOCKED),
			LUMIX_ENUM_VALUE(PhysicsScene::D6Motion::LIMITED),
			LUMIX_ENUM_VALUE(PhysicsScene::D6Motion::FREE)
		);
		registerEnum(d6MotionNameDesc);

		static auto phy_scene = scene("physics",
			functions(
				function(LUMIX_FUNC(PhysicsScene::raycast)),
				function(LUMIX_FUNC(PhysicsScene::raycastEx))
			),
			component("ragdoll",
				blob_property("data", LUMIX_PROP(PhysicsScene, RagdollData)),
				property("Layer", LUMIX_PROP(PhysicsScene, RagdollLayer))
			),
			component("sphere_rigid_actor",
				functions(
					function(LUMIX_FUNC(PhysicsScene::applyForceToActor)),
					function(LUMIX_FUNC(PhysicsScene::applyImpulseToActor)),
					function(LUMIX_FUNC(PhysicsScene::getActorVelocity))
				),
				property("Radius", LUMIX_PROP(PhysicsScene, SphereRadius),
					MinAttribute(0)),
				property("Layer", LUMIX_PROP(PhysicsScene, ActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, DynamicType), dynamicTypeDesc),
				property("Trigger", LUMIX_PROP(PhysicsScene, IsTrigger)) 
			),
			component("capsule_rigid_actor",
				functions(
					function(LUMIX_FUNC(PhysicsScene::applyForceToActor)),
					function(LUMIX_FUNC(PhysicsScene::applyImpulseToActor)),
					function(LUMIX_FUNC(PhysicsScene::getActorVelocity))
				),
				property("Radius", LUMIX_PROP(PhysicsScene, CapsuleRadius),
					MinAttribute(0)),
				property("Height", LUMIX_PROP(PhysicsScene, CapsuleHeight)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, DynamicType), dynamicTypeDesc),
				property("Trigger", LUMIX_PROP(PhysicsScene, IsTrigger))
			),
			component("d6_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, JointAxisDirection)),
				enum_property("X motion", LUMIX_PROP(PhysicsScene, D6JointXMotion), d6MotionNameDesc),
				enum_property("Y motion", LUMIX_PROP(PhysicsScene, D6JointYMotion), d6MotionNameDesc),
				enum_property("Z motion", LUMIX_PROP(PhysicsScene, D6JointZMotion), d6MotionNameDesc),
				enum_property("Swing 1", LUMIX_PROP(PhysicsScene, D6JointSwing1Motion), d6MotionNameDesc),
				enum_property("Swing 2", LUMIX_PROP(PhysicsScene, D6JointSwing2Motion), d6MotionNameDesc),
				enum_property("Twist", LUMIX_PROP(PhysicsScene, D6JointTwistMotion), d6MotionNameDesc),
				property("Linear limit", LUMIX_PROP(PhysicsScene, D6JointLinearLimit),
					MinAttribute(0)),
				property("Swing limit", LUMIX_PROP(PhysicsScene, D6JointSwingLimit),
					RadiansAttribute()),
				property("Twist limit", LUMIX_PROP(PhysicsScene, D6JointTwistLimit),
					RadiansAttribute()),
				property("Damping", LUMIX_PROP(PhysicsScene, D6JointDamping)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, D6JointStiffness)),
				property("Restitution", LUMIX_PROP(PhysicsScene, D6JointRestitution))
			),
			component("spherical_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, JointAxisDirection)),
				property("Use limit", LUMIX_PROP(PhysicsScene, SphericalJointUseLimit)),
				property("Limit", LUMIX_PROP(PhysicsScene, SphericalJointLimit),
					RadiansAttribute())
			),
			component("distance_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Damping", LUMIX_PROP(PhysicsScene, DistanceJointDamping),
					MinAttribute(0)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, DistanceJointStiffness),
					MinAttribute(0)),
				property("Tolerance", LUMIX_PROP(PhysicsScene, DistanceJointTolerance),
					MinAttribute(0)),
				property("Limits", LUMIX_PROP(PhysicsScene, DistanceJointLimits))
			),
			component("hinge_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Damping", LUMIX_PROP(PhysicsScene, HingeJointDamping),
					MinAttribute(0)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, HingeJointStiffness),
					MinAttribute(0)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)), 
				property("Axis direction", LUMIX_PROP(PhysicsScene, JointAxisDirection)),
				property("Use limit", LUMIX_PROP(PhysicsScene, HingeJointUseLimit)),
				property("Limit", LUMIX_PROP(PhysicsScene, HingeJointLimit),
					RadiansAttribute())
			),
			component("physical_controller",
				functions(
					function(LUMIX_FUNC(PhysicsScene::moveController))
				),
				property("Radius", LUMIX_PROP(PhysicsScene, ControllerRadius)),
				property("Height", LUMIX_PROP(PhysicsScene, ControllerHeight)),
				property("Layer", LUMIX_PROP(PhysicsScene, ControllerLayer)),
				property("Use Custom Gravity", LUMIX_PROP(PhysicsScene, ControllerCustomGravity)),
				property("Custom Gravity Acceleration", LUMIX_PROP(PhysicsScene, ControllerCustomGravityAcceleration))
			),
			component("rigid_actor",
				property("Layer", LUMIX_PROP(PhysicsScene, ActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, DynamicType), dynamicTypeDesc),
				property("Trigger", LUMIX_PROP(PhysicsScene, IsTrigger)),
				array("Box geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Size", LUMIX_PROP(PhysicsScene, BoxGeomHalfExtents)),
					property("Position offset", LUMIX_PROP(PhysicsScene, BoxGeomOffsetPosition)),
					property("Rotation offset", LUMIX_PROP(PhysicsScene, BoxGeomOffsetRotation),
						RadiansAttribute())
				),
				array("Sphere geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Radius", LUMIX_PROP(PhysicsScene, SphereGeomRadius),
						MinAttribute(0)),
					property("Position offset", LUMIX_PROP(PhysicsScene, SphereGeomOffsetPosition)),
					property("Rotation offset", LUMIX_PROP(PhysicsScene, SphereGeomOffsetRotation),
						RadiansAttribute())
				)
			),
			component("box_rigid_actor",
				functions(
					function(LUMIX_FUNC(PhysicsScene::applyForceToActor)),
					function(LUMIX_FUNC(PhysicsScene::applyImpulseToActor)),
					function(LUMIX_FUNC(PhysicsScene::getActorVelocity))
				),
				property("Layer", LUMIX_PROP(PhysicsScene, ActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, DynamicType), dynamicTypeDesc),
				property("Trigger", LUMIX_PROP(PhysicsScene, IsTrigger)),
				property("Size", LUMIX_PROP(PhysicsScene, HalfExtents))
			),
			component("mesh_rigid_actor",
				functions(
					function(LUMIX_FUNC(PhysicsScene::applyForceToActor)),
					function(LUMIX_FUNC(PhysicsScene::applyImpulseToActor)),
					function(LUMIX_FUNC(PhysicsScene::getActorVelocity))
				),
				property("Layer", LUMIX_PROP(PhysicsScene, ActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, DynamicType), dynamicTypeDesc),
				property("Source", LUMIX_PROP(PhysicsScene, ShapeSource),
					ResourceAttribute("Physics (*.phy)", PhysicsGeometry::TYPE))
			),
			component("physical_heightfield",
				property("Layer", LUMIX_PROP(PhysicsScene, HeightfieldLayer)),
				property("Heightmap", LUMIX_PROP(PhysicsScene, HeightmapSource),
					ResourceAttribute("Image (*.raw)", Texture::TYPE)),
				property("Y scale", LUMIX_PROP(PhysicsScene, HeightmapYScale),
					MinAttribute(0)),
				property("XZ scale", LUMIX_PROP(PhysicsScene, HeightmapXZScale),
					MinAttribute(0))
			)
		);
		registerScene(phy_scene);
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
			m_manager.create(PhysicsGeometry::TYPE, engine.getResourceManager());
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



