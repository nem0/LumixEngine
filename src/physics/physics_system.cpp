#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "engine/base_proxy_allocator.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/engine.h"
#include "engine/properties.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"
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
		using namespace Properties;

		static auto phy_scene = scene("physics",
			component("ragdoll",
				blob_property("data", &PhysicsScene::getRagdollData, &PhysicsScene::setRagdollData),
				property("Layer", &PhysicsScene::getRagdollLayer, &PhysicsScene::setRagdollLayer)
			),
			component("sphere_rigid_actor",
				property("Radius", &PhysicsScene::getSphereRadius, &PhysicsScene::setSphereRadius,
					MinAttribute(0)),
				property("Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer),
				enum_property("Dynamic", &PhysicsScene::getDynamicType, &PhysicsScene::setDynamicType, 3, getDynamicTypeName),
				property("Trigger", &PhysicsScene::getIsTrigger, &PhysicsScene::setIsTrigger) 
			),
			component("capsule_rigid_actor",
				property("Radius", &PhysicsScene::getCapsuleRadius, &PhysicsScene::setCapsuleRadius,
					MinAttribute(0)),
				property("Height", &PhysicsScene::getCapsuleHeight, &PhysicsScene::setCapsuleHeight),
				enum_property("Dynamic", &PhysicsScene::getDynamicType, &PhysicsScene::setDynamicType, 3, getDynamicTypeName),
				property("Trigger", &PhysicsScene::getIsTrigger, &PhysicsScene::setIsTrigger)
			),
			component("d6_joint",
				property("Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody),
				property("Axis position", &PhysicsScene::getJointAxisPosition, &PhysicsScene::setJointAxisPosition),
				property("Axis direction", &PhysicsScene::getJointAxisDirection, &PhysicsScene::setJointAxisDirection),
				enum_property("X motion", &PhysicsScene::getD6JointXMotion, &PhysicsScene::setD6JointXMotion, 3, getD6MotionName),
				enum_property("Y motion", &PhysicsScene::getD6JointYMotion, &PhysicsScene::setD6JointYMotion, 3, getD6MotionName),
				enum_property("Z motion", &PhysicsScene::getD6JointZMotion, &PhysicsScene::setD6JointZMotion, 3, getD6MotionName),
				enum_property("Swing 1", &PhysicsScene::getD6JointSwing1Motion, &PhysicsScene::setD6JointSwing1Motion, 3, getD6MotionName),
				enum_property("Swing 2", &PhysicsScene::getD6JointSwing2Motion, &PhysicsScene::setD6JointSwing2Motion, 3, getD6MotionName),
				enum_property("Twist", &PhysicsScene::getD6JointTwistMotion, &PhysicsScene::setD6JointTwistMotion, 3, getD6MotionName),
				property("Linear limit", &PhysicsScene::getD6JointLinearLimit, &PhysicsScene::setD6JointLinearLimit,
					MinAttribute(0)),
				property("Swing limit", &PhysicsScene::getD6JointSwingLimit, &PhysicsScene::setD6JointSwingLimit,
					RadiansAttribute()),
				property("Twist limit", &PhysicsScene::getD6JointTwistLimit, &PhysicsScene::setD6JointTwistLimit,
					RadiansAttribute())
			),
			component("spherical_joint",
				property("Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody),
				property("Axis position", &PhysicsScene::getJointAxisPosition, &PhysicsScene::setJointAxisPosition),
				property("Axis direction", &PhysicsScene::getJointAxisDirection, &PhysicsScene::setJointAxisDirection),
				property("Use limit", &PhysicsScene::getSphericalJointUseLimit, &PhysicsScene::setSphericalJointUseLimit),
				property("Limit", &PhysicsScene::getSphericalJointLimit, &PhysicsScene::setSphericalJointLimit,
					RadiansAttribute())
			),
			component("distance_joint",
				property("Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody),
				property("Damping", &PhysicsScene::getDistanceJointDamping, &PhysicsScene::setDistanceJointDamping,
					MinAttribute(0)),
				property("Stiffness", &PhysicsScene::getDistanceJointStiffness, &PhysicsScene::setDistanceJointStiffness,
					MinAttribute(0)),
				property("Tolerance", &PhysicsScene::getDistanceJointTolerance, &PhysicsScene::setDistanceJointTolerance,
					MinAttribute(0)),
				property("Limits", &PhysicsScene::getDistanceJointLimits, &PhysicsScene::setDistanceJointLimits)
			),
			component("hinge_joint",
				property("Connected body", &PhysicsScene::getJointConnectedBody, &PhysicsScene::setJointConnectedBody),
				property("Damping", &PhysicsScene::getHingeJointDamping, &PhysicsScene::setHingeJointDamping,
					MinAttribute(0)),
				property("Stiffness", &PhysicsScene::getHingeJointStiffness, &PhysicsScene::setHingeJointStiffness,
					MinAttribute(0)),
				property("Axis position", &PhysicsScene::getJointAxisPosition, &PhysicsScene::setJointAxisPosition), 
				property("Axis direction", &PhysicsScene::getJointAxisDirection, &PhysicsScene::setJointAxisDirection),
				property("Use limit", &PhysicsScene::getHingeJointUseLimit, &PhysicsScene::setHingeJointUseLimit),
				property("Limit", &PhysicsScene::getHingeJointLimit, &PhysicsScene::setHingeJointLimit,
					RadiansAttribute())
			),
			component("physical_controller",
				property("Layer", &PhysicsScene::getControllerLayer, &PhysicsScene::setControllerLayer)
			),
			component("rigid_actor",
				property("Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer),
				enum_property("Dynamic", &PhysicsScene::getDynamicType, &PhysicsScene::setDynamicType, 3, getDynamicTypeName),
				property("Trigger", &PhysicsScene::getIsTrigger, &PhysicsScene::setIsTrigger),
				array("Box geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Size", &PhysicsScene::getBoxGeomHalfExtents, &PhysicsScene::setBoxGeomHalfExtents),
					property("Position offset", &PhysicsScene::getBoxGeomOffsetPosition, &PhysicsScene::setBoxGeomOffsetPosition),
					property("Rotation offset", &PhysicsScene::getBoxGeomOffsetRotation, &PhysicsScene::setBoxGeomOffsetRotation,
						RadiansAttribute())
				),
				array("Sphere geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Radius", &PhysicsScene::getSphereGeomRadius, &PhysicsScene::setSphereGeomRadius,
						MinAttribute(0)),
					property("Position offset", &PhysicsScene::getSphereGeomOffsetPosition, &PhysicsScene::setSphereGeomOffsetPosition),
					property("Rotation offset", &PhysicsScene::getSphereGeomOffsetRotation, &PhysicsScene::setSphereGeomOffsetRotation,
						RadiansAttribute())
				)
			),
			component("box_rigid_actor",
				property("Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer),
				enum_property("Dynamic", &PhysicsScene::getDynamicType, &PhysicsScene::setDynamicType, 3, getDynamicTypeName),
				property("Trigger", &PhysicsScene::getIsTrigger, &PhysicsScene::setIsTrigger),
				property("Size", &PhysicsScene::getHalfExtents, &PhysicsScene::setHalfExtents)
			),
			component("mesh_rigid_actor",
				property("Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer),
				enum_property("Dynamic", &PhysicsScene::getDynamicType, &PhysicsScene::setDynamicType, 3, getDynamicTypeName),
				property("Source", &PhysicsScene::getShapeSource, &PhysicsScene::setShapeSource,
					ResourceAttribute("Physics (*.phy)", PHYSICS_TYPE))
			),
			component("physical_heightfield",
				property("Layer", &PhysicsScene::getActorLayer, &PhysicsScene::setActorLayer),
				property("Heightmap", &PhysicsScene::getHeightmapSource, &PhysicsScene::setHeightmapSource,
					ResourceAttribute("Image (*.raw)", TEXTURE_TYPE)),
				property("Y scale", &PhysicsScene::getHeightmapYScale, &PhysicsScene::setHeightmapYScale,
					MinAttribute(0)),
				property("XZ scale", &PhysicsScene::getHeightmapXZScale, &PhysicsScene::setHeightmapXZScale,
					MinAttribute(0))
			)
		);
		phy_scene.registerScene();
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



