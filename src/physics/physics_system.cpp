#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "engine/base_proxy_allocator.h"
#include "engine/log.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "engine/universe/universe.h"
#include <cstdlib>


namespace Lumix
{


	static const ResourceType TEXTURE_TYPE("texture");
	static const ResourceType PHYSICS_TYPE("physics");


	const char* getD6MotionName(int index)
	{
		switch ((PhysicsScene::D6Motion)index)
		{
			case PhysicsScene::D6Motion::LOCKED: return "locked";
			case PhysicsScene::D6Motion::LIMITED: return "limited";
			case PhysicsScene::D6Motion::FREE: return "free";
			default: ASSERT(false); return "Unknown";
		}
	}


	const char* getDynamicTypeName(int index)
	{
		switch ((PhysicsScene::DynamicType)index)
		{
			case PhysicsScene::DynamicType::STATIC: return "static";
			case PhysicsScene::DynamicType::DYNAMIC: return "dynamic";
			case PhysicsScene::DynamicType::KINEMATIC: return "kinematic";
			default: ASSERT(false); return "Unknown";
		}
	}


	static void registerProperties(IAllocator& allocator)
	{
		using namespace Reflection;

		static auto phy_scene = scene("physics",
			functions(
				function(LUMIX_FUNC(PhysicsScene::raycast)),
				function(LUMIX_FUNC(PhysicsScene::raycastEx))
			),
			component("ragdoll",
				blob_property("data", LUMIX_PROP(PhysicsScene, getRagdollData, setRagdollData)),
				property("Layer", LUMIX_PROP(PhysicsScene, getRagdollLayer, setRagdollLayer))
			),
			component("sphere_rigid_actor",
				property("Radius", LUMIX_PROP(PhysicsScene, getSphereRadius, setSphereRadius),
					MinAttribute(0)),
				property("Layer", LUMIX_PROP(PhysicsScene, getActorLayer, setActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, getDynamicType, setDynamicType), 3, getDynamicTypeName),
				property("Trigger", LUMIX_PROP(PhysicsScene, getIsTrigger, setIsTrigger)) 
			),
			component("capsule_rigid_actor",
				property("Radius", LUMIX_PROP(PhysicsScene, getCapsuleRadius, setCapsuleRadius),
					MinAttribute(0)),
				property("Height", LUMIX_PROP(PhysicsScene, getCapsuleHeight, setCapsuleHeight)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, getDynamicType, setDynamicType), 3, getDynamicTypeName),
				property("Trigger", LUMIX_PROP(PhysicsScene, getIsTrigger, setIsTrigger))
			),
			component("d6_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, getJointConnectedBody, setJointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, getJointAxisPosition, setJointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, getJointAxisDirection, setJointAxisDirection)),
				enum_property("X motion", LUMIX_PROP(PhysicsScene, getD6JointXMotion, setD6JointXMotion), 3, getD6MotionName),
				enum_property("Y motion", LUMIX_PROP(PhysicsScene, getD6JointYMotion, setD6JointYMotion), 3, getD6MotionName),
				enum_property("Z motion", LUMIX_PROP(PhysicsScene, getD6JointZMotion, setD6JointZMotion), 3, getD6MotionName),
				enum_property("Swing 1", LUMIX_PROP(PhysicsScene, getD6JointSwing1Motion, setD6JointSwing1Motion), 3, getD6MotionName),
				enum_property("Swing 2", LUMIX_PROP(PhysicsScene, getD6JointSwing2Motion, setD6JointSwing2Motion), 3, getD6MotionName),
				enum_property("Twist", LUMIX_PROP(PhysicsScene, getD6JointTwistMotion, setD6JointTwistMotion), 3, getD6MotionName),
				property("Linear limit", LUMIX_PROP(PhysicsScene, getD6JointLinearLimit, setD6JointLinearLimit),
					MinAttribute(0)),
				property("Swing limit", LUMIX_PROP(PhysicsScene, getD6JointSwingLimit, setD6JointSwingLimit),
					RadiansAttribute()),
				property("Twist limit", LUMIX_PROP(PhysicsScene, getD6JointTwistLimit, setD6JointTwistLimit),
					RadiansAttribute())
			),
			component("spherical_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, getJointConnectedBody, setJointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, getJointAxisPosition, setJointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, getJointAxisDirection, setJointAxisDirection)),
				property("Use limit", LUMIX_PROP(PhysicsScene, getSphericalJointUseLimit, setSphericalJointUseLimit)),
				property("Limit", LUMIX_PROP(PhysicsScene, getSphericalJointLimit, setSphericalJointLimit),
					RadiansAttribute())
			),
			component("distance_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, getJointConnectedBody, setJointConnectedBody)),
				property("Damping", LUMIX_PROP(PhysicsScene, getDistanceJointDamping, setDistanceJointDamping),
					MinAttribute(0)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, getDistanceJointStiffness, setDistanceJointStiffness),
					MinAttribute(0)),
				property("Tolerance", LUMIX_PROP(PhysicsScene, getDistanceJointTolerance, setDistanceJointTolerance),
					MinAttribute(0)),
				property("Limits", LUMIX_PROP(PhysicsScene, getDistanceJointLimits, setDistanceJointLimits))
			),
			component("hinge_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, getJointConnectedBody, setJointConnectedBody)),
				property("Damping", LUMIX_PROP(PhysicsScene, getHingeJointDamping, setHingeJointDamping),
					MinAttribute(0)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, getHingeJointStiffness, setHingeJointStiffness),
					MinAttribute(0)),
				property("Axis position", LUMIX_PROP(PhysicsScene, getJointAxisPosition, setJointAxisPosition)), 
				property("Axis direction", LUMIX_PROP(PhysicsScene, getJointAxisDirection, setJointAxisDirection)),
				property("Use limit", LUMIX_PROP(PhysicsScene, getHingeJointUseLimit, setHingeJointUseLimit)),
				property("Limit", LUMIX_PROP(PhysicsScene, getHingeJointLimit, setHingeJointLimit),
					RadiansAttribute())
			),
			component("physical_controller",
				functions(
					function(LUMIX_FUNC(PhysicsScene::moveController))
				),
				property("Layer", LUMIX_PROP(PhysicsScene, getControllerLayer, setControllerLayer))
			),
			component("rigid_actor",
				property("Layer", LUMIX_PROP(PhysicsScene, getActorLayer, setActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, getDynamicType, setDynamicType), 3, getDynamicTypeName),
				property("Trigger", LUMIX_PROP(PhysicsScene, getIsTrigger, setIsTrigger)),
				array("Box geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Size", LUMIX_PROP(PhysicsScene, getBoxGeomHalfExtents, setBoxGeomHalfExtents)),
					property("Position offset", LUMIX_PROP(PhysicsScene, getBoxGeomOffsetPosition, setBoxGeomOffsetPosition)),
					property("Rotation offset", LUMIX_PROP(PhysicsScene, getBoxGeomOffsetRotation, setBoxGeomOffsetRotation),
						RadiansAttribute())
				),
				array("Sphere geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Radius", LUMIX_PROP(PhysicsScene, getSphereGeomRadius, setSphereGeomRadius),
						MinAttribute(0)),
					property("Position offset", LUMIX_PROP(PhysicsScene, getSphereGeomOffsetPosition, setSphereGeomOffsetPosition)),
					property("Rotation offset", LUMIX_PROP(PhysicsScene, getSphereGeomOffsetRotation, setSphereGeomOffsetRotation),
						RadiansAttribute())
				)
			),
			component("box_rigid_actor",
				property("Layer", LUMIX_PROP(PhysicsScene, getActorLayer, setActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, getDynamicType, setDynamicType), 3, getDynamicTypeName),
				property("Trigger", LUMIX_PROP(PhysicsScene, getIsTrigger, setIsTrigger)),
				property("Size", LUMIX_PROP(PhysicsScene, getHalfExtents, setHalfExtents))
			),
			component("mesh_rigid_actor",
				property("Layer", LUMIX_PROP(PhysicsScene, getActorLayer, setActorLayer)),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, getDynamicType, setDynamicType), 3, getDynamicTypeName),
				property("Source", LUMIX_PROP(PhysicsScene, getShapeSource, setShapeSource),
					ResourceAttribute("Physics (*.phy)", PHYSICS_TYPE))
			),
			component("physical_heightfield",
				property("Layer", LUMIX_PROP(PhysicsScene, getHeightfieldLayer, setHeightfieldLayer)),
				property("Heightmap", LUMIX_PROP(PhysicsScene, getHeightmapSource, setHeightmapSource),
					ResourceAttribute("Image (*.raw)", TEXTURE_TYPE)),
				property("Y scale", LUMIX_PROP(PhysicsScene, getHeightmapYScale, setHeightmapYScale),
					MinAttribute(0)),
				property("XZ scale", LUMIX_PROP(PhysicsScene, getHeightmapXZScale, setHeightmapXZScale),
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



