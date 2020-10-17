#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "engine/log.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "physics/physics_geometry.h"
#include "physics/physics_scene.h"
#include "renderer/texture.h"


namespace Lumix
{
	static void registerProperties(IAllocator& allocator)
	{
		using namespace Reflection;

		struct LayerEnum : Reflection::EnumAttribute {
			u32 count(ComponentUID cmp) const override { 
				return ((PhysicsScene*)cmp.scene)->getSystem().getCollisionsLayersCount();
			}
			const char* name(ComponentUID cmp, u32 idx) const override { 
				PhysicsSystem& system = ((PhysicsScene*)cmp.scene)->getSystem();
				return system.getCollisionLayerName(idx);
			}
		};

		struct DynamicTypeEnum : Reflection::EnumAttribute {
			u32 count(ComponentUID cmp) const override { return 3; }
			const char* name(ComponentUID cmp, u32 idx) const override { 
				switch ((PhysicsScene::DynamicType)idx) {
					case PhysicsScene::DynamicType::DYNAMIC: return "Dynamic";
					case PhysicsScene::DynamicType::STATIC: return "Static";
					case PhysicsScene::DynamicType::KINEMATIC: return "Kinematic";
					default: ASSERT(false); return "N/A";
				}
			}
		};

		struct D6MotionEnum : Reflection::EnumAttribute {
			u32 count(ComponentUID cmp) const override { return 3; }
			const char* name(ComponentUID cmp, u32 idx) const override { 
				switch ((PhysicsScene::D6Motion)idx) {
					case PhysicsScene::D6Motion::LOCKED: return "Locked";
					case PhysicsScene::D6Motion::LIMITED: return "Limited";
					case PhysicsScene::D6Motion::FREE: return "Free";
					default: ASSERT(false); return "N/A";
				}
			}
		};

		struct WheelSlotEnum : Reflection::EnumAttribute {
			u32 count(ComponentUID cmp) const override { return 4; }
			const char* name(ComponentUID cmp, u32 idx) const override { 
				switch ((PhysicsScene::WheelSlot)idx) {
					case PhysicsScene::WheelSlot::FRONT_LEFT: return "Front left";
					case PhysicsScene::WheelSlot::FRONT_RIGHT: return "Front right";
					case PhysicsScene::WheelSlot::REAR_LEFT: return "Rear left";
					case PhysicsScene::WheelSlot::REAR_RIGHT: return "Rear right";
					default: ASSERT(false); return "N/A";
				}
			}
		};

		static auto phy_scene = scene("physics",
			functions(
				LUMIX_FUNC(PhysicsScene::raycast)
				//function(LUMIX_FUNC(PhysicsScene::raycastEx))
			),
			component("ragdoll",
				blob_property("data", LUMIX_PROP(PhysicsScene, RagdollData)),
				property("Layer", LUMIX_PROP(PhysicsScene, RagdollLayer), LayerEnum())
			),
			component("d6_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, JointAxisDirection)),
				enum_property("X motion", LUMIX_PROP(PhysicsScene, D6JointXMotion), D6MotionEnum()),
				enum_property("Y motion", LUMIX_PROP(PhysicsScene, D6JointYMotion), D6MotionEnum()),
				enum_property("Z motion", LUMIX_PROP(PhysicsScene, D6JointZMotion), D6MotionEnum()),
				enum_property("Swing 1", LUMIX_PROP(PhysicsScene, D6JointSwing1Motion), D6MotionEnum()),
				enum_property("Swing 2", LUMIX_PROP(PhysicsScene, D6JointSwing2Motion), D6MotionEnum()),
				enum_property("Twist", LUMIX_PROP(PhysicsScene, D6JointTwistMotion), D6MotionEnum()),
				property("Linear limit", LUMIX_PROP(PhysicsScene, D6JointLinearLimit), MinAttribute(0)),
				property("Swing limit", LUMIX_PROP(PhysicsScene, D6JointSwingLimit), RadiansAttribute()),
				property("Twist limit", LUMIX_PROP(PhysicsScene, D6JointTwistLimit), RadiansAttribute()),
				property("Damping", LUMIX_PROP(PhysicsScene, D6JointDamping)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, D6JointStiffness)),
				property("Restitution", LUMIX_PROP(PhysicsScene, D6JointRestitution))
			),
			component("spherical_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, JointAxisDirection)),
				property("Use limit", LUMIX_PROP(PhysicsScene, SphericalJointUseLimit)),
				property("Limit", LUMIX_PROP(PhysicsScene, SphericalJointLimit), RadiansAttribute())
			),
			component("distance_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Damping", LUMIX_PROP(PhysicsScene, DistanceJointDamping),	MinAttribute(0)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, DistanceJointStiffness), MinAttribute(0)),
				property("Tolerance", LUMIX_PROP(PhysicsScene, DistanceJointTolerance), MinAttribute(0)),
				property("Limits", LUMIX_PROP(PhysicsScene, DistanceJointLimits))
			),
			component("hinge_joint",
				property("Connected body", LUMIX_PROP(PhysicsScene, JointConnectedBody)),
				property("Damping", LUMIX_PROP(PhysicsScene, HingeJointDamping), MinAttribute(0)),
				property("Stiffness", LUMIX_PROP(PhysicsScene, HingeJointStiffness), MinAttribute(0)),
				property("Axis position", LUMIX_PROP(PhysicsScene, JointAxisPosition)),
				property("Axis direction", LUMIX_PROP(PhysicsScene, JointAxisDirection)),
				property("Use limit", LUMIX_PROP(PhysicsScene, HingeJointUseLimit)),
				property("Limit", LUMIX_PROP(PhysicsScene, HingeJointLimit), RadiansAttribute())
			),
			component("physical_controller",
				functions(
					LUMIX_FUNC_EX(PhysicsScene::moveController, "move"),
					LUMIX_FUNC_EX(PhysicsScene::isControllerCollisionDown, "isCollisionDown")
				),
				property("Radius", LUMIX_PROP(PhysicsScene, ControllerRadius)),
				property("Height", LUMIX_PROP(PhysicsScene, ControllerHeight)),
				property("Layer", LUMIX_PROP(PhysicsScene, ControllerLayer), LayerEnum()),
				property("Use root motion", LUMIX_PROP(PhysicsScene, ControllerUseRootMotion)),
				property("Use custom gravity", LUMIX_PROP(PhysicsScene, ControllerCustomGravity)),
				property("Custom gravity acceleration", LUMIX_PROP(PhysicsScene, ControllerCustomGravityAcceleration))
			),
			component("rigid_actor",
				functions(
					LUMIX_FUNC_EX(PhysicsScene::putToSleep, "putToSleep"),
					LUMIX_FUNC_EX(PhysicsScene::getActorSpeed, "getSpeed"),
					LUMIX_FUNC_EX(PhysicsScene::getActorVelocity, "getVelocity"),
					LUMIX_FUNC_EX(PhysicsScene::applyForceToActor, "applyForce"),
					LUMIX_FUNC_EX(PhysicsScene::applyForceToActor, "applyImpulse"),
					LUMIX_FUNC_EX(PhysicsScene::addForceAtPos, "addForceAtPos")
				),
				property("Layer", LUMIX_PROP(PhysicsScene, ActorLayer), LayerEnum()),
				enum_property("Dynamic", LUMIX_PROP(PhysicsScene, DynamicType), DynamicTypeEnum()),
				property("Trigger", LUMIX_PROP(PhysicsScene, IsTrigger)),
				array("Box geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
					property("Size", LUMIX_PROP(PhysicsScene, BoxGeomHalfExtents)),
					property("Position offset", LUMIX_PROP(PhysicsScene, BoxGeomOffsetPosition)),
					property("Rotation offset", LUMIX_PROP(PhysicsScene, BoxGeomOffsetRotation), RadiansAttribute())),
				array("Sphere geometry", &PhysicsScene::getSphereGeometryCount, &PhysicsScene::addSphereGeometry, &PhysicsScene::removeSphereGeometry,
					property("Radius", LUMIX_PROP(PhysicsScene, SphereGeomRadius), MinAttribute(0)),
					property("Position offset", LUMIX_PROP(PhysicsScene, SphereGeomOffsetPosition))),
				property("Mesh", LUMIX_PROP(PhysicsScene, MeshGeomPath), ResourceAttribute("Mesh (*.msh)", PhysicsGeometry::TYPE))
			),
			component("wheel",
				property("Radius", LUMIX_PROP(PhysicsScene, WheelRadius), MinAttribute(0)),
				property("Width", LUMIX_PROP(PhysicsScene, WheelWidth), MinAttribute(0)),
				property("Mass", LUMIX_PROP(PhysicsScene, WheelMass), MinAttribute(0)),
				property("MOI", LUMIX_PROP(PhysicsScene, WheelMOI), MinAttribute(0)),
				enum_property("Slot", LUMIX_PROP(PhysicsScene, WheelSlot), WheelSlotEnum())
			),
			component("physical_heightfield",
				property("Layer", LUMIX_PROP(PhysicsScene, HeightfieldLayer), LayerEnum()),
				property("Heightmap", LUMIX_PROP(PhysicsScene, HeightmapSource), ResourceAttribute("Image (*.raw)", Texture::TYPE)),
				property("Y scale", LUMIX_PROP(PhysicsScene, HeightmapYScale), MinAttribute(0)),
				property("XZ scale", LUMIX_PROP(PhysicsScene, HeightmapXZScale), MinAttribute(0))
			)
		);
		registerScene(phy_scene);
	}


	struct CustomErrorCallback : physx::PxErrorCallback
	{
		void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
		{
			logError("Physics") << message;
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


	struct PhysicsSystemImpl final : PhysicsSystem
	{
		explicit PhysicsSystemImpl(Engine& engine)
			: m_allocator(engine.getAllocator())
			, m_engine(engine)
			, m_manager(*this, engine.getAllocator())
			, m_physx_allocator(m_allocator)
		{
			m_layers.count = 2;
			memset(m_layers.names, 0, sizeof(m_layers.names));
			for (u32 i = 0; i < lengthOf(m_layers.names); ++i)
			{
				copyString(m_layers.names[i], "Layer");
				char tmp[3];
				toCString(i, Span(tmp));
				catString(m_layers.names[i], tmp);
				m_layers.filter[i] = 0xffffFFFF;
			}
			
			registerProperties(engine.getAllocator());
			m_manager.create(PhysicsGeometry::TYPE, engine.getResourceManager());
			PhysicsScene::registerLuaAPI(m_engine.getState());

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
			physx::PxVehicleSetBasisVectors(physx::PxVec3(0, 1, 0), physx::PxVec3(0, 0, 1));
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



