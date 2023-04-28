#include <characterkinematic/PxCapsuleController.h>
#include <characterkinematic/PxControllerBehavior.h>
#include <characterkinematic/PxControllerManager.h>
#include <cooking/PxConvexMeshDesc.h>
#include <cooking/PxCooking.h>
#include <extensions/PxD6Joint.h>
#include <extensions/PxDefaultStreams.h>
#include <extensions/PxDistanceJoint.h>
#include <extensions/PxFixedJoint.h>
#include <extensions/PxRevoluteJoint.h>
#include <extensions/PxRigidActorExt.h>
#include <extensions/PxRigidBodyExt.h>
#include <extensions/PxSimpleFactory.h>
#include <extensions/PxSphericalJoint.h>
#include <foundation/PxIO.h>
#include <foundation/PxMat44.h>
#include <geometry/PxHeightField.h>
#include <geometry/PxHeightFieldDesc.h>
#include <geometry/PxHeightFieldSample.h>
#include <PxBatchQuery.h>
#include <PxMaterial.h>
#include <PxRigidActor.h>
#include <PxRigidStatic.h>
#include <PxScene.h>
#include <PxSimulationEventCallback.h>
#include <task/PxCpuDispatcher.h>
#include <task/PxTask.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleUtilSetup.h>

#include "physics/physics_module.h"
#include "animation/animation_module.h"
#include "engine/associative_array.h"
#include "engine/atomic.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/world.h"
#include "lua_script/lua_script_system.h"
#include "physics/physics_resources.h"
#include "physics/physics_system.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_module.h"
#include "renderer/texture.h"
#include "imgui/IconsFontAwesome5.h"


using namespace physx;


namespace Lumix
{


static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");
static const ComponentType INSTANCED_MODEL_TYPE = reflection::getComponentType("instanced_model");
static const ComponentType RIGID_ACTOR_TYPE = reflection::getComponentType("rigid_actor");
static const ComponentType CONTROLLER_TYPE = reflection::getComponentType("physical_controller");
static const ComponentType HEIGHTFIELD_TYPE = reflection::getComponentType("physical_heightfield");
static const ComponentType DISTANCE_JOINT_TYPE = reflection::getComponentType("distance_joint");
static const ComponentType HINGE_JOINT_TYPE = reflection::getComponentType("hinge_joint");
static const ComponentType SPHERICAL_JOINT_TYPE = reflection::getComponentType("spherical_joint");
static const ComponentType D6_JOINT_TYPE = reflection::getComponentType("d6_joint");
static const ComponentType VEHICLE_TYPE = reflection::getComponentType("vehicle");
static const ComponentType WHEEL_TYPE = reflection::getComponentType("wheel");
static const ComponentType INSTANCED_CUBE_TYPE = reflection::getComponentType("physical_instanced_cube");
static const ComponentType INSTANCED_MESH_TYPE = reflection::getComponentType("physical_instanced_mesh");

enum class FilterFlags : u32 {
	VEHICLE = 1 << 0
};

enum class PhysicsModuleVersion
{
	REMOVED_RAGDOLLS,
	VEHICLE_PEAK_TORQUE,
	VEHICLE_MAX_RPM,
	INSTANCED_CUBE,
	INSTANCED_MESH,
	MATERIAL,

	LATEST,
};

static constexpr PxVehiclePadSmoothingData pad_smoothing =
{
	{
		6.0f,	//rise rate eANALOG_INPUT_ACCEL		
		6.0f,	//rise rate eANALOG_INPUT_BRAKE		
		12.0f,	//rise rate eANALOG_INPUT_HANDBRAKE	
		2.5f,	//rise rate eANALOG_INPUT_STEER_LEFT	
		2.5f,	//rise rate eANALOG_INPUT_STEER_RIGHT	
	},
	{
		10.0f,	//fall rate eANALOG_INPUT_ACCEL		
		10.0f,	//fall rate eANALOG_INPUT_BRAKE		
		12.0f,	//fall rate eANALOG_INPUT_HANDBRAKE	
		5.0f,	//fall rate eANALOG_INPUT_STEER_LEFT	
		5.0f	//fall rate eANALOG_INPUT_STEER_RIGHT	
	}
};

static constexpr PxF32 steer_vs_forward_speed_data[] =
{
	0.0f,		0.75f,
	5.0f,		0.75f,
	30.0f,		0.125f,
	120.0f,		0.1f,
	PX_MAX_F32, PX_MAX_F32,
	PX_MAX_F32, PX_MAX_F32,
	PX_MAX_F32, PX_MAX_F32,
	PX_MAX_F32, PX_MAX_F32
};

static const PxFixedSizeLookupTable<8> steer_vs_forward_speed(steer_vs_forward_speed_data, 4);


struct InputStream final : PxInputStream
{
	InputStream(unsigned char* data, int size)
	{
		this->data = data;
		this->size = size;
		pos = 0;
	}

	PxU32 read(void* dest, PxU32 count) override
	{
		if (pos + (int)count <= size)
		{
			memcpy(dest, data + pos, count);
			pos += count;
			return count;
		}
		else
		{
			memcpy(dest, data + pos, size - pos);
			int real_count = size - pos;
			pos = size;
			return real_count;
		}
	}


	int pos;
	int size;
	unsigned char* data;
};


static Vec3 fromPhysx(const PxVec3& v)
{
	return Vec3(v.x, v.y, v.z);
}
static PxVec3 toPhysx(const Vec3& v)
{
	return PxVec3(v.x, v.y, v.z);
}
static PxVec3 toPhysx(const DVec3& v)
{
	return PxVec3((float)v.x, (float)v.y, (float)v.z);
}
static Quat fromPhysx(const PxQuat& v)
{
	return Quat(v.x, v.y, v.z, v.w);
}
static PxQuat toPhysx(const Quat& v)
{
	return PxQuat(v.x, v.y, v.z, v.w);
}
static RigidTransform fromPhysx(const PxTransform& v)
{
	return {DVec3(fromPhysx(v.p)), fromPhysx(v.q)};
}
static PxTransform toPhysx(const RigidTransform& v)
{
	return {toPhysx(Vec3(v.pos)), toPhysx(v.rot)};
}


struct Joint
{
	EntityPtr connected_body;
	PxJoint* physx;
	PxTransform local_frame0;
};


struct Vehicle
{
	PxRigidDynamic* actor = nullptr;
	PxVehicleDrive4WRawInputData raw_input;
	PxVehicleDrive4W* drive = nullptr;
	float mass = 1'500;
	PhysicsGeometry* geom = nullptr;
	u32 wheels_layer = 1;
	u32 chassis_layer = 0;
	Vec3 center_of_mass = Vec3(0);
	float moi_multiplier = 1;
	float peak_torque = 500.f;
	float max_rpm = 6000.f;

	void onStateChanged(Resource::State old_state, Resource::State new_state, Resource&) {

	}
};


struct Wheel
{
	float mass = 1;
	float radius = 1;
	float width = 0.2f;
	float moi = 1;
	float max_droop = 0.1f;
	float max_compression = 0.3f;
	float spring_strength = 10'000.f;
	float spring_damper_rate = 4'500.f;
	PhysicsModule::WheelSlot slot = PhysicsModule::WheelSlot::FRONT_LEFT;
	
	static_assert((int)PhysicsModule::WheelSlot::FRONT_LEFT == PxVehicleDrive4WWheelOrder::eFRONT_LEFT);
	static_assert((int)PhysicsModule::WheelSlot::FRONT_RIGHT == PxVehicleDrive4WWheelOrder::eFRONT_RIGHT);
	static_assert((int)PhysicsModule::WheelSlot::REAR_LEFT == PxVehicleDrive4WWheelOrder::eREAR_LEFT);
	static_assert((int)PhysicsModule::WheelSlot::REAR_RIGHT == PxVehicleDrive4WWheelOrder::eREAR_RIGHT);
};


struct Heightfield {
	~Heightfield();
	void heightmapLoaded(Resource::State, Resource::State new_state, Resource&);

	struct PhysicsModuleImpl* m_module;
	EntityRef m_entity;
	PxRigidActor* m_actor = nullptr;
	Texture* m_heightmap = nullptr;
	float m_xz_scale = 1.f;
	float m_y_scale = 1.f;
	i32 m_layer = 0;
};


struct PhysicsModuleImpl final : PhysicsModule
{
	struct CPUDispatcher : physx::PxCpuDispatcher
	{
		void submitTask(PxBaseTask& task) override
		{
			jobs::runLambda([&task]() {
					PROFILE_BLOCK(task.getName());
					profiler::blockColor(0x50, 0xff, 0x50);
					task.run();
					task.release();
				},
				nullptr);
		}
		PxU32 getWorkerCount() const override { return os::getCPUsCount(); }
	};


	struct PhysxContactCallback final : PxSimulationEventCallback
	{
		explicit PhysxContactCallback(PhysicsModuleImpl& module)
			: m_module(module)
		{
		}


		void onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override
		{
		}


		void onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs) override
		{
			for (PxU32 i = 0; i < nbPairs; i++)
			{
				const auto& cp = pairs[i];

				if (!(cp.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)) continue;

				PxContactPairPoint contact;
				cp.extractContacts(&contact, 1);

				ContactData contact_data;
				contact_data.position = fromPhysx(contact.position);
				contact_data.e1 = {(int)(intptr_t)(pairHeader.actors[0]->userData)};
				contact_data.e2 = {(int)(intptr_t)(pairHeader.actors[1]->userData)};

				m_module.onContact(contact_data);
			}
		}


		void onTrigger(PxTriggerPair* pairs, PxU32 count) override
		{
			for (PxU32 i = 0; i < count; i++)
			{
				const auto REMOVED_FLAGS =
					PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER;
				if (pairs[i].flags & REMOVED_FLAGS) continue;

				EntityRef e1 = {(int)(intptr_t)(pairs[i].triggerActor->userData)};
				EntityRef e2 = {(int)(intptr_t)(pairs[i].otherActor->userData)};

				m_module.onTrigger(e1, e2, pairs[i].status == PxPairFlag::eNOTIFY_TOUCH_LOST);
			}
		}


		void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
		void onWake(PxActor**, PxU32) override {}
		void onSleep(PxActor**, PxU32) override {}


		PhysicsModuleImpl& m_module;
	};


	struct RigidActor {
		RigidActor(PhysicsModuleImpl& module, EntityRef entity)
			: module(module)
			, entity(entity)
		{}

		RigidActor(RigidActor&& rhs)
			: module(rhs.module)
			, entity(rhs.entity)
			, physx_actor(rhs.physx_actor)
			, mesh(rhs.mesh)
			, material(rhs.material)
			, scale(rhs.scale)
			, layer(rhs.layer)
			, prev_with_mesh(rhs.prev_with_mesh)
			, next_with_mesh(rhs.next_with_mesh)
			, dynamic_type(rhs.dynamic_type)
			, is_trigger(rhs.is_trigger)
		{
			rhs.mesh = nullptr;
			rhs.material = nullptr;
			rhs.physx_actor = nullptr;
		}

		void operator =(RigidActor&& rhs) = delete;

		~RigidActor() {
			setMesh(nullptr);
			if (physx_actor) physx_actor->release();
		}

		void rescale();
		void setMesh(PhysicsGeometry* resource);
		void setPhysxActor(PxRigidActor* actor);
		void onStateChanged(Resource::State old_state, Resource::State new_state, Resource&);
		void setIsTrigger(bool is);

		PhysicsModuleImpl& module;
		EntityRef entity;
		PxRigidActor* physx_actor = nullptr;
		PhysicsGeometry* mesh  = nullptr;
		PhysicsMaterial* material = nullptr;
		Vec3 scale = Vec3(1);
		i32 layer = 0;
		EntityPtr prev_with_mesh = INVALID_ENTITY;
		EntityPtr next_with_mesh = INVALID_ENTITY;
		DynamicType dynamic_type = DynamicType::STATIC;
		bool is_trigger = false;
	};


	PhysicsModuleImpl(Engine& engine, World& world, PhysicsSystem& system, IAllocator& allocator);


	PxBatchQuery* createVehicleBatchQuery(u8* mem)
	{
		const PxU32 maxNumQueriesInBatch = 64;
		const PxU32 maxNumHitResultsInBatch = 64;

		PxBatchQueryDesc desc(maxNumQueriesInBatch, maxNumQueriesInBatch, 0);

		desc.queryMemory.userRaycastResultBuffer = (PxRaycastQueryResult*)(mem + sizeof(PxRaycastHit) * 64);
		desc.queryMemory.userRaycastTouchBuffer = (PxRaycastHit*)mem;
		desc.queryMemory.raycastTouchBufferSize = maxNumHitResultsInBatch;

		m_vehicle_results = desc.queryMemory.userRaycastResultBuffer;

		desc.preFilterShader = [](PxFilterData queryFilterData, PxFilterData objectFilterData, const void* constantBlock, PxU32 constantBlockSize, PxHitFlags& hitFlags) -> PxQueryHitType::Enum {
			if (objectFilterData.word3 == (u32)FilterFlags::VEHICLE) return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		};

		return m_scene->createBatchQuery(desc);
	}


	PxVehicleDrivableSurfaceToTireFrictionPairs* createFrictionPairs() const
	{
		PxVehicleDrivableSurfaceType surfaceTypes[1];
		surfaceTypes[0].mType = 0;

		const PxMaterial* surfaceMaterials[1];
		surfaceMaterials[0] = m_default_material;

		auto* surfaceTirePairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);

		surfaceTirePairs->setup(1, 1, surfaceMaterials, surfaceTypes);
		surfaceTirePairs->setTypePairFriction(0, 0, 5.0f);
		return surfaceTirePairs;
	}

	int getVersion() const override { return (int)PhysicsModuleVersion::LATEST; }

	~PhysicsModuleImpl() {
		for (auto& controller : m_controllers) {
			controller.controller->release();
		}
		m_controllers.clear();

		for (auto& v : m_vehicles) {
			if (v->geom) {
				v->geom->getObserverCb().unbind<&Vehicle::onStateChanged>(v.get());
				v->geom->decRefCount();
			}
		}

		m_vehicles.clear();
		m_wheels.clear();
		
		for (auto& ic : m_instanced_cubes) {
			for (PxRigidActor* actor : ic.actors) {
				actor->release();
			}
		}
		m_instanced_cubes.clear();
		
		for (auto& ic : m_instanced_meshes) {
			for (PxRigidActor* actor : ic.actors) {
				actor->release();
			}
		}
		m_instanced_meshes.clear();

		for (auto& joint : m_joints) {
			joint.physx->release();
		}
		m_joints.clear();

		m_actors.clear();
		m_dynamic_actors.clear();

		m_terrains.clear();

		m_vehicle_batch_query->release();
		m_vehicle_frictions->release();
		m_controller_manager->release();
		m_default_material->release();
		m_dummy_actor->release();
		m_scene->release();
	}


	void onTrigger(EntityRef e1, EntityRef e2, bool touch_lost)
	{
		if (!m_script_module) return;

		auto send = [this, touch_lost](EntityRef e1, EntityRef e2) {
			if (!m_script_module->getWorld().hasComponent(e1, LUA_SCRIPT_TYPE)) return;

			for (int i = 0, c = m_script_module->getScriptCount(e1); i < c; ++i)
			{
				auto* call = m_script_module->beginFunctionCall(e1, i, "onTrigger");
				if (!call) continue;

				call->add(e2);
				call->add(touch_lost);
				m_script_module->endFunctionCall();
			}
		};

		send(e1, e2);
		send(e2, e1);
	}

	void onControllerHit(EntityRef controller, EntityRef obj) {
		if (!m_script_module) return;
		if (!m_script_module->getWorld().hasComponent(controller, LUA_SCRIPT_TYPE)) return;

		for (int i = 0, c = m_script_module->getScriptCount(controller); i < c; ++i) {
			auto* call = m_script_module->beginFunctionCall(controller, i, "onControllerHit");
			if (!call) continue;

			call->add(obj);
			m_script_module->endFunctionCall();
		}
	}

	void onContact(const ContactData& contact_data)
	{
		if (!m_script_module) return;

		auto send = [this](EntityRef e1, EntityRef e2, const Vec3& position) {
			if (!m_script_module->getWorld().hasComponent(e1, LUA_SCRIPT_TYPE)) return;

			for (int i = 0, c = m_script_module->getScriptCount(e1); i < c; ++i)
			{
				auto* call = m_script_module->beginFunctionCall(e1, i, "onContact");
				if (!call) continue;

				call->add(e2.index);
				call->add(position.x);
				call->add(position.y);
				call->add(position.z);
				m_script_module->endFunctionCall();
			}
		};

		send(contact_data.e1, contact_data.e2, contact_data.position);
		send(contact_data.e2, contact_data.e1, contact_data.position);
		m_contact_callbacks.invoke(contact_data);
	}


	u32 getDebugVisualizationFlags() const override { return m_debug_visualization_flags; }


	void setDebugVisualizationFlags(u32 flags) override
	{
		if (flags == m_debug_visualization_flags) return;

		m_debug_visualization_flags = flags;

		m_scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, flags != 0 ? 1.0f : 0.0f);

		auto setFlag = [this, flags](int flag) {
			m_scene->setVisualizationParameter(PxVisualizationParameter::Enum(flag), flags & (1 << flag) ? 1.0f : 0.0f);
		};

		setFlag(PxVisualizationParameter::eBODY_AXES);
		setFlag(PxVisualizationParameter::eBODY_MASS_AXES);
		setFlag(PxVisualizationParameter::eBODY_LIN_VELOCITY);
		setFlag(PxVisualizationParameter::eBODY_ANG_VELOCITY);
		setFlag(PxVisualizationParameter::eCONTACT_NORMAL);
		setFlag(PxVisualizationParameter::eCONTACT_ERROR);
		setFlag(PxVisualizationParameter::eCONTACT_FORCE);
		setFlag(PxVisualizationParameter::eCOLLISION_AXES);
		setFlag(PxVisualizationParameter::eJOINT_LOCAL_FRAMES);
		setFlag(PxVisualizationParameter::eJOINT_LIMITS);
		setFlag(PxVisualizationParameter::eCOLLISION_SHAPES);
		setFlag(PxVisualizationParameter::eACTOR_AXES);
		setFlag(PxVisualizationParameter::eCOLLISION_AABBS);
		setFlag(PxVisualizationParameter::eWORLD_AXES);
		setFlag(PxVisualizationParameter::eCONTACT_POINT);
	}


	void setVisualizationCullingBox(const DVec3& min, const DVec3& max) override
	{
		PxBounds3 box(toPhysx(min), toPhysx(max));
		m_scene->setVisualizationCullingBox(box);
	}


	World& getWorld() override { return m_world; }


	ISystem& getSystem() const override { return *m_system; }


	u32 getControllerLayer(EntityRef entity) override { return m_controllers[entity].layer; }


	void setControllerLayer(EntityRef entity, u32 layer) override
	{
		ASSERT(layer < lengthOf(m_layers.names));
		auto& controller = m_controllers[entity];
		controller.layer = layer;

		PxFilterData data;
		data.word0 = 1 << layer;
		data.word1 = m_layers.filter[layer];
		controller.filter_data = data;
		PxShape* shapes[8];
		int shapes_count = controller.controller->getActor()->getShapes(shapes, lengthOf(shapes));
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}
		controller.controller->invalidateCache();
	}

	void setActorLayer(EntityRef entity, u32 layer) override
	{
		ASSERT(layer < lengthOf(m_layers.names));
		RigidActor& actor = m_actors[entity];
		actor.layer = layer;
		if (actor.physx_actor) {
			updateFilterData(actor.physx_actor, actor.layer);
		}
	}


	u32 getActorLayer(EntityRef entity) override { return m_actors[entity].layer; }

	const Vehicle* getWheelVehicle(EntityRef wheel) const {
		EntityPtr parent = m_world.getParent(wheel);
		if (!parent.isValid()) return nullptr;
		auto iter = m_vehicles.find(*parent);
		if (!iter.isValid()) return nullptr;
		return iter.value().get();
	}

	float getWheelRPM(EntityRef entity) override {
		const Wheel& wheel = m_wheels[entity];
		const Vehicle* vehicle = getWheelVehicle(entity);
		if (!vehicle) return 0;
		if (!vehicle->drive) return 0;

		return vehicle->drive->mWheelsDynData.getWheelRotationSpeed((u32)wheel.slot) * (60 / (2 * PI));
	}

	float getWheelMOI(EntityRef entity) override { return m_wheels[entity].moi; }
	void setWheelMOI(EntityRef entity, float moi) override { m_wheels[entity].moi = moi; }
	WheelSlot getWheelSlot(EntityRef entity) override { return m_wheels[entity].slot; }
	void setWheelSlot(EntityRef entity, WheelSlot s) override { m_wheels[entity].slot = s; }
	float getWheelSpringStrength(EntityRef entity) override { return m_wheels[entity].spring_strength; }
	void setWheelSpringStrength(EntityRef entity, float str) override { m_wheels[entity].spring_strength = str; }
	float getWheelSpringMaxCompression(EntityRef entity) override { return m_wheels[entity].max_compression; }
	void setWheelSpringMaxCompression(EntityRef entity, float val) override { m_wheels[entity].max_compression = val; }
	float getWheelSpringMaxDroop(EntityRef entity) override { return m_wheels[entity].max_droop; }
	void setWheelSpringMaxDroop(EntityRef entity, float val) override { m_wheels[entity].max_droop = val; }
	float getWheelSpringDamperRate(EntityRef entity) override { return m_wheels[entity].spring_damper_rate; }
	void setWheelSpringDamperRate(EntityRef entity, float rate) override { m_wheels[entity].spring_damper_rate = rate; }
	float getWheelRadius(EntityRef entity) override { return m_wheels[entity].radius; }
	void setWheelRadius(EntityRef entity, float r) override { m_wheels[entity].radius = r; rebuildWheel(entity); }
	float getWheelWidth(EntityRef entity) override { return m_wheels[entity].width; }
	void setWheelWidth(EntityRef entity, float w) override { m_wheels[entity].width = w; rebuildWheel(entity); }
	float getWheelMass(EntityRef entity) override { return m_wheels[entity].mass; }
	void setWheelMass(EntityRef entity, float m) override { m_wheels[entity].mass = m; rebuildWheel(entity); }

	u32 getVehicleWheelsLayer(EntityRef entity) override {
		return m_vehicles[entity]->wheels_layer;
	}

	void setVehicleWheelsLayer(EntityRef entity, u32 layer) override {
		const UniquePtr<Vehicle>& veh = m_vehicles[entity];
		veh->wheels_layer = layer;
		if (veh->actor) {
			rebuildVehicle(entity, *veh.get());
		}
	}

	u32 getVehicleChassisLayer(EntityRef entity) override {
		return m_vehicles[entity]->chassis_layer;
	}

	void setVehicleChassisLayer(EntityRef entity, u32 layer) override {
		const UniquePtr<Vehicle>& veh = m_vehicles[entity];
		veh->chassis_layer = layer;
		if (veh->actor) {
			rebuildVehicle(entity, *veh.get());
		}
	}
	
	Vec3 getVehicleCenterOfMass(EntityRef entity) override {
		return m_vehicles[entity]->center_of_mass;
	}

	void setVehicleCenterOfMass(EntityRef entity, Vec3 center) override {
		UniquePtr<Vehicle>& veh = m_vehicles[entity];
		veh->center_of_mass = center;
		if (veh->actor) veh->actor->setCMassLocalPose(PxTransform(toPhysx(center), PxQuat(PxIdentity)));
	}

	float getVehicleMOIMultiplier(EntityRef entity) override {
		return m_vehicles[entity]->moi_multiplier;
	}

	void setVehicleMOIMultiplier(EntityRef entity, float m) override {
		UniquePtr<Vehicle>& veh = m_vehicles[entity];
		veh->moi_multiplier = m;
		if (veh->actor) {
			PxVec3 extents(1);
			if (veh->geom && veh->geom->convex_mesh) {
				const PxBounds3 bounds = veh->geom->convex_mesh->getLocalBounds();
				extents = bounds.getExtents();
			}
			veh->actor->setMassSpaceInertiaTensor(PxVec3(extents.x, extents.z, extents.y) * veh->mass * veh->moi_multiplier);
		}
	}

	float getVehicleMass(EntityRef entity) override {
		return m_vehicles[entity]->mass;
	}

	void setVehicleMass(EntityRef entity, float mass) override {
		UniquePtr<Vehicle>& veh = m_vehicles[entity];
		veh->mass = mass;
		if (veh->actor) veh->actor->setMass(mass);
	}

	Path getVehicleChassis(EntityRef entity) override {
		UniquePtr<Vehicle>& veh = m_vehicles[entity];
		return veh->geom ? veh->geom->getPath() : Path();
	}

	void setVehicleChassis(EntityRef entity, const Path& path) override {
		UniquePtr<Vehicle>& veh = m_vehicles[entity];
		ResourceManagerHub& manager = m_engine.getResourceManager();
		PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(path);

		if (veh->actor) {
			const i32 shape_count = veh->actor->getNbShapes();
			PxShape* shape;
			for (int i = 0; i < shape_count; ++i) {
				veh->actor->getShapes(&shape, 1, i);
				if (shape->getGeometryType() == physx::PxGeometryType::eCONVEXMESH ||
					shape->getGeometryType() == physx::PxGeometryType::eTRIANGLEMESH)
				{
					veh->actor->detachShape(*shape);
					break;
				}
			}
		}

		if (veh->geom) {
			veh->geom->getObserverCb().unbind<&Vehicle::onStateChanged>(veh.get());
			veh->geom->decRefCount();
		}
		veh->geom = geom_res;
		if (veh->geom) {
			veh->geom->onLoaded<&Vehicle::onStateChanged>(veh.get());
		}
	}
	
	void setVehicleAccel(EntityRef entity, float accel) override {
		if (accel < 0.0f && m_vehicles[entity]->drive->mDriveDynData.getCurrentGear() != PxVehicleGearsData::eREVERSE) {
			m_vehicles[entity]->drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eREVERSE);
		}
		else if (accel > 0.0f && m_vehicles[entity]->drive->mDriveDynData.getCurrentGear() == PxVehicleGearsData::eREVERSE) {
			
			m_vehicles[entity]->drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
		}

		m_vehicles[entity]->raw_input.setAnalogAccel(fabsf(accel));
	}

	void setVehicleSteer(EntityRef entity, float value) override {
		m_vehicles[entity]->raw_input.setAnalogSteer(value);
	}

	void setVehicleBrake(EntityRef entity, float value) override {
		m_vehicles[entity]->raw_input.setAnalogBrake(value);
	}

	float getVehicleRPM(EntityRef entity) override {
		if (!m_vehicles[entity]->drive) return 0;
		
		return m_vehicles[entity]->drive->mDriveDynData.getEngineRotationSpeed() * (60 / (PI * 2));
	}

	i32 getVehicleCurrentGear(EntityRef entity) override {
		if (!m_vehicles[entity]->drive) return 0;

		return m_vehicles[entity]->drive->mDriveDynData.getCurrentGear() - 1;
	}

	float getVehicleSpeed(EntityRef entity) override {
		if (!m_vehicles[entity]->drive) return 0.0f;

		return m_vehicles[entity]->drive->computeForwardSpeed();
	}

	float getVehiclePeakTorque(EntityRef entity) override {
		return m_vehicles[entity]->peak_torque;
	}

	void setVehiclePeakTorque(EntityRef entity, float value) override {
		Vehicle* veh = m_vehicles[entity].get();
		veh->peak_torque = value;
		if(veh->actor) rebuildVehicle(entity, *veh);
	}

	float getVehicleMaxRPM(EntityRef entity) override {
		return m_vehicles[entity]->max_rpm;
	}

	void setVehicleMaxRPM(EntityRef entity, float value) override {
		Vehicle* veh = m_vehicles[entity].get();
		veh->max_rpm = value;
		if(veh->actor) rebuildVehicle(entity, *veh);
	}

	void rebuildWheel(EntityRef entity)
	{
		if (!m_is_game_running) return;

		const EntityPtr veh_entity = m_world.getParent(entity);
		if (!veh_entity.isValid()) return;

		auto iter = m_vehicles.find((EntityRef)veh_entity);
		if (!iter.isValid()) return;

		rebuildVehicle(iter.key(), *iter.value().get());
	}

	u32 getHeightfieldLayer(EntityRef entity) override { return m_terrains[entity].m_layer; }

	void setHeightfieldLayer(EntityRef entity, u32 layer) override
	{
		ASSERT(layer < lengthOf(m_layers.names));
		auto& terrain = m_terrains[entity];
		terrain.m_layer = layer;

		if (terrain.m_actor)
		{
			PxFilterData data;
			data.word0 = 1 << layer;
			data.word1 = m_layers.filter[layer];
			PxShape* shapes[8];
			int shapes_count = terrain.m_actor->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}
	}


	void updateHeighfieldData(EntityRef entity,
		int x,
		int y,
		int width,
		int height,
		const u8* src_data,
		int bytes_per_pixel) override
	{
		PROFILE_FUNCTION();
		Heightfield& terrain = m_terrains[entity];

		PxShape* shape;
		terrain.m_actor->getShapes(&shape, 1);
		PxHeightFieldGeometry geom;
		shape->getHeightFieldGeometry(geom);

		Array<PxHeightFieldSample> heights(m_allocator);

		heights.resize(width * height);
		if (bytes_per_pixel == 2)
		{
			const i16* LUMIX_RESTRICT data = (const i16*)src_data;
			for (int j = 0; j < height; ++j)
			{
				for (int i = 0; i < width; ++i)
				{
					int idx = j + i * height;
					int idx2 = i + j * width;
					heights[idx].height = PxI16((i32)data[idx2] - 0x7fff);
					heights[idx].materialIndex0 = heights[idx].materialIndex1 = 0;
				}
			}
		}
		else
		{
			ASSERT(bytes_per_pixel == 1);
			const u8* LUMIX_RESTRICT data = src_data;
			for (int j = 0; j < height; ++j)
			{
				for (int i = 0; i < width; ++i)
				{
					int idx = j + i * height;
					int idx2 = i + j * width;
					heights[idx].height = PxI16((i32)data[idx2] - 0x7f);
					heights[idx].materialIndex0 = heights[idx].materialIndex1 = 0;
				}
			}
		}

		PxHeightFieldDesc hfDesc;
		hfDesc.format = PxHeightFieldFormat::eS16_TM;
		hfDesc.nbColumns = height;
		hfDesc.nbRows = width;
		hfDesc.samples.data = &heights[0];
		hfDesc.samples.stride = sizeof(PxHeightFieldSample);

		geom.heightField->modifySamples(y, x, hfDesc);
		shape->setGeometry(geom);
	}


	int getJointCount() override { return m_joints.size(); }
	EntityRef getJointEntity(int index) override { return {m_joints.getKey(index).index}; }


	PxDistanceJoint* getDistanceJoint(EntityRef entity)
	{
		return static_cast<PxDistanceJoint*>(m_joints[entity].physx);
	}


	Vec3 getDistanceJointLinearForce(EntityRef entity) override
	{
		PxVec3 linear, angular;
		getDistanceJoint(entity)->getConstraint()->getForce(linear, angular);
		return Vec3(linear.x, linear.y, linear.z);
	}


	float getDistanceJointDamping(EntityRef entity) override { return getDistanceJoint(entity)->getDamping(); }


	void setDistanceJointDamping(EntityRef entity, float value) override
	{
		getDistanceJoint(entity)->setDamping(value);
	}


	float getDistanceJointStiffness(EntityRef entity) override { return getDistanceJoint(entity)->getStiffness(); }


	void setDistanceJointStiffness(EntityRef entity, float value) override
	{
		getDistanceJoint(entity)->setStiffness(value);
	}


	float getDistanceJointTolerance(EntityRef entity) override { return getDistanceJoint(entity)->getTolerance(); }


	void setDistanceJointTolerance(EntityRef entity, float value) override
	{
		getDistanceJoint(entity)->setTolerance(value);
	}


	Vec2 getDistanceJointLimits(EntityRef entity) override
	{
		auto* joint = getDistanceJoint(entity);
		return {joint->getMinDistance(), joint->getMaxDistance()};
	}


	void setDistanceJointLimits(EntityRef entity, const Vec2& value) override
	{
		auto* joint = getDistanceJoint(entity);
		joint->setMinDistance(value.x);
		joint->setMaxDistance(value.y);
		joint->setDistanceJointFlag(PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, value.x > 0);
		joint->setDistanceJointFlag(PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, value.y > 0);
	}


	PxD6Joint* getD6Joint(EntityRef entity) { return static_cast<PxD6Joint*>(m_joints[entity].physx); }


	float getD6JointDamping(EntityRef entity) override { return getD6Joint(entity)->getLinearLimit().damping; }


	void setD6JointDamping(EntityRef entity, float value) override
	{
		PxD6Joint* joint = getD6Joint(entity);
		PxJointLinearLimit limit = joint->getLinearLimit();
		limit.damping = value;
		joint->setLinearLimit(limit);
	}


	float getD6JointStiffness(EntityRef entity) override { return getD6Joint(entity)->getLinearLimit().stiffness; }


	void setD6JointStiffness(EntityRef entity, float value) override
	{
		PxD6Joint* joint = getD6Joint(entity);
		PxJointLinearLimit limit = joint->getLinearLimit();
		limit.stiffness = value;
		joint->setLinearLimit(limit);
	}


	float getD6JointRestitution(EntityRef entity) override { return getD6Joint(entity)->getLinearLimit().restitution; }


	void setD6JointRestitution(EntityRef entity, float value) override
	{
		PxD6Joint* joint = getD6Joint(entity);
		PxJointLinearLimit limit = joint->getLinearLimit();
		limit.restitution = value;
		joint->setLinearLimit(limit);
	}


	Vec2 getD6JointTwistLimit(EntityRef entity) override
	{
		auto limit = getD6Joint(entity)->getTwistLimit();
		return {limit.lower, limit.upper};
	}


	void setD6JointTwistLimit(EntityRef entity, const Vec2& limit) override
	{
		auto* joint = getD6Joint(entity);
		auto px_limit = joint->getTwistLimit();
		px_limit.lower = limit.x;
		px_limit.upper = limit.y;
		joint->setTwistLimit(px_limit);
	}


	Vec2 getD6JointSwingLimit(EntityRef entity) override
	{
		auto limit = getD6Joint(entity)->getSwingLimit();
		return {limit.yAngle, limit.zAngle};
	}


	void setD6JointSwingLimit(EntityRef entity, const Vec2& limit) override
	{
		auto* joint = getD6Joint(entity);
		auto px_limit = joint->getSwingLimit();
		px_limit.yAngle = maximum(0.0f, limit.x);
		px_limit.zAngle = maximum(0.0f, limit.y);
		joint->setSwingLimit(px_limit);
	}


	D6Motion getD6JointXMotion(EntityRef entity) override
	{
		return (D6Motion)getD6Joint(entity)->getMotion(PxD6Axis::eX);
	}


	void setD6JointXMotion(EntityRef entity, D6Motion motion) override
	{
		getD6Joint(entity)->setMotion(PxD6Axis::eX, (PxD6Motion::Enum)motion);
	}


	D6Motion getD6JointYMotion(EntityRef entity) override
	{
		return (D6Motion)getD6Joint(entity)->getMotion(PxD6Axis::eY);
	}


	void setD6JointYMotion(EntityRef entity, D6Motion motion) override
	{
		getD6Joint(entity)->setMotion(PxD6Axis::eY, (PxD6Motion::Enum)motion);
	}


	D6Motion getD6JointSwing1Motion(EntityRef entity) override
	{
		return (D6Motion)getD6Joint(entity)->getMotion(PxD6Axis::eSWING1);
	}


	void setD6JointSwing1Motion(EntityRef entity, D6Motion motion) override
	{
		getD6Joint(entity)->setMotion(PxD6Axis::eSWING1, (PxD6Motion::Enum)motion);
	}


	D6Motion getD6JointSwing2Motion(EntityRef entity) override
	{
		return (D6Motion)getD6Joint(entity)->getMotion(PxD6Axis::eSWING2);
	}


	void setD6JointSwing2Motion(EntityRef entity, D6Motion motion) override
	{
		getD6Joint(entity)->setMotion(PxD6Axis::eSWING2, (PxD6Motion::Enum)motion);
	}


	D6Motion getD6JointTwistMotion(EntityRef entity) override
	{
		return (D6Motion)getD6Joint(entity)->getMotion(PxD6Axis::eTWIST);
	}


	void setD6JointTwistMotion(EntityRef entity, D6Motion motion) override
	{
		getD6Joint(entity)->setMotion(PxD6Axis::eTWIST, (PxD6Motion::Enum)motion);
	}


	D6Motion getD6JointZMotion(EntityRef entity) override
	{
		return (D6Motion)getD6Joint(entity)->getMotion(PxD6Axis::eZ);
	}


	void setD6JointZMotion(EntityRef entity, D6Motion motion) override
	{
		getD6Joint(entity)->setMotion(PxD6Axis::eZ, (PxD6Motion::Enum)motion);
	}


	float getD6JointLinearLimit(EntityRef entity) override { return getD6Joint(entity)->getLinearLimit().value; }


	void setD6JointLinearLimit(EntityRef entity, float limit) override
	{
		auto* joint = getD6Joint(entity);
		auto px_limit = joint->getLinearLimit();
		px_limit.value = limit;
		joint->setLinearLimit(px_limit);
	}


	EntityPtr getJointConnectedBody(EntityRef entity) override { return m_joints[entity].connected_body; }


	void setJointConnectedBody(EntityRef joint_entity, EntityPtr connected_body) override
	{
		int idx = m_joints.find(joint_entity);
		Joint& joint = m_joints.at(idx);
		joint.connected_body = connected_body;
		if (m_is_game_running) initJoint(joint_entity, joint);
	}


	void setJointAxisPosition(EntityRef entity, const Vec3& value) override
	{
		auto& joint = m_joints[entity];
		joint.local_frame0.p = toPhysx(value);
		joint.physx->setLocalPose(PxJointActorIndex::eACTOR0, joint.local_frame0);
	}


	void setJointAxisDirection(EntityRef entity, const Vec3& value) override
	{
		auto& joint = m_joints[entity];
		joint.local_frame0.q = toPhysx(Quat::vec3ToVec3(Vec3(1, 0, 0), value));
		joint.physx->setLocalPose(PxJointActorIndex::eACTOR0, joint.local_frame0);
	}


	Vec3 getJointAxisPosition(EntityRef entity) override { return fromPhysx(m_joints[entity].local_frame0.p); }


	Vec3 getJointAxisDirection(EntityRef entity) override
	{
		return fromPhysx(m_joints[entity].local_frame0.q.rotate(PxVec3(1, 0, 0)));
	}


	bool getSphericalJointUseLimit(EntityRef entity) override
	{
		return static_cast<PxSphericalJoint*>(m_joints[entity].physx)
			->getSphericalJointFlags()
			.isSet(PxSphericalJointFlag::eLIMIT_ENABLED);
	}


	void setSphericalJointUseLimit(EntityRef entity, bool use_limit) override
	{
		return static_cast<PxSphericalJoint*>(m_joints[entity].physx)
			->setSphericalJointFlag(PxSphericalJointFlag::eLIMIT_ENABLED, use_limit);
	}


	Vec2 getSphericalJointLimit(EntityRef entity) override
	{
		auto cone = static_cast<PxSphericalJoint*>(m_joints[entity].physx)->getLimitCone();
		return {cone.yAngle, cone.zAngle};
	}


	void setSphericalJointLimit(EntityRef entity, const Vec2& limit) override
	{
		auto* joint = static_cast<PxSphericalJoint*>(m_joints[entity].physx);
		auto limit_cone = joint->getLimitCone();
		limit_cone.yAngle = limit.x;
		limit_cone.zAngle = limit.y;
		joint->setLimitCone(limit_cone);
	}


	RigidTransform getJointLocalFrame(EntityRef entity) override { return fromPhysx(m_joints[entity].local_frame0); }


	PxJoint* getJoint(EntityRef entity) override { return m_joints[entity].physx; }


	RigidTransform getJointConnectedBodyLocalFrame(EntityRef entity) override
	{
		auto& joint = m_joints[entity];
		if (!joint.connected_body.isValid()) return {DVec3(0, 0, 0), Quat(0, 0, 0, 1)};

		PxRigidActor *a0, *a1;
		joint.physx->getActors(a0, a1);
		if (a1) return fromPhysx(joint.physx->getLocalPose(PxJointActorIndex::eACTOR1));

		Transform connected_body_tr = m_world.getTransform((EntityRef)joint.connected_body);
		RigidTransform unscaled_connected_body_tr = {connected_body_tr.pos, connected_body_tr.rot};
		Transform tr = m_world.getTransform(entity);
		RigidTransform unscaled_tr = {tr.pos, tr.rot};

		return unscaled_connected_body_tr.inverted() * unscaled_tr * fromPhysx(joint.local_frame0);
	}


	void setHingeJointUseLimit(EntityRef entity, bool use_limit) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		joint->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, use_limit);
	}


	bool getHingeJointUseLimit(EntityRef entity) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		return joint->getRevoluteJointFlags().isSet(PxRevoluteJointFlag::eLIMIT_ENABLED);
	}


	Vec2 getHingeJointLimit(EntityRef entity) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		PxJointAngularLimitPair limit = joint->getLimit();
		return {limit.lower, limit.upper};
	}


	void setHingeJointLimit(EntityRef entity, const Vec2& limit) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		PxJointAngularLimitPair px_limit = joint->getLimit();
		px_limit.lower = limit.x;
		px_limit.upper = limit.y;
		joint->setLimit(px_limit);
	}


	float getHingeJointDamping(EntityRef entity) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		return joint->getLimit().damping;
	}


	void setHingeJointDamping(EntityRef entity, float value) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		PxJointAngularLimitPair px_limit = joint->getLimit();
		px_limit.damping = value;
		joint->setLimit(px_limit);
	}


	float getHingeJointStiffness(EntityRef entity) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		return joint->getLimit().stiffness;
	}


	void setHingeJointStiffness(EntityRef entity, float value) override
	{
		auto* joint = static_cast<PxRevoluteJoint*>(m_joints[entity].physx);
		PxJointAngularLimitPair px_limit = joint->getLimit();
		px_limit.stiffness = value;
		joint->setLimit(px_limit);
	}


	void destroyHeightfield(EntityRef entity)
	{
		m_terrains.erase(entity);
		m_world.onComponentDestroyed(entity, HEIGHTFIELD_TYPE, this);
	}

	void destroyInstancedCube(EntityRef entity) {
		for (PxRigidActor* actor : m_instanced_cubes[entity].actors) {
			actor->release();
		}
		m_instanced_cubes.erase(entity);
		m_world.onComponentDestroyed(entity, INSTANCED_CUBE_TYPE, this);
	}

	void destroyInstancedMesh(EntityRef entity) {
		InstancedMesh& im = m_instanced_meshes[entity];
		for (PxRigidActor* actor : im.actors) {
			actor->release();
		}
		if (im.resource) im.resource->decRefCount();
		m_instanced_meshes.erase(entity);
		m_world.onComponentDestroyed(entity, INSTANCED_MESH_TYPE, this);
	}

	void destroyController(EntityRef entity)
	{
		m_controllers[entity].controller->release();
		m_controllers.erase(entity);
		m_world.onComponentDestroyed(entity, CONTROLLER_TYPE, this);
	}


	void destroyWheel(EntityRef entity)
	{
		m_wheels.erase(entity);
		m_world.onComponentDestroyed(entity, WHEEL_TYPE, this);

		// we do not support removing wheels at runtime
		// if you need this, you need to refresh physx part after removing the wheel
		ASSERT(!m_is_game_running);
	}


	void destroyVehicle(EntityRef entity) 
	{
		const UniquePtr<Vehicle>& veh = m_vehicles[entity];
		if (veh->actor) {
			m_scene->removeActor(*veh->actor);
			veh->actor->release();
		}
		if (veh->drive) veh->drive->free();
		if (veh->geom) {
			veh->geom->getObserverCb().unbind<&Vehicle::onStateChanged>(veh.get());
			veh->geom->decRefCount();
		}
		m_vehicles.erase(entity);
		m_world.onComponentDestroyed(entity, VEHICLE_TYPE, this);
	}


	void destroySphericalJoint(EntityRef entity) { destroyJointGeneric(entity, SPHERICAL_JOINT_TYPE); }
	void destroyHingeJoint(EntityRef entity) { destroyJointGeneric(entity, HINGE_JOINT_TYPE); }
	void destroyD6Joint(EntityRef entity) { destroyJointGeneric(entity, D6_JOINT_TYPE); }
	void destroyDistanceJoint(EntityRef entity) { destroyJointGeneric(entity, DISTANCE_JOINT_TYPE); }


	void destroyRigidActor(EntityRef entity)
	{
		RigidActor& actor = m_actors[entity];
		actor.setPhysxActor(nullptr);
		m_actors.erase(entity);
		m_dynamic_actors.eraseItem(entity);
		m_world.onComponentDestroyed(entity, RIGID_ACTOR_TYPE, this);
		if (m_is_game_running)
		{
			for (int i = 0, c = m_joints.size(); i < c; ++i)
			{
				Joint& joint = m_joints.at(i);
				if (m_joints.getKey(i) == entity || joint.connected_body == entity)
				{
					if (joint.physx) joint.physx->release();
					joint.physx = PxDistanceJointCreate(m_scene->getPhysics(),
						m_dummy_actor,
						PxTransform(PxIdentity),
						nullptr,
						PxTransform(PxIdentity));
				}
			}
		}
	}


	void destroyJointGeneric(EntityRef entity, ComponentType type)
	{
		auto& joint = m_joints[entity];
		if (joint.physx) joint.physx->release();
		m_joints.erase(entity);
		m_world.onComponentDestroyed(entity, type, this);
	}


	void createDistanceJoint(EntityRef entity)
	{
		if (m_joints.find(entity) >= 0) return;
		Joint& joint = m_joints.insert(entity);
		joint.connected_body = INVALID_ENTITY;
		joint.local_frame0.p = PxVec3(0, 0, 0);
		joint.local_frame0.q = PxQuat(0, 0, 0, 1);
		joint.physx = PxDistanceJointCreate(
			m_scene->getPhysics(), m_dummy_actor, PxTransform(PxIdentity), nullptr, PxTransform(PxIdentity));
		joint.physx->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
		static_cast<PxDistanceJoint*>(joint.physx)->setDistanceJointFlag(PxDistanceJointFlag::eSPRING_ENABLED, true);

		m_world.onComponentCreated(entity, DISTANCE_JOINT_TYPE, this);
	}


	void createSphericalJoint(EntityRef entity)
	{
		if (m_joints.find(entity) >= 0) return;
		Joint& joint = m_joints.insert(entity);
		joint.connected_body = INVALID_ENTITY;
		joint.local_frame0.p = PxVec3(0, 0, 0);
		joint.local_frame0.q = PxQuat(0, 0, 0, 1);
		joint.physx = PxSphericalJointCreate(
			m_scene->getPhysics(), m_dummy_actor, PxTransform(PxIdentity), nullptr, PxTransform(PxIdentity));
		joint.physx->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);

		m_world.onComponentCreated(entity, SPHERICAL_JOINT_TYPE, this);
	}


	void createD6Joint(EntityRef entity)
	{
		if (m_joints.find(entity) >= 0) return;
		Joint& joint = m_joints.insert(entity);
		joint.connected_body = INVALID_ENTITY;
		joint.local_frame0.p = PxVec3(0, 0, 0);
		joint.local_frame0.q = PxQuat(0, 0, 0, 1);
		joint.physx = PxD6JointCreate(
			m_scene->getPhysics(), m_dummy_actor, PxTransform(PxIdentity), nullptr, PxTransform(PxIdentity));
		auto* d6_joint = static_cast<PxD6Joint*>(joint.physx);
		auto linear_limit = d6_joint->getLinearLimit();
		linear_limit.value = 1.0f;
		d6_joint->setLinearLimit(linear_limit);
		joint.physx->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);

		m_world.onComponentCreated(entity, D6_JOINT_TYPE, this);
	}


	void createHingeJoint(EntityRef entity)
	{
		if (m_joints.find(entity) >= 0) return;
		Joint& joint = m_joints.insert(entity);
		joint.connected_body = INVALID_ENTITY;
		joint.local_frame0.p = PxVec3(0, 0, 0);
		joint.local_frame0.q = PxQuat(0, 0, 0, 1);
		joint.physx = PxRevoluteJointCreate(
			m_scene->getPhysics(), m_dummy_actor, PxTransform(PxIdentity), nullptr, PxTransform(PxIdentity));
		joint.physx->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);

		m_world.onComponentCreated(entity, HINGE_JOINT_TYPE, this);
	}


	void createHeightfield(EntityRef entity)
	{
		Heightfield& terrain = m_terrains.insert(entity);
		terrain.m_heightmap = nullptr;
		terrain.m_module = this;
		terrain.m_actor = nullptr;
		terrain.m_entity = entity;

		m_world.onComponentCreated(entity, HEIGHTFIELD_TYPE, this);
	}


	void initControllerDesc(PxCapsuleControllerDesc& desc)
	{
		static struct : PxControllerBehaviorCallback
		{
			PxControllerBehaviorFlags getBehaviorFlags(const PxShape& shape, const PxActor& actor) override
			{
				return PxControllerBehaviorFlag::eCCT_CAN_RIDE_ON_OBJECT | PxControllerBehaviorFlag::eCCT_SLIDE;
			}


			PxControllerBehaviorFlags getBehaviorFlags(const PxController& controller) override
			{
				return PxControllerBehaviorFlag::eCCT_CAN_RIDE_ON_OBJECT;
			}


			PxControllerBehaviorFlags getBehaviorFlags(const PxObstacle& obstacle) override
			{
				return PxControllerBehaviorFlag::eCCT_CAN_RIDE_ON_OBJECT;
			}
		} behaviour_cb;

		desc.material = m_default_material;
		desc.height = 1.8f;
		desc.radius = 0.25f;
		desc.slopeLimit = 0.0f;
		desc.contactOffset = 0.1f;
		desc.stepOffset = 0.02f;
		desc.behaviorCallback = &behaviour_cb;
		desc.reportCallback = &m_hit_report;
	}

	void createInstancedMesh(EntityRef entity) {
		InstancedMesh im(m_allocator);
		m_instanced_meshes.insert(entity, static_cast<InstancedMesh&&>(im));
		m_world.onComponentCreated(entity, INSTANCED_MESH_TYPE, this);
	}

	void createInstancedCube(EntityRef entity) {
		InstancedCube ic(m_allocator);
		ic.half_extents = Vec3(1);
		m_instanced_cubes.insert(entity, static_cast<InstancedCube&&>(ic));
		m_world.onComponentCreated(entity, INSTANCED_CUBE_TYPE, this);
	} 

	void createController(EntityRef entity)
	{
		PxCapsuleControllerDesc cDesc;
		initControllerDesc(cDesc);
		DVec3 position = m_world.getPosition(entity);
		cDesc.position.set(position.x, position.y, position.z);
		Controller& c = m_controllers.insert(entity);
		c.controller = m_controller_manager->createController(cDesc);
		c.controller->getActor()->userData = (void*)(uintptr)entity.index;
		c.entity = entity;
		c.frame_change = Vec3(0, 0, 0);
		c.radius = cDesc.radius;
		c.height = cDesc.height;
		c.custom_gravity = false;
		c.custom_gravity_acceleration = 9.8f;
		c.layer = 0;

		PxFilterData data;
		int controller_layer = c.layer;
		data.word0 = 1 << controller_layer;
		data.word1 = m_layers.filter[controller_layer];
		c.filter_data = data;
		PxShape* shapes[8];
		int shapes_count = c.controller->getActor()->getShapes(shapes, lengthOf(shapes));
		c.controller->getActor()->userData = (void*)(intptr_t)entity.index;
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}

		m_world.onComponentCreated(entity, CONTROLLER_TYPE, this);
	}

	void createWheel(EntityRef entity)
	{
		m_wheels.insert(entity, {});

		m_world.onComponentCreated(entity, WHEEL_TYPE, this);
	}


	void createVehicle(EntityRef entity)
	{
		m_vehicles.insert(entity, UniquePtr<Vehicle>::create(m_allocator));
		m_world.onComponentCreated(entity, VEHICLE_TYPE, this);
	}


	void createRigidActor(EntityRef entity)
	{
		if (m_actors.find(entity).isValid()) {
			logError("Entity ", entity.index, " already has rigid actor");
			return;
		}
		RigidActor actor(*this, entity);

		Transform transform = m_world.getTransform(entity);
		PxTransform px_transform = toPhysx(transform.getRigidPart());

		PxRigidStatic* physx_actor = m_system->getPhysics()->createRigidStatic(px_transform);
		actor.setPhysxActor(physx_actor);

		m_actors.insert(entity, static_cast<RigidActor&&>(actor));
		m_world.onComponentCreated(entity, RIGID_ACTOR_TYPE, this);
	}


	Path getHeightmapSource(EntityRef entity) override
	{
		auto& terrain = m_terrains[entity];
		return terrain.m_heightmap ? terrain.m_heightmap->getPath() : Path("");
	}


	float getHeightmapXZScale(EntityRef entity) override { return m_terrains[entity].m_xz_scale; }


	void setHeightmapXZScale(EntityRef entity, float scale) override
	{
		if (scale == 0) return;
		auto& terrain = m_terrains[entity];
		if (scale != terrain.m_xz_scale)
		{
			terrain.m_xz_scale = scale;
			if (terrain.m_heightmap && terrain.m_heightmap->isReady())
			{
				heightmapLoaded(terrain);
			}
		}
	}


	float getHeightmapYScale(EntityRef entity) override { return m_terrains[entity].m_y_scale; }


	void setHeightmapYScale(EntityRef entity, float scale) override
	{
		if (scale == 0) return;
		auto& terrain = m_terrains[entity];
		if (scale != terrain.m_y_scale)
		{
			terrain.m_y_scale = scale;
			if (terrain.m_heightmap && terrain.m_heightmap->isReady())
			{
				heightmapLoaded(terrain);
			}
		}
	}


	void setHeightmapSource(EntityRef entity, const Path& str) override
	{
		ResourceManagerHub& resource_manager = m_engine.getResourceManager();
		Heightfield& terrain = m_terrains[entity];
		Texture* old_hm = terrain.m_heightmap;
		if (old_hm) {
			old_hm->getObserverCb().unbind<&Heightfield::heightmapLoaded>(&terrain);
			old_hm->decRefCount();
		}

		if (str.isEmpty()) {
			terrain.m_heightmap = nullptr;
		} else {
			auto* new_hm = resource_manager.load<Texture>(str);
			terrain.m_heightmap = new_hm;
			new_hm->onLoaded<&Heightfield::heightmapLoaded>(&terrain);
			new_hm->addDataReference();
		}
	}


	bool isActorDebugEnabled(EntityRef entity) const override
	{
		auto* px_actor = m_actors[entity].physx_actor;
		if (!px_actor) return false;
		return px_actor->getActorFlags().isSet(PxActorFlag::eVISUALIZATION);
	}


	void enableActorDebug(EntityRef entity, bool enable) const override
	{
		auto* px_actor = m_actors[entity].physx_actor;
		if (!px_actor) return;
		px_actor->setActorFlag(PxActorFlag::eVISUALIZATION, enable);
		PxShape* shape;
		px_actor->getShapes(&shape, 1);
		if(shape) {
			shape->setFlag(PxShapeFlag::eVISUALIZATION, enable);
		}
	}


	void render() override {
		auto* render_module = static_cast<RenderModule*>(m_world.getModule("renderer"));
		if (!render_module) return;

		const PxRenderBuffer& rb = m_scene->getRenderBuffer();
		const PxU32 num_lines = minimum(100000U, rb.getNbLines());
		if (num_lines) {
			const PxDebugLine* PX_RESTRICT lines = rb.getLines();
			DebugLine* tmp = render_module->addDebugLines(num_lines);
			for (PxU32 i = 0; i < num_lines; ++i)
			{
				const PxDebugLine& line = lines[i];
				tmp[i].from = DVec3(fromPhysx(line.pos0));
				tmp[i].to = DVec3(fromPhysx(line.pos1));
				tmp[i].color = line.color0;
			}
		}
		const PxU32 num_tris = rb.getNbTriangles();
		if (num_tris) {
			const PxDebugTriangle* PX_RESTRICT tris = rb.getTriangles();
			DebugTriangle* tmp = render_module->addDebugTriangles(num_tris);
			for (PxU32 i = 0; i < num_tris; ++i)
			{
				const PxDebugTriangle& tri = tris[i];
				tmp[i].p0 = DVec3(fromPhysx(tri.pos0));
				tmp[i].p1 = DVec3(fromPhysx(tri.pos1));
				tmp[i].p2 = DVec3(fromPhysx(tri.pos2));
				tmp[i].color = tri.color0;
			}
		}
	}


	void updateDynamicActors(bool vehicles)
	{
		PROFILE_FUNCTION();
		for (EntityRef e : m_dynamic_actors)
		{
			RigidActor& actor = m_actors[e];
			m_update_in_progress = &actor;
			PxTransform trans = actor.physx_actor->getGlobalPose();
			m_world.setTransform(actor.entity, fromPhysx(trans));
		}
		m_update_in_progress = nullptr;

		if (!vehicles) return;

		for (auto iter = m_vehicles.begin(), end = m_vehicles.end(); iter != end; ++iter) {
			Vehicle* veh = iter.value().get();
			if (veh->actor) {
				const PxTransform car_trans = veh->actor->getGlobalPose();
				m_world.setTransform(iter.key(), fromPhysx(car_trans));

				EntityPtr wheels[4];
				getWheels(iter.key(), Span(wheels));

				PxShape* shapes[5];
				veh->actor->getShapes(shapes, 5);
				for (u32 i = 0; i < 4; ++i) {
					if (!wheels[i].isValid()) continue;
					const PxTransform trans = shapes[i]->getLocalPose();
					m_world.setTransform((EntityRef)wheels[i], fromPhysx(car_trans * trans));
				
				}

			}
		}
	}


	void simulateScene(float time_delta)
	{
		PROFILE_FUNCTION();
		m_scene->simulate(time_delta);
	}


	void fetchResults()
	{
		PROFILE_FUNCTION();
		m_scene->fetchResults(true);
	}


	void updateControllers(float time_delta)
	{
		PROFILE_FUNCTION();
		for (auto& controller : m_controllers)
		{
			Vec3 dif = controller.frame_change;
			controller.frame_change = Vec3(0, 0, 0);

			PxControllerState state;
			controller.controller->getState(state);
			float gravity_acceleration = 0.0f;
			if (controller.custom_gravity)
			{
				gravity_acceleration = controller.custom_gravity_acceleration * -1.0f;
			}
			else
			{
				gravity_acceleration = m_scene->getGravity().y;
			}

			bool apply_gravity = (state.collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) == 0;
			if (apply_gravity)
			{
				dif.y += controller.gravity_speed * time_delta;
				controller.gravity_speed += time_delta * gravity_acceleration;
			}
			else
			{
				controller.gravity_speed = 0;
			}

			if (squaredLength(dif) > 0.00001f) {
				m_filter_callback.m_filter_data = controller.filter_data;
				PxControllerFilters filters(nullptr, &m_filter_callback);
				controller.controller->move(toPhysx(dif), 0.001f, time_delta, filters);
				PxExtendedVec3 p = controller.controller->getFootPosition();

				m_world.setPosition(controller.entity, {p.x, p.y, p.z});
			}
		}
	}

	void updateVehicles(float time_delta) {
		PxVehicleWheels* vehicles[16];

		u32 valid_count = 0;
		for (auto iter = m_vehicles.begin(), end = m_vehicles.end(); iter != end; ++iter) {
			Vehicle* veh = iter.value().get();
			if (veh->drive) {
				vehicles[valid_count] = veh->drive;
				PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(pad_smoothing, steer_vs_forward_speed, veh->raw_input, time_delta, false, *veh->drive);
				++valid_count;

				if (valid_count == lengthOf(vehicles)) {
					PxVehicleSuspensionRaycasts(m_vehicle_batch_query, valid_count, vehicles, valid_count * 4, m_vehicle_results);
					PxVehicleUpdates(time_delta, m_scene->getGravity(), *m_vehicle_frictions, valid_count, vehicles, nullptr);
					valid_count = 0;
				}
			}
		}

		if (valid_count > 0) {
			PxVehicleSuspensionRaycasts(m_vehicle_batch_query, valid_count, vehicles, valid_count * 4, m_vehicle_results);
			PxVehicleUpdates(time_delta, m_scene->getGravity(), *m_vehicle_frictions, valid_count, vehicles, nullptr);
		}
	}

	void lateUpdate(float time_delta) override {
		if (!m_is_game_running) return;

		AnimationModule* anim_module = (AnimationModule*)m_world.getModule("animation");
		if (!anim_module) return;

		for (Controller& ctrl : m_controllers) {
			if (ctrl.use_root_motion) {
				const LocalRigidTransform tr = anim_module->getAnimatorRootMotion(ctrl.entity);
				const Quat rot = m_world.getRotation(ctrl.entity);
				ctrl.frame_change += rot.rotate(tr.pos);
				m_world.setRotation(ctrl.entity, rot * tr.rot);
			}
		}
	}
	
	const Array<EntityRef>& getDynamicActors() override { return m_dynamic_actors; }

	void forceUpdateDynamicActors(float time_delta) override {
		simulateScene(time_delta);
		fetchResults();
		updateDynamicActors(false);
	}

	void update(float time_delta) override
	{
		if (!m_is_game_running) return;

		time_delta = minimum(1 / 20.0f, time_delta);
		updateVehicles(time_delta);
		simulateScene(time_delta);
		fetchResults();
		updateDynamicActors(true);
		updateControllers(time_delta);

		render();
	}


	DelegateList<void(const ContactData&)>& onContact() override { return m_contact_callbacks; }


	void initJoint(EntityRef entity, Joint& joint)
	{
		PxRigidActor* actors[2] = {nullptr, nullptr};
		auto iter = m_actors.find(entity);
		if (iter.isValid()) actors[0] = iter.value().physx_actor;
		iter = joint.connected_body.isValid() ? m_actors.find((EntityRef)joint.connected_body) : m_actors.end();
		if (iter.isValid()) actors[1] = iter.value().physx_actor;
		if (!actors[0] || !actors[1]) return;

		DVec3 pos0 = m_world.getPosition(entity);
		Quat rot0 = m_world.getRotation(entity);
		DVec3 pos1 = m_world.getPosition((EntityRef)joint.connected_body);
		Quat rot1 = m_world.getRotation((EntityRef)joint.connected_body);
		PxTransform entity0_frame(toPhysx(pos0), toPhysx(rot0));
		PxTransform entity1_frame(toPhysx(pos1), toPhysx(rot1));

		PxTransform axis_local_frame1 = entity1_frame.getInverse() * entity0_frame * joint.local_frame0;

		joint.physx->setLocalPose(PxJointActorIndex::eACTOR0, joint.local_frame0);
		joint.physx->setLocalPose(PxJointActorIndex::eACTOR1, axis_local_frame1);
		joint.physx->setActors(actors[0], actors[1]);
		joint.physx->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
	}


	// from physx docs
	PxVehicleWheelsSimData* setupWheelsSimulationData(EntityRef entity, const Vehicle& vehicle) const {
		u8 mask = 0;
		PxVehicleWheelsSimData* wheel_sim_data = PxVehicleWheelsSimData::allocate(4);
		PxVehicleSuspensionData suspensions[PX_MAX_NB_WHEELS];
		PxVehicleWheelData wheels[PX_MAX_NB_WHEELS];
		PxVec3 offsets[4];
		const Transform& chassis_tr = m_world.getTransform(entity);
		const PxF32 camber_angle_at_rest = 0.0;
		const PxF32 camber_angle_at_max_droop = 0.01f;
		const PxF32 camber_angle_at_max_compression = -0.01f;

		wheels[PxVehicleDrive4WWheelOrder::eREAR_LEFT].mMaxHandBrakeTorque = 4000.0f;
		wheels[PxVehicleDrive4WWheelOrder::eREAR_RIGHT].mMaxHandBrakeTorque = 4000.0f;
		wheels[PxVehicleDrive4WWheelOrder::eFRONT_LEFT].mMaxSteer = PxPi * 0.3333f;
		wheels[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT].mMaxSteer = PxPi * 0.3333f;

		for (EntityRef e : m_world.childrenOf(entity)) {
			if (!m_world.hasComponent(e, WHEEL_TYPE)) continue;

			const Wheel& w = m_wheels[e];
			const u32 idx = (u32)w.slot;
			mask |= 1 << idx;

			suspensions[idx].mMaxCompression = w.max_compression;
			suspensions[idx].mMaxDroop = w.max_droop;
			suspensions[idx].mSpringStrength = w.spring_strength;
			suspensions[idx].mSpringDamperRate = w.spring_damper_rate;

			PxVehicleTireData tire;
			enum { TIRE_TYPE_NORMAL = 0 };
			tire.mType = TIRE_TYPE_NORMAL;

			wheels[idx].mMass = w.mass;
			wheels[idx].mMOI = w.moi;
			wheels[idx].mRadius = w.radius;
			wheels[idx].mWidth = w.width;

			const Transform& wheel_tr = m_world.getTransform((EntityRef)e);
			offsets[idx] = toPhysx((chassis_tr.inverted() * wheel_tr).pos - vehicle.center_of_mass);
			
			wheel_sim_data->setTireData(idx, tire);
			wheel_sim_data->setSuspTravelDirection(idx, PxVec3(0, -1, 0));
			wheel_sim_data->setWheelCentreOffset(idx, offsets[idx]);
			wheel_sim_data->setSuspForceAppPointOffset(idx, offsets[idx] + PxVec3(0, 0.1f, 0));
			wheel_sim_data->setTireForceAppPointOffset(idx, offsets[idx] + PxVec3(0, 0.1f, 0));
			wheel_sim_data->setWheelShapeMapping(idx, idx);

			physx::PxFilterData filter;
			filter.word0 = 1 << vehicle.wheels_layer;
			filter.word1 = m_layers.filter[vehicle.wheels_layer];
			filter.word2 = 0;
			filter.word3 = (u32)FilterFlags::VEHICLE;
			wheel_sim_data->setSceneQueryFilterData(idx, filter);
		}

		if (mask != 0b1111) {
			logError("Vehicle ", entity.index, " does not have a wheel in each slot.");
			wheel_sim_data->free();
			return nullptr;
		}

		PxF32 susp_sprung_masses[PX_MAX_NB_WHEELS];
		PxVehicleComputeSprungMasses(4, offsets, PxVec3(0), m_vehicles[entity]->mass, 1, susp_sprung_masses);

		for (u32 i = 0; i < 4; ++i) {
			suspensions[i].mSprungMass = susp_sprung_masses[i];
		}

		for (PxU32 i = 0; i < 4; i += 2) {
			suspensions[i + 0].mCamberAtRest = camber_angle_at_rest;
			suspensions[i + 1].mCamberAtRest = -camber_angle_at_rest;
			suspensions[i + 0].mCamberAtMaxDroop = camber_angle_at_max_droop;
			suspensions[i + 1].mCamberAtMaxDroop = -camber_angle_at_max_droop;
			suspensions[i + 0].mCamberAtMaxCompression = camber_angle_at_max_compression;
			suspensions[i + 1].mCamberAtMaxCompression = -camber_angle_at_max_compression;
		}

		for (PxU32 i = 0; i < 4; i++) {
			wheel_sim_data->setWheelData(i, wheels[i]);
			wheel_sim_data->setSuspensionData(i, suspensions[i]);
		}

		return wheel_sim_data;
	}

	static void setupDriveSimData(const PxVehicleWheelsSimData& wheel_sim_data, PxVehicleDriveSimData4W& drive_sim_data, const Vehicle& vehicle) {
		PxVehicleDifferential4WData diff;
		diff.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
		drive_sim_data.setDiffData(diff);

		PxVehicleEngineData engine;
		engine.mPeakTorque = vehicle.peak_torque;
		engine.mMaxOmega = vehicle.max_rpm * 2 * PI / 60;
		drive_sim_data.setEngineData(engine);

		PxVehicleGearsData gears;
		gears.mSwitchTime = 0.5f;
		drive_sim_data.setGearsData(gears);

		PxVehicleClutchData clutch;
		clutch.mStrength = 10.0f;
		drive_sim_data.setClutchData(clutch);

		PxVehicleAckermannGeometryData ackermann;
		ackermann.mAccuracy = 1.0f;
		ackermann.mAxleSeparation =
			fabsf(wheel_sim_data.getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eFRONT_LEFT).z - wheel_sim_data.getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eREAR_LEFT).z);
		ackermann.mFrontWidth =
			wheel_sim_data.getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eFRONT_RIGHT).x -
			wheel_sim_data.getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eFRONT_LEFT).x;
		ackermann.mRearWidth =
			wheel_sim_data.getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eREAR_RIGHT).x -
			wheel_sim_data.getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eREAR_LEFT).x;
		drive_sim_data.setAckermannGeometryData(ackermann);
	}

	PxRigidDynamic* createVehicleActor(const RigidTransform& transform, Span<const EntityRef> wheels_entities, Vehicle& vehicle) {
		PxPhysics& physics = *m_system->getPhysics();
		PxCooking& cooking = *m_system->getCooking();

		RigidTransform wheel_transforms[4];
		getTransforms(Span(wheels_entities), Span(wheel_transforms));

		PxRigidDynamic* actor = physics.createRigidDynamic(toPhysx(transform));

		for (int i = 0; i < 4; i++) {
			const Wheel& w = m_wheels[wheels_entities[i]];
			PxConvexMesh* wheel_mesh = createWheelMesh(w.width, w.radius, physics, cooking);
			PxConvexMeshGeometry geom(wheel_mesh);
			PxShape* wheel_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
			physx::PxFilterData filter;
			filter.word0 = 1 << vehicle.wheels_layer;
			filter.word1 = m_layers.filter[vehicle.wheels_layer];
			filter.word2 = 0;
			filter.word3 = (u32)FilterFlags::VEHICLE;
			wheel_shape->setQueryFilterData(filter);
			wheel_shape->setSimulationFilterData(filter);
			wheel_shape->setLocalPose(toPhysx(transform.inverted() * wheel_transforms[i]));
		}

		PxVec3 extents(1, 1, 1);
		if (vehicle.geom && vehicle.geom->isReady()) {
			physx::PxFilterData filter;
			filter.word0 = 1 << vehicle.chassis_layer;
			filter.word1 = m_layers.filter[vehicle.chassis_layer];
			filter.word2 = 0;
			filter.word3 = (u32)FilterFlags::VEHICLE;
			PxMeshScale pxscale(1.f);
			PxConvexMeshGeometry convex_geom(vehicle.geom->convex_mesh, pxscale);
			// TODO what if there's no geom or it's not ready
			PxShape* chassis_shape = PxRigidActorExt::createExclusiveShape(*actor, convex_geom, *m_default_material);
			const PxBounds3 bounds = vehicle.geom->convex_mesh->getLocalBounds();
			extents = bounds.getExtents();
			chassis_shape->setQueryFilterData(filter);
			chassis_shape->setSimulationFilterData(filter);
			chassis_shape->setLocalPose(PxTransform(PxIdentity));
		}

		actor->setMass(vehicle.mass);
		actor->setMassSpaceInertiaTensor(PxVec3(extents.x, extents.z, extents.y) * vehicle.mass * vehicle.moi_multiplier);
		actor->setCMassLocalPose(PxTransform(toPhysx(vehicle.center_of_mass), PxQuat(PxIdentity)));

		return actor;
	}


	void rebuildVehicle(EntityRef entity, Vehicle& vehicle) {
		if (vehicle.actor) {
			m_scene->removeActor(*vehicle.actor);
			vehicle.actor->release();
		}

		PxVehicleWheelsSimData* wheel_sim_data = setupWheelsSimulationData(entity, vehicle);
		if (!wheel_sim_data) {
			logError("Failed to init vehicle ", entity.index);
			return;
		}

		PxVehicleDriveSimData4W drive_sim_data;
		setupDriveSimData(*wheel_sim_data, drive_sim_data, vehicle);

		const RigidTransform tr = m_world.getTransform(entity).getRigidPart();

		EntityPtr wheels_ptr[4];
		getWheels(entity, Span(wheels_ptr));
		
		EntityRef wheels[4];
		wheels[0] = (EntityRef)wheels_ptr[0];
		wheels[1] = (EntityRef)wheels_ptr[1];
		wheels[2] = (EntityRef)wheels_ptr[2];
		wheels[3] = (EntityRef)wheels_ptr[3];

		vehicle.actor = createVehicleActor(tr, Span(wheels), vehicle);
		m_scene->addActor(*vehicle.actor);

		vehicle.drive = PxVehicleDrive4W::allocate(4);
		vehicle.drive->setup(m_system->getPhysics(), vehicle.actor, *wheel_sim_data, drive_sim_data, 0);
		vehicle.drive->mDriveDynData.setUseAutoGears(true);
			
		wheel_sim_data->free();
	}


	static PxConvexMesh* createConvexMesh(const PxVec3* verts, const PxU32 numVerts, PxPhysics& physics, PxCooking& cooking)
	{
		PxConvexMeshDesc convexDesc;
		convexDesc.points.count = numVerts;
		convexDesc.points.stride = sizeof(PxVec3);
		convexDesc.points.data = verts;
		convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;

		PxConvexMesh* convexMesh = NULL;
		PxDefaultMemoryOutputStream buf;
		if (cooking.cookConvexMesh(convexDesc, buf))
		{
			PxDefaultMemoryInputData id(buf.getData(), buf.getSize());
			convexMesh = physics.createConvexMesh(id);
		}

		return convexMesh;
	}


	static PxConvexMesh* createWheelMesh(const PxF32 width, const PxF32 radius, PxPhysics& physics, PxCooking& cooking)
	{
		PxVec3 points[2 * 16];
		for (PxU32 i = 0; i < 16; i++)
		{
			const PxF32 cosTheta = PxCos(i*PxPi*2.0f / 16.0f);
			const PxF32 sinTheta = PxSin(i*PxPi*2.0f / 16.0f);
			const PxF32 y = radius * cosTheta;
			const PxF32 z = radius * sinTheta;
			points[2 * i + 0] = PxVec3(-width / 2.0f, y, z);
			points[2 * i + 1] = PxVec3(+width / 2.0f, y, z);
		}

		return createConvexMesh(points, 32, physics, cooking);
	}

	void getWheels(EntityRef car, Span<EntityPtr> wheels) {
		for (EntityPtr& e : wheels) e = INVALID_ENTITY;
		for (EntityPtr e : m_world.childrenOf(car)) {
			if (m_world.hasComponent((EntityRef)e, WHEEL_TYPE)) {
				const Wheel& w = m_wheels[(EntityRef)e];
				wheels[(i32)w.slot] = e;
			}
		}
	}

	void getTransforms(Span<const EntityRef> entities, Span<RigidTransform> transforms) {
		for (u32 i = 0; i < entities.length(); ++i) {
			transforms[i] = m_world.getTransform(entities[i]).getRigidPart();
		}
	}

	void initInstancedCubes() {
		PROFILE_FUNCTION();
		RenderModule* rs = (RenderModule*)m_world.getModule(INSTANCED_MODEL_TYPE);
		if (!rs) return;

		for (auto iter = m_instanced_cubes.begin(), end = m_instanced_cubes.end(); iter != end; ++iter) {
			if (!m_world.hasComponent(iter.key(), INSTANCED_MODEL_TYPE)) continue;
			
			const InstancedModel& im = rs->getInstancedModels()[iter.key()];
			InstancedCube& ic = iter.value();
			
			const RigidTransform tr = m_world.getTransform(iter.key()).getRigidPart();

			ic.actors.reserve(im.instances.size());
			for (const InstancedModel::InstanceData& id : im.instances) {
				PxBoxGeometry geom;
				geom.halfExtents = toPhysx(ic.half_extents * id.scale);
				RigidTransform inst_tr = tr;
				inst_tr.pos += id.pos;
				Quat irot(id.rot_quat.x, id.rot_quat.y, id.rot_quat.z, 0);
				irot.w = sqrtf(1 - dot(id.rot_quat, id.rot_quat));
				inst_tr.rot = inst_tr.rot * irot;
				PxTransform transform = toPhysx(inst_tr);
				PxRigidActor* actor = PxCreateStatic(*m_system->getPhysics(), transform, geom, *m_default_material);
				actor->userData = (void*)(intptr_t)iter.key().index;
				m_scene->addActor(*actor);
				ic.actors.push(actor);
			}
		}
	}

	void initInstancedMeshes() {
		PROFILE_FUNCTION();
		RenderModule* rs = (RenderModule*)m_world.getModule(INSTANCED_MODEL_TYPE);
		if (!rs) return;

		for (auto iter = m_instanced_meshes.begin(), end = m_instanced_meshes.end(); iter != end; ++iter) {
			if (!m_world.hasComponent(iter.key(), INSTANCED_MODEL_TYPE)) continue;
			
			const InstancedModel& im = rs->getInstancedModels()[iter.key()];
			InstancedMesh& mesh = iter.value();
			if (!mesh.resource) continue;
			if (!mesh.resource->isReady()) continue;
			
			const RigidTransform tr = m_world.getTransform(iter.key()).getRigidPart();

			mesh.actors.reserve(im.instances.size());
			
			for (const InstancedModel::InstanceData& id : im.instances) {
				RigidTransform inst_tr = tr;
				inst_tr.pos += id.pos;
				Quat irot(id.rot_quat.x, id.rot_quat.y, id.rot_quat.z, 0);
				irot.w = sqrtf(1 - dot(id.rot_quat, id.rot_quat));
				inst_tr.rot = inst_tr.rot * irot;
				const PxTransform px_transform = toPhysx(inst_tr);

				PxRigidStatic* physx_actor = m_system->getPhysics()->createRigidStatic(px_transform);
				physx_actor->userData = (void*)(uintptr)iter.key().index;
				
				PxMeshScale pxscale(id.scale);
				PxConvexMeshGeometry convex_geom(mesh.resource->convex_mesh, pxscale);
				PxTriangleMeshGeometry tri_geom(mesh.resource->tri_mesh, pxscale);
				const PxGeometry* geom = mesh.resource->convex_mesh ? static_cast<PxGeometry*>(&convex_geom) : static_cast<PxGeometry*>(&tri_geom);
				PxShape* shape = PxRigidActorExt::createExclusiveShape(*physx_actor, *geom, *m_default_material);
				shape->userData = (void*)(uintptr)iter.key().index;

				m_scene->addActor(*physx_actor);
				mesh.actors.push(physx_actor);
			}
		}
	}

	void initVehicles()
	{
		for (auto iter = m_vehicles.begin(), end = m_vehicles.end(); iter != end; ++iter) {
			rebuildVehicle(iter.key(), *iter.value().get());
		}
	}


	void initJoints()
	{
		for (int i = 0, c = m_joints.size(); i < c; ++i)
		{
			Joint& joint = m_joints.at(i);
			EntityRef entity = m_joints.getKey(i);
			initJoint(entity, joint);
		}
	}


	void startGame() override
	{
		auto* module = m_world.getModule("lua_script");
		m_script_module = static_cast<LuaScriptModule*>(module);
		m_is_game_running = true;

		initJoints();
		initVehicles();
		initInstancedCubes();
		initInstancedMeshes();

		updateFilterData();
	}


	void stopGame() override { m_is_game_running = false; }


	float getControllerRadius(EntityRef entity) override { return m_controllers[entity].radius; }
	float getControllerHeight(EntityRef entity) override { return m_controllers[entity].height; }
	bool getControllerCustomGravity(EntityRef entity) override { return m_controllers[entity].custom_gravity; }
	float getControllerCustomGravityAcceleration(EntityRef entity) override
	{
		return m_controllers[entity].custom_gravity_acceleration;
	}


	void setControllerRadius(EntityRef entity, float value) override
	{
		if (value <= 0) return;

		Controller& ctrl = m_controllers[entity];
		ctrl.radius = value;

		PxRigidActor* actor = ctrl.controller->getActor();
		PxShape* shapes;
		if (actor->getNbShapes() == 1 && actor->getShapes(&shapes, 1))
		{
			PxCapsuleGeometry capsule;
			bool is_capsule = shapes->getCapsuleGeometry(capsule);
			ASSERT(is_capsule);
			capsule.radius = value;
			shapes->setGeometry(capsule);
		}
	}


	void setControllerHeight(EntityRef entity, float value) override
	{
		if (value <= 0) return;

		Controller& ctrl = m_controllers[entity];
		ctrl.height = value;

		PxRigidActor* actor = ctrl.controller->getActor();
		PxShape* shapes;
		if (actor->getNbShapes() == 1 && actor->getShapes(&shapes, 1))
		{
			PxCapsuleGeometry capsule;
			bool is_capsule = shapes->getCapsuleGeometry(capsule);
			ASSERT(is_capsule);
			capsule.halfHeight = value * 0.5f;
			shapes->setGeometry(capsule);
		}
	}

	void setControllerCustomGravity(EntityRef entity, bool value) override
	{
		Controller& ctrl = m_controllers[entity];
		ctrl.custom_gravity = value;
	}

	void setControllerCustomGravityAcceleration(EntityRef entity, float value) override
	{
		Controller& ctrl = m_controllers[entity];
		ctrl.custom_gravity_acceleration = value;
	}

	bool isControllerCollisionDown(EntityRef entity) const override
	{
		const Controller& ctrl = m_controllers[entity];
		PxControllerState state;
		ctrl.controller->getState(state);
		return (state.collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) != 0;
	}
	
	bool getControllerUseRootMotion(EntityRef entity) override {
		return m_controllers[entity].use_root_motion;
	}

	void setControllerUseRootMotion(EntityRef entity, bool enable) override {
		m_controllers[entity].use_root_motion = enable;
	}

	void resizeController(EntityRef entity, float height) override
	{
		Controller& ctrl = m_controllers[entity];
		ctrl.height = height;
		ctrl.controller->resize(height);
	}


	void addForceAtPos(EntityRef entity, const Vec3& force, const Vec3& pos) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;

		RigidActor& actor = iter.value();
		if (!actor.physx_actor) return;

		PxRigidBody* rigid_body = actor.physx_actor->is<PxRigidBody>();
		if (!rigid_body) return;

		PxRigidBodyExt::addForceAtPos(*rigid_body, toPhysx(force), toPhysx(pos));
	}


	void moveController(EntityRef entity, const Vec3& v) override { m_controllers[entity].frame_change += v; }


	EntityPtr raycast(const Vec3& origin, const Vec3& dir, EntityPtr ignore_entity) override
	{
		RaycastHit hit;
		if (raycastEx(origin, dir, FLT_MAX, hit, ignore_entity, -1)) return hit.entity;
		return INVALID_ENTITY;
	}

	struct Filter : PxQueryFilterCallback
	{
		bool canLayersCollide(int layer1, int layer2) const { return (module->m_layers.filter[layer1] & (1 << layer2)) != 0; }

		PxQueryHitType::Enum preFilter(const PxFilterData& filterData,
			const PxShape* shape,
			const PxRigidActor* actor,
			PxHitFlags& queryFlags) override
		{
			if (layer >= 0)
			{
				const EntityRef hit_entity = {(int)(intptr_t)actor->userData};
				const auto iter = module->m_actors.find(hit_entity);
				if (iter.isValid())
				{
					const RigidActor& actor = iter.value();
					if (!canLayersCollide(actor.layer, layer)) return PxQueryHitType::eNONE;
				}
			}
			if (entity.index == (int)(intptr_t)actor->userData) return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		}


		PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override
		{
			return PxQueryHitType::eBLOCK;
		}

		EntityPtr entity;
		int layer;
		PhysicsModuleImpl* module;
	};


	bool raycastEx(const Vec3& origin,
		const Vec3& dir,
		float distance,
		RaycastHit& result,
		EntityPtr ignored,
		int layer) override
	{
		PxVec3 physx_origin(origin.x, origin.y, origin.z);
		PxVec3 unit_dir(dir.x, dir.y, dir.z);
		PxReal max_distance = distance;

		const PxHitFlags flags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL;
		PxRaycastBuffer hit;

		Filter filter;
		filter.entity = ignored;
		filter.layer = layer;
		filter.module = this;
		PxQueryFilterData filter_data;
		filter_data.flags = PxQueryFlag::eDYNAMIC | PxQueryFlag::eSTATIC | PxQueryFlag::ePREFILTER;
		bool status = m_scene->raycast(physx_origin, unit_dir, max_distance, hit, flags, filter_data, &filter);
		result.normal.x = hit.block.normal.x;
		result.normal.y = hit.block.normal.y;
		result.normal.z = hit.block.normal.z;
		result.position.x = hit.block.position.x;
		result.position.y = hit.block.position.y;
		result.position.z = hit.block.position.z;
		result.entity = INVALID_ENTITY;
		if (hit.block.shape)
		{
			PxRigidActor* actor = hit.block.shape->getActor();
			if (actor) result.entity = EntityPtr{(int)(intptr_t)actor->userData};
		}
		return status;
	}

	void onEntityDestroyed(EntityRef entity)
	{
		for (int i = 0, c = m_joints.size(); i < c; ++i)
		{
			if (m_joints.at(i).connected_body == entity)
			{
				setJointConnectedBody({m_joints.getKey(i).index}, INVALID_ENTITY);
			}
		}
	}

	void onEntityMoved(EntityRef entity)
	{
		const u64 cmp_mask = m_world.getComponentsMask(entity);
		if ((cmp_mask & m_physics_cmps_mask) == 0) return;
		
		if (m_world.hasComponent(entity, CONTROLLER_TYPE)) {
			auto iter = m_controllers.find(entity);
			if (iter.isValid())
			{
				Controller& controller = iter.value();
				DVec3 pos = m_world.getPosition(entity);
				PxExtendedVec3 pvec(pos.x, pos.y, pos.z);
				controller.controller->setFootPosition(pvec);
			}
		}

		if (m_world.hasComponent(entity, RIGID_ACTOR_TYPE)) {
			auto iter = m_actors.find(entity);
			if (iter.isValid()) {
				RigidActor& actor = iter.value();
				if (actor.physx_actor && m_update_in_progress != &actor)
				{
					Transform trans = m_world.getTransform(entity);
					if (actor.dynamic_type == DynamicType::KINEMATIC)
					{
						auto* rigid_dynamic = (PxRigidDynamic*)actor.physx_actor;
						rigid_dynamic->setKinematicTarget(toPhysx(trans.getRigidPart()));
					}
					else
					{
						actor.physx_actor->setGlobalPose(toPhysx(trans.getRigidPart()), false);
					}
					if (actor.mesh && (actor.scale != trans.scale))
					{
						actor.rescale();
					}
				}
			}
		}
	}


	void heightmapLoaded(Heightfield& terrain)
	{
		PROFILE_FUNCTION();
		Array<PxHeightFieldSample> heights(m_allocator);

		int width = terrain.m_heightmap->width;
		int height = terrain.m_heightmap->height;
		heights.resize(width * height);
		PxHeightFieldSample* heights_ptr = heights.begin();
		if (terrain.m_heightmap->format == gpu::TextureFormat::R16)
		{
			PROFILE_BLOCK("copyData");
			const i16* LUMIX_RESTRICT data = (const i16*)terrain.m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				int idx = j * width;
				for (int i = 0; i < width; ++i)
				{
					int idx2 = j + i * height;
					heights_ptr[idx].height = PxI16((i32)data[idx2] - 0x7fff);
					heights_ptr[idx].materialIndex0 = heights_ptr[idx].materialIndex1 = 0;
					heights_ptr[idx].setTessFlag();
					++idx;
				}
			}
		}
		else if (terrain.m_heightmap->format == gpu::TextureFormat::R8)
		{
			PROFILE_BLOCK("copyData");
			const u8* data = terrain.m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				for (int i = 0; i < width; ++i)
				{
					int idx = i + j * width;
					int idx2 = j + i * height;
					heights_ptr[idx].height = PxI16((i32)data[idx2] - 0x7f);
					heights_ptr[idx].materialIndex0 = heights_ptr[idx].materialIndex1 = 0;
					heights_ptr[idx].setTessFlag();
				}
			}
		}
		else {
			logError("Unsupported physics heightmap format ", terrain.m_heightmap->getPath());
			return;
		}

		{ // PROFILE_BLOCK scope
			PROFILE_BLOCK("physX");
			PxHeightFieldDesc hfDesc;
			hfDesc.format = PxHeightFieldFormat::eS16_TM;
			hfDesc.nbColumns = width;
			hfDesc.nbRows = height;
			hfDesc.samples.data = &heights[0];
			hfDesc.samples.stride = sizeof(PxHeightFieldSample);

			PxHeightField* heightfield = m_system->getCooking()->createHeightField(
				hfDesc, m_system->getPhysics()->getPhysicsInsertionCallback());
			float height_scale = terrain.m_heightmap->format == gpu::TextureFormat::R16 ? 1 / (256 * 256.0f - 1) : 1 / 255.0f;
			PxHeightFieldGeometry hfGeom(heightfield,
				PxMeshGeometryFlags(),
				height_scale * terrain.m_y_scale,
				terrain.m_xz_scale,
				terrain.m_xz_scale);
			if (terrain.m_actor)
			{
				PxRigidActor* actor = terrain.m_actor;
				m_scene->removeActor(*actor);
				actor->release();
				terrain.m_actor = nullptr;
			}

			PxTransform transform = toPhysx(m_world.getTransform(terrain.m_entity).getRigidPart());
			transform.p.y += terrain.m_y_scale * 0.5f;

			PxRigidActor* actor = PxCreateStatic(*m_system->getPhysics(), transform, hfGeom, *m_default_material);
			if (actor)
			{
				actor->userData = (void*)(intptr_t)terrain.m_entity.index;
				m_scene->addActor(*actor);
				terrain.m_actor = actor;

				PxFilterData data;
				int terrain_layer = terrain.m_layer;
				data.word0 = 1 << terrain_layer;
				data.word1 = m_layers.filter[terrain_layer];
				PxShape* shapes[8];
				int shapes_count = actor->getShapes(shapes, lengthOf(shapes));
				for (int i = 0; i < shapes_count; ++i)
				{
					shapes[i]->setSimulationFilterData(data);
				}
				terrain.m_actor->setActorFlag(PxActorFlag::eVISUALIZATION, true);
			}
			else
			{
				logError("Could not create PhysX heightfield ", terrain.m_heightmap->getPath());
			}
		}
	}


	void updateFilterData(PxRigidActor* actor, int layer)
	{
		PxFilterData data;
		data.word0 = 1 << layer;
		data.word1 = m_layers.filter[layer];
		PxShape* shapes[64];
		int shapes_count = actor->getShapes(shapes, lengthOf(shapes));
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}
	}


	void updateFilterData()
	{
		for (const RigidActor& actor : m_actors)
		{
			if (!actor.physx_actor) continue;
			PxFilterData data;
			int actor_layer = actor.layer;
			data.word0 = 1 << actor_layer;
			data.word1 = m_layers.filter[actor_layer];
			PxShape* shapes[64];
			int shapes_count = actor.physx_actor->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}

		for (auto& controller : m_controllers)
		{
			PxFilterData data;
			int controller_layer = controller.layer;
			data.word0 = 1 << controller_layer;
			data.word1 = m_layers.filter[controller_layer];
			controller.filter_data = data;
			PxShape* shapes[8];
			int shapes_count = controller.controller->getActor()->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
			controller.controller->invalidateCache();
		}

		for (auto& instanced_cube : m_instanced_cubes) {
			PxFilterData data;
			data.word0 = 1 << instanced_cube.layer;
			data.word1 = m_layers.filter[instanced_cube.layer];
			for (PxRigidActor* actor : instanced_cube.actors) {
				PxShape* shapes[1];
				const i32 shapes_count = actor->getShapes(shapes, lengthOf(shapes));
				for (int i = 0; i < shapes_count; ++i) {
					shapes[i]->setSimulationFilterData(data);
				}
			}
		}

		for (auto& instanced_mesh : m_instanced_meshes) {
			PxFilterData data;
			data.word0 = 1 << instanced_mesh.layer;
			data.word1 = m_layers.filter[instanced_mesh.layer];
			for (PxRigidActor* actor : instanced_mesh.actors) {
				PxShape* shapes[1];
				const i32 shapes_count = actor->getShapes(shapes, lengthOf(shapes));
				for (int i = 0; i < shapes_count; ++i) {
					shapes[i]->setSimulationFilterData(data);
				}
			}
		}

		for (auto& terrain : m_terrains)
		{
			if (!terrain.m_actor) continue;

			PxFilterData data;
			int terrain_layer = terrain.m_layer;
			data.word0 = 1 << terrain_layer;
			data.word1 = m_layers.filter[terrain_layer];
			PxShape* shapes[8];
			int shapes_count = terrain.m_actor->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}
	}


	bool getIsTrigger(EntityRef entity) override { return m_actors[entity].is_trigger; }


	void setIsTrigger(EntityRef entity, bool is_trigger) override
	{
		RigidActor& actor = m_actors[entity];
		actor.setIsTrigger(is_trigger);
	}


	DynamicType getDynamicType(EntityRef entity) override { return m_actors[entity].dynamic_type; }


	void moveShapeIndices(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxRigidActor* actor = m_actors[entity].physx_actor;
		int count = getGeometryCount(actor, type);
		for (int i = index; i < count; ++i)
		{
			PxShape* shape = getShape(entity, i, type);
			shape->userData = (void*)(intptr_t)(i + 1);
		}
	}


	void addBoxGeometry(EntityRef entity, int index) override
	{
		if (index == -1) index = getBoxGeometryCount(entity);
		moveShapeIndices(entity, index, PxGeometryType::eBOX);
		PhysicsMaterial* mat = m_actors[entity].material;
		PxRigidActor* actor = m_actors[entity].physx_actor;
		PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*actor, geom, mat ? *mat->material : *m_default_material);
		shape->userData = (void*)(intptr_t)index;
	}


	void removeGeometry(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxRigidActor* actor = m_actors[entity].physx_actor;
		int count = getGeometryCount(actor, type);
		
		PxShape* shape = getShape(entity, index, type);
		actor->detachShape(*shape);

		for (int i = index + 1; i < count; ++i)
		{
			PxShape* s = getShape(entity, i, type);
			s->userData = (void*)(intptr_t)(i - 1);
		}
	}


	void removeBoxGeometry(EntityRef entity, int index) override
	{
		removeGeometry(entity, index, PxGeometryType::eBOX);
	}


	Vec3 getBoxGeomHalfExtents(EntityRef entity, int index) override
	{
		PxShape* shape = getShape(entity, index, PxGeometryType::eBOX);
		PxBoxGeometry box = shape->getGeometry().box();
		return fromPhysx(box.halfExtents);
	}


	PxShape* getShape(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxRigidActor* actor = m_actors[entity].physx_actor;
		int shape_count = actor->getNbShapes();
		PxShape* shape;
		for (int i = 0; i < shape_count; ++i)
		{
			actor->getShapes(&shape, 1, i);
			if (shape->getGeometryType() == type)
			{
				if (shape->userData == (void*)(intptr_t)index)
				{
					return shape;
				}
			}
		}
		ASSERT(false);
		return nullptr;
	}


	void setBoxGeomHalfExtents(EntityRef entity, int index, const Vec3& size) override
	{
		PxShape* shape = getShape(entity, index, PxGeometryType::eBOX);
		PxBoxGeometry box = shape->getGeometry().box();
		box.halfExtents = toPhysx(size);
		shape->setGeometry(box);
	}


	Vec3 getGeomOffsetPosition(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxShape* shape = getShape(entity, index, type);
		PxTransform tr = shape->getLocalPose();
		return fromPhysx(tr.p);
	}


	Quat getGeomOffsetRotation(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxShape* shape = getShape(entity, index, type);
		PxTransform tr = shape->getLocalPose();
		return fromPhysx(tr.q);
	}

	Quat getBoxGeomOffsetRotationQuat(EntityRef entity, int index) override {
		return getGeomOffsetRotation(entity, index, PxGeometryType::eBOX);
	}

	Vec3 getBoxGeomOffsetRotation(EntityRef entity, int index) override
	{
		return getGeomOffsetRotation(entity, index, PxGeometryType::eBOX).toEuler();
	}


	Vec3 getBoxGeomOffsetPosition(EntityRef entity, int index) override
	{
		return getGeomOffsetPosition(entity, index, PxGeometryType::eBOX);
	}


	void setGeomOffsetPosition(EntityRef entity, int index, const Vec3& pos, PxGeometryType::Enum type)
	{
		PxShape* shape = getShape(entity, index, type);
		PxTransform tr = shape->getLocalPose();
		tr.p = toPhysx(pos);
		shape->setLocalPose(tr);
	}


	void setGeomOffsetRotation(EntityRef entity, int index, const Vec3& rot, PxGeometryType::Enum type)
	{
		PxShape* shape = getShape(entity, index, type);
		PxTransform tr = shape->getLocalPose();
		Quat q;
		q.fromEuler(rot);
		tr.q = toPhysx(q);
		shape->setLocalPose(tr);
	}


	void setBoxGeomOffsetPosition(EntityRef entity, int index, const Vec3& pos) override
	{
		setGeomOffsetPosition(entity, index, pos, PxGeometryType::eBOX);
	}


	void setBoxGeomOffsetRotation(EntityRef entity, int index, const Vec3& rot) override
	{
		setGeomOffsetRotation(entity, index, rot, PxGeometryType::eBOX);
	}


	int getGeometryCount(PxRigidActor* actor, PxGeometryType::Enum type)
	{
		int shape_count = actor->getNbShapes();
		PxShape* shape;
		int count = 0;
		for (int i = 0; i < shape_count; ++i)
		{
			actor->getShapes(&shape, 1, i);
			if (shape->getGeometryType() == type) ++count;
		}
		return count;
	}


	int getBoxGeometryCount(EntityRef entity) override 
	{ 
		PxRigidActor* actor = m_actors[entity].physx_actor;
		return getGeometryCount(actor, PxGeometryType::eBOX);
	}

	Path getMeshGeomPath(EntityRef entity) override {
		RigidActor& actor = m_actors[entity];
		return actor.mesh ? actor.mesh->getPath() : Path();
	}
	
	void setMeshGeomPath(EntityRef entity, const Path& path) override {
		ResourceManagerHub& manager = m_engine.getResourceManager();
		PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(path);
		m_actors[entity].setMesh(geom_res);
	}
	
	void setRigidActorMaterial(EntityRef entity, const Path& path) override {
		PxShape* shapes[64];
		const u32 shapes_count = m_actors[entity].physx_actor->getShapes(shapes, lengthOf(shapes));

		ResourceManagerHub& manager = m_engine.getResourceManager();
		if (path.isEmpty()) {
			m_actors[entity].material = nullptr;
			for (u32 i = 0; i < shapes_count; ++i) {
				shapes[i]->setMaterials(&m_default_material, 1);
			}
		}
		else {
			PhysicsMaterial* mat = manager.load<PhysicsMaterial>(path);
			m_actors[entity].material = mat;
		
			for (u32 i = 0; i < shapes_count; ++i) {
				shapes[i]->setMaterials(&mat->material, 1);
			}
		}
	}

	Path getRigidActorMaterial(EntityRef entity) override {
		RigidActor& actor = m_actors[entity];
		return actor.material ? actor.material->getPath() : Path();
	}

	void addSphereGeometry(EntityRef entity, int index) override
	{
		if (index == -1) index = getSphereGeometryCount(entity);
		moveShapeIndices(entity, index, PxGeometryType::eSPHERE);
		PxRigidActor* actor = m_actors[entity].physx_actor;
		PxSphereGeometry geom;
		geom.radius = 1;
		PhysicsMaterial* mat = m_actors[entity].material;
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*actor, geom, mat ? *mat->material : *m_default_material);
		shape->userData = (void*)(intptr_t)index;
	}


	void removeSphereGeometry(EntityRef entity, int index) override
	{
		removeGeometry(entity, index, PxGeometryType::eSPHERE);
	}


	int getSphereGeometryCount(EntityRef entity) override 
	{ 
		PxRigidActor* actor = m_actors[entity].physx_actor;
		return getGeometryCount(actor, PxGeometryType::eSPHERE);
	}


	float getSphereGeomRadius(EntityRef entity, int index) override
	{
		PxShape* shape = getShape(entity, index, PxGeometryType::eSPHERE);
		PxSphereGeometry geom = shape->getGeometry().sphere();
		return geom.radius;
	}


	void setSphereGeomRadius(EntityRef entity, int index, float radius) override
	{
		PxShape* shape = getShape(entity, index, PxGeometryType::eSPHERE);
		PxSphereGeometry geom = shape->getGeometry().sphere();
		geom.radius = radius;
		shape->setGeometry(geom);
	}


	Vec3 getSphereGeomOffsetPosition(EntityRef entity, int index) override
	{
		return getGeomOffsetPosition(entity, index, PxGeometryType::eSPHERE);
	}


	void setSphereGeomOffsetPosition(EntityRef entity, int index, const Vec3& pos) override
	{
		setGeomOffsetPosition(entity, index, pos, PxGeometryType::eSPHERE);
	}


	void setDynamicType(EntityRef entity, DynamicType new_value) override
	{
		RigidActor& actor = m_actors[entity];
		if (actor.dynamic_type == new_value) return;

		actor.dynamic_type = new_value;
		if (new_value == DynamicType::DYNAMIC) {
			m_dynamic_actors.push(entity);
		}
		else {
			m_dynamic_actors.swapAndPopItem(entity);
		}
		if (!actor.physx_actor) return;

		PxTransform transform = toPhysx(m_world.getTransform(actor.entity).getRigidPart());
		PxRigidActor* new_physx_actor;
		switch (actor.dynamic_type)
		{
			case DynamicType::DYNAMIC: new_physx_actor = m_system->getPhysics()->createRigidDynamic(transform); break;
			case DynamicType::KINEMATIC:
				new_physx_actor = m_system->getPhysics()->createRigidDynamic(transform);
				new_physx_actor->is<PxRigidBody>()->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
				break;
			case DynamicType::STATIC: new_physx_actor = m_system->getPhysics()->createRigidStatic(transform); break;
		}
		for (int i = 0, c = actor.physx_actor->getNbShapes(); i < c; ++i)
		{
			PxShape* shape;
			actor.physx_actor->getShapes(&shape, 1, i);
			duplicateShape(shape, new_physx_actor, actor.material ? actor.material->material : m_default_material);
		}
		PxRigidBody* rigid_body = new_physx_actor->is<PxRigidBody>();
		if (rigid_body) {
			PxRigidBodyExt::updateMassAndInertia(*rigid_body, 1);
		}
		actor.setPhysxActor(new_physx_actor);
	}


	void duplicateShape(PxShape* shape, PxRigidActor* actor, physx::PxMaterial* material)
	{
		PxShape* new_shape;
		switch (shape->getGeometryType())
		{
			case PxGeometryType::eBOX:
			{
				PxBoxGeometry geom;
				shape->getBoxGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			case PxGeometryType::eSPHERE:
			{
				PxSphereGeometry geom;
				shape->getSphereGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			case PxGeometryType::eCONVEXMESH:
			{
				PxConvexMeshGeometry geom;
				shape->getConvexMeshGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			case PxGeometryType::eTRIANGLEMESH:
			{
				PxTriangleMeshGeometry geom;
				shape->getTriangleMeshGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			default: ASSERT(false); return;
		}
		new_shape->userData = shape->userData;
	}


	void serializeActor(OutputMemoryStream& serializer, const RigidActor& actor)
	{
		serializer.write(actor.entity);
		serializer.write(actor.dynamic_type);
		serializer.write(actor.is_trigger);
		serializer.write(actor.layer);
		serializer.writeString(actor.material ? actor.material->getPath().c_str() : "");
		auto* px_actor = actor.physx_actor;
		PxShape* shape;
		int shape_count = px_actor->getNbShapes();
		serializer.writeString(actor.mesh ? actor.mesh->getPath().c_str() : "");
		serializer.write(shape_count);
		for (int i = 0; i < shape_count; ++i)
		{
			px_actor->getShapes(&shape, 1, i);
			int type = shape->getGeometryType();
			serializer.write(type);
			serializer.write((int)(intptr_t)shape->userData);
			RigidTransform tr = fromPhysx(shape->getLocalPose());
			serializer.write(tr);
			switch (type)
			{
				case PxGeometryType::eBOX: {
					PxBoxGeometry geom;
					shape->getBoxGeometry(geom);
					serializer.write(geom.halfExtents.x);
					serializer.write(geom.halfExtents.y);
					serializer.write(geom.halfExtents.z);
					break;
				}
				case PxGeometryType::eSPHERE: {
					PxSphereGeometry geom;
					shape->getSphereGeometry(geom);
					serializer.write(geom.radius);
					break;
				}
				case PxGeometryType::eCONVEXMESH:
				case PxGeometryType::eTRIANGLEMESH:
					break;
				default: ASSERT(false); break;
			}
		}
	}


	void serialize(OutputMemoryStream& serializer) override
	{
		serializer.write((i32)m_actors.size());
		for (const RigidActor& actor : m_actors)
		{
			serializeActor(serializer, actor);
		}
		serializer.write((i32)m_controllers.size());
		for (const auto& controller : m_controllers)
		{
			serializer.write(controller.entity);
			serializer.write(controller.layer);
			serializer.write(controller.radius);
			serializer.write(controller.height);
			serializer.write(controller.custom_gravity);
			serializer.write(controller.custom_gravity_acceleration);
			serializer.write(controller.use_root_motion);
		}
		serializer.write((i32)m_terrains.size());
		for (auto& terrain : m_terrains)
		{
			serializer.write(terrain.m_entity);
			serializer.writeString(terrain.m_heightmap ? terrain.m_heightmap->getPath().c_str() : "");
			serializer.write(terrain.m_xz_scale);
			serializer.write(terrain.m_y_scale);
			serializer.write(terrain.m_layer);
		}
		
		serializer.write((i32)m_instanced_cubes.size());
		for (auto iter = m_instanced_cubes.begin(), end = m_instanced_cubes.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			serializer.write(iter.value().half_extents);
			serializer.write(iter.value().layer);
		}

		serializer.write((i32)m_instanced_meshes.size());
		for (auto iter = m_instanced_meshes.begin(), end = m_instanced_meshes.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			serializer.writeString(iter.value().resource ? iter.value().resource->getPath().c_str() : "");
			serializer.write(iter.value().layer);
		}

		serializeJoints(serializer);
		serializeVehicles(serializer);
	}


	void serializeVehicles(OutputMemoryStream& serializer)
	{
		serializer.write(m_vehicles.size());
		for (auto iter = m_vehicles.begin(), end = m_vehicles.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			const UniquePtr<Vehicle>& veh = iter.value();
			serializer.write(veh->mass);
			serializer.write(veh->center_of_mass);
			serializer.write(veh->moi_multiplier);
			serializer.write(veh->chassis_layer);
			serializer.write(veh->wheels_layer);
			serializer.write(veh->peak_torque);
			serializer.write(veh->max_rpm);
			serializer.writeString(veh->geom ? veh->geom->getPath().c_str() : "");
		}

		serializer.write(m_wheels.size());
		for (auto iter = m_wheels.begin(), end = m_wheels.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			const Wheel& w = iter.value();
			serializer.write(w);
		}
	}


	void serializeJoints(OutputMemoryStream& serializer)
	{
		serializer.write(m_joints.size());
		for (int i = 0; i < m_joints.size(); ++i)
		{
			const Joint& joint = m_joints.at(i);
			serializer.write(m_joints.getKey(i));
			serializer.write((int)joint.physx->getConcreteType());
			serializer.write(joint.connected_body);
			serializer.write(joint.local_frame0);
			switch ((PxJointConcreteType::Enum)joint.physx->getConcreteType())
			{
				case PxJointConcreteType::eSPHERICAL:
				{
					auto* px_joint = static_cast<PxSphericalJoint*>(joint.physx);
					u32 flags = (u32)px_joint->getSphericalJointFlags();
					serializer.write(flags);
					auto limit = px_joint->getLimitCone();
					serializer.write(limit);
					break;
				}
				case PxJointConcreteType::eREVOLUTE:
				{
					auto* px_joint = static_cast<PxRevoluteJoint*>(joint.physx);
					u32 flags = (u32)px_joint->getRevoluteJointFlags();
					serializer.write(flags);
					auto limit = px_joint->getLimit();
					serializer.write(limit);
					break;
				}
				case PxJointConcreteType::eDISTANCE:
				{
					auto* px_joint = static_cast<PxDistanceJoint*>(joint.physx);
					u32 flags = (u32)px_joint->getDistanceJointFlags();
					serializer.write(flags);
					serializer.write(px_joint->getDamping());
					serializer.write(px_joint->getStiffness());
					serializer.write(px_joint->getTolerance());
					serializer.write(px_joint->getMinDistance());
					serializer.write(px_joint->getMaxDistance());
					break;
				}
				case PxJointConcreteType::eD6:
				{
					auto* px_joint = static_cast<PxD6Joint*>(joint.physx);
					serializer.write(px_joint->getMotion(PxD6Axis::eX));
					serializer.write(px_joint->getMotion(PxD6Axis::eY));
					serializer.write(px_joint->getMotion(PxD6Axis::eZ));
					serializer.write(px_joint->getMotion(PxD6Axis::eSWING1));
					serializer.write(px_joint->getMotion(PxD6Axis::eSWING2));
					serializer.write(px_joint->getMotion(PxD6Axis::eTWIST));
					serializer.write(px_joint->getLinearLimit());
					serializer.write(px_joint->getSwingLimit());
					serializer.write(px_joint->getTwistLimit());
					break;
				}
				default: ASSERT(false); break;
			}
		}
	}


	void deserializeActors(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version)
	{
		PROFILE_FUNCTION();
		u32 count;
		serializer.read(count);
		m_actors.reserve(count + m_actors.size());

		for (u32 j = 0; j < count; ++j) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			RigidActor actor(*this, entity);
			serializer.read(actor.dynamic_type);
			serializer.read(actor.is_trigger);
			if (actor.dynamic_type == DynamicType::DYNAMIC) m_dynamic_actors.push(entity);
			actor.layer = 0;
			serializer.read(actor.layer);
			
			const char* material_path = "";
			if (version > (i32)PhysicsModuleVersion::MATERIAL) material_path = serializer.readString();
			const char* mesh_path = serializer.readString();

			PxTransform transform = toPhysx(m_world.getTransform(actor.entity).getRigidPart());
			PxRigidActor* physx_actor = actor.dynamic_type == DynamicType::STATIC
				? (PxRigidActor*)m_system->getPhysics()->createRigidStatic(transform)
				: (PxRigidActor*)m_system->getPhysics()->createRigidDynamic(transform);
			if (actor.dynamic_type == DynamicType::KINEMATIC) {
				physx_actor->is<PxRigidBody>()->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
			}

			PhysicsMaterial* material = nullptr;
			if (material_path[0]) {
				ResourceManagerHub& manager = m_engine.getResourceManager();
				material = manager.load<PhysicsMaterial>(Path(material_path));
				actor.material = material;
			}

			PxFilterData filter_data;
			filter_data.word0 = 1 << actor.layer;
			filter_data.word1 = m_layers.filter[actor.layer];

			int geoms_count = serializer.read<int>();
			for (int i = 0; i < geoms_count; ++i) {
				int type = serializer.read<int>();
				int index = serializer.read<int>();
				PxTransform tr = toPhysx(serializer.read<RigidTransform>());
				PxShape* shape = nullptr;
				switch (type) {
					case PxGeometryType::eBOX: {
						PxBoxGeometry box_geom;
						serializer.read(box_geom.halfExtents.x);
						serializer.read(box_geom.halfExtents.y);
						serializer.read(box_geom.halfExtents.z);

						shape = PxRigidActorExt::createExclusiveShape(*physx_actor, box_geom, material ? *material->material : *m_default_material);
						shape->setLocalPose(tr);
						break;
					}
					case PxGeometryType::eSPHERE: {
						PxSphereGeometry geom;
						serializer.read(geom.radius);
						shape = PxRigidActorExt::createExclusiveShape(*physx_actor, geom, material ? *material->material : *m_default_material);
						shape->setLocalPose(tr);
						break;
					}
					case PxGeometryType::eCONVEXMESH:
					case PxGeometryType::eTRIANGLEMESH: break;
					default: ASSERT(false); break;
				}
				if (shape) {
					shape->userData = (void*)(intptr_t)index;
					shape->setSimulationFilterData(filter_data);

					if (actor.is_trigger) {
						shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false); // must set false first
						shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
					}
				}
			}
			actor.setPhysxActor(physx_actor);
			m_actors.insert(entity, static_cast<RigidActor&&>(actor));
			
			if (mesh_path[0]) {
				ResourceManagerHub& manager = m_engine.getResourceManager();
				PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(Path(mesh_path));
				m_actors[entity].setMesh(geom_res);
			}

			m_world.onComponentCreated(entity, RIGID_ACTOR_TYPE, this);
		}
	}


	void deserializeControllers(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		for (u32 ctrl_idx = 0; ctrl_idx < count; ++ctrl_idx) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			Controller& c = m_controllers.insert(entity);
			c.frame_change = Vec3(0, 0, 0);

			serializer.read(c.layer);
			serializer.read(c.radius);
			serializer.read(c.height);
			serializer.read(c.custom_gravity);
			serializer.read(c.custom_gravity_acceleration);
			serializer.read(c.use_root_motion);
			PxCapsuleControllerDesc cDesc;
			initControllerDesc(cDesc);
			cDesc.height = c.height;
			cDesc.radius = c.radius;
			DVec3 position = m_world.getPosition(entity);
			cDesc.position.set(position.x, position.y - cDesc.height * 0.5f, position.z);
			c.controller = m_controller_manager->createController(cDesc);
			c.controller->getActor()->userData = (void*)(intptr_t)entity.index;
			c.entity = entity;
			c.controller->setFootPosition({position.x, position.y, position.z});

			PxFilterData data;
			data.word0 = 1 << c.layer;
			data.word1 = m_layers.filter[c.layer];
			c.filter_data = data;
			PxShape* shapes[8];
			const u32 shapes_count = c.controller->getActor()->getShapes(shapes, lengthOf(shapes));
			for (u32 i = 0; i < shapes_count; ++i) shapes[i]->setSimulationFilterData(data);
			c.controller->invalidateCache();

			m_world.onComponentCreated(entity, CONTROLLER_TYPE, this);
		}
	}

	void deserializeVehicles(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version)
	{
		const u32 vehicles_count = serializer.read<u32>();
		m_vehicles.reserve(vehicles_count + m_vehicles.size());
		Array<EntityRef> tmp(m_allocator);
		for (u32 i = 0; i < vehicles_count; ++i) {
			EntityRef e = serializer.read<EntityRef>();
			e = entity_map.get(e);
			auto iter = m_vehicles.insert(e, UniquePtr<Vehicle>::create(m_allocator));
			serializer.read(iter.value()->mass);
			serializer.read(iter.value()->center_of_mass);
			serializer.read(iter.value()->moi_multiplier);
			serializer.read(iter.value()->chassis_layer);
			serializer.read(iter.value()->wheels_layer);
			if (version > (i32)PhysicsModuleVersion::VEHICLE_PEAK_TORQUE) serializer.read(iter.value()->peak_torque);
			if (version > (i32)PhysicsModuleVersion::VEHICLE_MAX_RPM) serializer.read(iter.value()->max_rpm);
			const char* path = serializer.readString();
			if (path[0]) {
				ResourceManagerHub& manager = m_engine.getResourceManager();
				PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(Path(path));
				iter.value()->geom = geom_res;
			}
			m_world.onComponentCreated(e, VEHICLE_TYPE, this);
			if (m_is_game_running) tmp.push(e);
		}

		const u32 wheels_count = serializer.read<u32>();
		m_wheels.reserve(wheels_count);
		for (u32 i = 0; i < wheels_count; ++i) {
			EntityRef e = serializer.read<EntityRef>();
			e = entity_map.get(e);
			Wheel& w = m_wheels.insert(e);
			serializer.read(w);
			m_world.onComponentCreated(e, WHEEL_TYPE, this);
		}

		if (m_is_game_running) {
			for (EntityRef e : tmp) {
				rebuildVehicle(e, *m_vehicles[e]);
			}
		}
	}

	void deserializeJoints(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_joints.reserve(count + m_joints.size());

		for (u32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			Joint& joint = m_joints.insert(entity);
			int type;
			serializer.read(type);
			serializer.read(joint.connected_body);
			joint.connected_body = entity_map.get(joint.connected_body);
			serializer.read(joint.local_frame0);
			ComponentType cmp_type;

			switch (PxJointConcreteType::Enum(type)) {
				case PxJointConcreteType::eSPHERICAL: {
					cmp_type = SPHERICAL_JOINT_TYPE;
					auto* px_joint = PxSphericalJointCreate(m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
					joint.physx = px_joint;
					u32 flags;
					serializer.read(flags);
					px_joint->setSphericalJointFlags(PxSphericalJointFlags(flags));
					PxJointLimitCone limit(0, 0);
					serializer.read(limit);
					px_joint->setLimitCone(limit);
					break;
				}
				case PxJointConcreteType::eREVOLUTE: {
					cmp_type = HINGE_JOINT_TYPE;
					auto* px_joint = PxRevoluteJointCreate(m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
					joint.physx = px_joint;
					u32 flags;
					serializer.read(flags);
					px_joint->setRevoluteJointFlags(PxRevoluteJointFlags(flags));
					PxJointAngularLimitPair limit(0, 0);
					serializer.read(limit);
					px_joint->setLimit(limit);
					break;
				}
				case PxJointConcreteType::eDISTANCE: {
					cmp_type = DISTANCE_JOINT_TYPE;
					auto* px_joint = PxDistanceJointCreate(m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
					joint.physx = px_joint;
					u32 flags;
					serializer.read(flags);
					px_joint->setDistanceJointFlags(PxDistanceJointFlags(flags));
					float tmp;
					serializer.read(tmp);
					px_joint->setDamping(tmp);
					serializer.read(tmp);
					px_joint->setStiffness(tmp);
					serializer.read(tmp);
					px_joint->setTolerance(tmp);
					serializer.read(tmp);
					px_joint->setMinDistance(tmp);
					serializer.read(tmp);
					px_joint->setMaxDistance(tmp);
					break;
				}
				case PxJointConcreteType::eD6: {
					cmp_type = D6_JOINT_TYPE;
					auto* px_joint = PxD6JointCreate(m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
					joint.physx = px_joint;
					int motions[6];
					serializer.read(motions);
					px_joint->setMotion(PxD6Axis::eX, (PxD6Motion::Enum)motions[0]);
					px_joint->setMotion(PxD6Axis::eY, (PxD6Motion::Enum)motions[1]);
					px_joint->setMotion(PxD6Axis::eZ, (PxD6Motion::Enum)motions[2]);
					px_joint->setMotion(PxD6Axis::eSWING1, (PxD6Motion::Enum)motions[3]);
					px_joint->setMotion(PxD6Axis::eSWING2, (PxD6Motion::Enum)motions[4]);
					px_joint->setMotion(PxD6Axis::eTWIST, (PxD6Motion::Enum)motions[5]);
					PxJointLinearLimit linear_limit(0, PxSpring(0, 0));
					serializer.read(linear_limit);
					px_joint->setLinearLimit(linear_limit);
					PxJointLimitCone swing_limit(0, 0);
					serializer.read(swing_limit);
					px_joint->setSwingLimit(swing_limit);
					PxJointAngularLimitPair twist_limit(0, 0);
					serializer.read(twist_limit);
					px_joint->setTwistLimit(twist_limit);
					break;
				}
				default: ASSERT(false); break;
			}

			m_world.onComponentCreated(entity, cmp_type, this);
		}
	}


	void deserializeTerrains(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		for (u32 i = 0; i < count; ++i) {
			Heightfield terrain;
			terrain.m_module = this;
			serializer.read(terrain.m_entity);
			terrain.m_entity = entity_map.get(terrain.m_entity);
			const char* tmp = serializer.readString();
			serializer.read(terrain.m_xz_scale);
			serializer.read(terrain.m_y_scale);
			serializer.read(terrain.m_layer);

			m_terrains.insert(terrain.m_entity, terrain);
			setHeightmapSource(terrain.m_entity, Path(tmp));
			m_world.onComponentCreated(terrain.m_entity, HEIGHTFIELD_TYPE, this);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		deserializeActors(serializer, entity_map, version);
		deserializeControllers(serializer, entity_map);
		deserializeTerrains(serializer, entity_map);

		if (version <= (i32)PhysicsModuleVersion::REMOVED_RAGDOLLS) {
			u32 count;
			serializer.read(count);
			ASSERT(count == 0); // ragdolls were removed
		}

		if (version > (i32)PhysicsModuleVersion::INSTANCED_CUBE) {
			i32 count;
			serializer.read(count);
			for (i32 i = 0; i < count; ++i) {
				EntityRef e;
				serializer.read(e);
				e = entity_map.get(e);
				InstancedCube c(m_allocator);
				serializer.read(c.half_extents);
				serializer.read(c.layer);
				m_instanced_cubes.insert(e, static_cast<InstancedCube&&>(c));
				m_world.onComponentCreated(e, INSTANCED_CUBE_TYPE, this);
			}
		}

		if (version > (i32)PhysicsModuleVersion::INSTANCED_MESH) {
			i32 count;
			serializer.read(count);
			for (i32 i = 0; i < count; ++i) {
				EntityRef e;
				serializer.read(e);
				e = entity_map.get(e);
				InstancedMesh m(m_allocator);
				const char* path = serializer.readString();
				m.resource = path[0] ? m_engine.getResourceManager().load<PhysicsGeometry>(Path(path)) : nullptr;
				serializer.read(m.layer);
				m_instanced_meshes.insert(e, static_cast<InstancedMesh&&>(m));
				m_world.onComponentCreated(e, INSTANCED_MESH_TYPE, this);
			}
		}

		deserializeJoints(serializer, entity_map);
		deserializeVehicles(serializer, entity_map, version);
	}


	Vec3 getActorVelocity(EntityRef entity) override
	{
		const RigidActor& actor = m_actors[entity];
		if (actor.dynamic_type != DynamicType::DYNAMIC)
		{
			logWarning("Trying to get speed of static object");
			return Vec3::ZERO;
		}

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor.physx_actor);
		if (!physx_actor) return Vec3::ZERO;
		return fromPhysx(physx_actor->getLinearVelocity());
	}


	float getActorSpeed(EntityRef entity) override
	{
		const RigidActor& actor = m_actors[entity];
		if (actor.dynamic_type != DynamicType::DYNAMIC)
		{
			logWarning("Trying to get speed of static object");
			return 0;
		}

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor.physx_actor);
		if (!physx_actor) return 0;
		return physx_actor->getLinearVelocity().magnitude();
	}


	void putToSleep(EntityRef entity) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;
		const RigidActor& actor = iter.value();

		if (actor.dynamic_type != DynamicType::DYNAMIC) {
			logWarning("Trying to put static object to sleep");
			return;
		}

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor.physx_actor);
		if (!physx_actor) return;
		physx_actor->putToSleep();
	}


	void applyForceToActor(EntityRef entity, const Vec3& force) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;
		const RigidActor& actor = iter.value();

		if (actor.dynamic_type != DynamicType::DYNAMIC) return;

		auto* physx_actor = actor.physx_actor->is<PxRigidDynamic>();
		if (!physx_actor) return;
		physx_actor->addForce(toPhysx(force));
	}


	void applyImpulseToActor(EntityRef entity, const Vec3& impulse) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;
		RigidActor& actor = iter.value();

		if (actor.dynamic_type != DynamicType::DYNAMIC) return;

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor.physx_actor);
		if (!physx_actor) return;
		physx_actor->addForce(toPhysx(impulse), PxForceMode::eIMPULSE);
	}
	
	void onActorResourceStateChanged(Resource::State prev_state, Resource::State new_state, Resource& res) {
		auto iter = m_resource_actor_map.find((PhysicsGeometry*)&res);
		ASSERT(iter.isValid());
		const EntityRef e = iter.value();
		RigidActor* actor = &m_actors[e];
		for (;;) {
			actor->onStateChanged(prev_state, new_state, res);
			if (!actor->next_with_mesh.isValid()) break;
			
			actor = &m_actors[*actor->next_with_mesh];
		}
	}
	
	Path getInstancedMeshGeomPath(EntityRef entity) override {
		const InstancedMesh& im = m_instanced_meshes[entity];
		return im.resource ? im.resource->getPath() : Path();
	}

	void setInstancedMeshGeomPath(EntityRef entity, const Path& path) override {
		InstancedMesh& im = m_instanced_meshes[entity];
		if (path.isEmpty() && !im.resource) return;
		if (im.resource && im.resource->getPath() == path) return;

		if (im.resource) im.resource->decRefCount();
		im.resource = nullptr;
		if (!path.isEmpty()) {
			im.resource = m_engine.getResourceManager().load<PhysicsGeometry>(path);
		}
	}

	u32 getInstancedMeshLayer(EntityRef entity) override {
		return m_instanced_meshes[entity].layer;
	}

	void setInstancedMeshLayer(EntityRef entity, u32 layer) override {
		m_instanced_meshes[entity].layer = layer;
	}
	
	u32 getInstancedCubeLayer(EntityRef entity) override {
		return m_instanced_cubes[entity].layer;
	}

	void setInstancedCubeLayer(EntityRef entity, u32 layer) override {
		m_instanced_cubes[entity].layer = layer;
	}

	Vec3 getInstancedCubeHalfExtents(EntityRef entity) override {
		return m_instanced_cubes[entity].half_extents;
	}

	void setInstancedCubeHalfExtents(EntityRef entity, const Vec3& half_extents) override {
		m_instanced_cubes[entity].half_extents = half_extents;
	}

	static PxFilterFlags filterShader(PxFilterObjectAttributes attributes0,
		PxFilterData filterData0,
		PxFilterObjectAttributes attributes1,
		PxFilterData filterData1,
		PxPairFlags& pairFlags,
		const void* constantBlock,
		PxU32 constantBlockSize)
	{
		if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1)) {
			pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
			return PxFilterFlag::eDEFAULT;
		}

		if (!(filterData0.word0 & filterData1.word1) || !(filterData1.word0 & filterData0.word1)) {
			return PxFilterFlag::eSUPPRESS;
		}
		pairFlags = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return PxFilterFlag::eDEFAULT;
	}


	struct QueuedForce
	{
		EntityRef entity;
		Vec3 force;
	};

	struct Controller
	{
		PxController* controller;
		EntityRef entity;
		Vec3 frame_change;
		float radius;
		float height;
		float custom_gravity_acceleration;
		u32 layer;
		PxFilterData filter_data;

		bool custom_gravity = false;
		bool use_root_motion = 0;
		float gravity_speed = 0;
	};
	
	struct FilterCallback : PxQueryFilterCallback
	{
		PxQueryHitType::Enum preFilter(const PxFilterData& filterData,
			const PxShape* shape,
			const PxRigidActor* actor,
			PxHitFlags& queryFlags) override
		{
			PxFilterData fd0 = shape->getSimulationFilterData();
			PxFilterData fd1 = m_filter_data;
			if (!(fd0.word0 & fd1.word1) || !(fd0.word1 & fd1.word0)) return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override
		{
			return PxQueryHitType::eNONE;
		}

		PxFilterData m_filter_data;
	};

	struct HitReport : PxUserControllerHitReport {
		HitReport(PhysicsModuleImpl& module) : module(module) {}
		void onShapeHit(const PxControllerShapeHit& hit) override {
			void* user_data = hit.controller->getActor()->userData;
			const EntityRef e1 {(i32)(uintptr)user_data};
			const EntityRef e2 {(i32)(uintptr)hit.actor->userData};

			module.onControllerHit(e1, e2);
		}
		void onControllerHit(const PxControllersHit& hit) override {}
		void onObstacleHit(const PxControllerObstacleHit& hit) override {}

		PhysicsModuleImpl& module;
	} ;

	struct InstancedCube {
		InstancedCube(IAllocator& allocator) : actors(allocator) {}
		Vec3 half_extents;
		u32 layer = 0;
		Array<PxRigidActor*> actors;
	};

	struct InstancedMesh {
		InstancedMesh(IAllocator& allocator) : actors(allocator) {}
		u32 layer = 0;
		Array<PxRigidActor*> actors;
		PhysicsGeometry* resource = nullptr;
	};

	IAllocator& m_allocator;
	Engine& m_engine;
	World& m_world;
	HitReport m_hit_report;
	PhysxContactCallback m_contact_callback;
	BoneOrientation m_new_bone_orientation = BoneOrientation::X;
	PxScene* m_scene;
	LuaScriptModule* m_script_module;
	PhysicsSystem* m_system;
	PxRigidDynamic* m_dummy_actor;
	PxControllerManager* m_controller_manager;
	PxMaterial* m_default_material;
	FilterCallback m_filter_callback;

	HashMap<EntityRef, RigidActor> m_actors;
	HashMap<PhysicsGeometry*, EntityRef> m_resource_actor_map;
	AssociativeArray<EntityRef, Joint> m_joints;
	HashMap<EntityRef, Controller> m_controllers;
	HashMap<EntityRef, Heightfield> m_terrains;
	HashMap<EntityRef, UniquePtr<Vehicle>> m_vehicles;
	HashMap<EntityRef, Wheel> m_wheels;
	HashMap<EntityRef, InstancedCube> m_instanced_cubes;
	HashMap<EntityRef, InstancedMesh> m_instanced_meshes;
	PxVehicleDrivableSurfaceToTireFrictionPairs* m_vehicle_frictions;
	PxBatchQuery* m_vehicle_batch_query;
	u8 m_vehicle_query_mem[sizeof(PxRaycastQueryResult) * 64 + sizeof(PxRaycastHit) * 64];
	PxRaycastQueryResult* m_vehicle_results;
	u64 m_physics_cmps_mask;

	Array<EntityRef> m_dynamic_actors;
	RigidActor* m_update_in_progress;
	DelegateList<void(const ContactData&)> m_contact_callbacks;
	bool m_is_game_running;
	u32 m_debug_visualization_flags;
	CPUDispatcher m_cpu_dispatcher;
	CollisionLayers& m_layers;
};

PhysicsModuleImpl::PhysicsModuleImpl(Engine& engine, World& world, PhysicsSystem& system, IAllocator& allocator)
	: m_allocator(allocator)
	, m_engine(engine)
	, m_controllers(m_allocator)
	, m_actors(m_allocator)
	, m_vehicles(m_allocator)
	, m_wheels(m_allocator)
	, m_terrains(m_allocator)
	, m_dynamic_actors(m_allocator)
	, m_instanced_cubes(m_allocator)
	, m_instanced_meshes(m_allocator)
	, m_world(world)
	, m_is_game_running(false)
	, m_contact_callback(*this)
	, m_contact_callbacks(m_allocator)
	, m_joints(m_allocator)
	, m_script_module(nullptr)
	, m_debug_visualization_flags(0)
	, m_update_in_progress(nullptr)
	, m_vehicle_batch_query(nullptr)
	, m_system(&system)
	, m_hit_report(*this)
	, m_layers(m_system->getCollisionLayers())
	, m_resource_actor_map(m_allocator)
{
	m_physics_cmps_mask = 0;

	const RuntimeHash hash("physics");
	for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
		if (cmp.system_hash == hash) {
			m_physics_cmps_mask |= (u64)1 << cmp.cmp->component_type.index;
		}
	}

	m_vehicle_frictions = createFrictionPairs();
}


UniquePtr<PhysicsModule> PhysicsModule::create(PhysicsSystem& system, World& world, Engine& engine, IAllocator& allocator)
{
	PhysicsModuleImpl* impl = LUMIX_NEW(allocator, PhysicsModuleImpl)(engine, world, system, allocator);
	impl->m_world.entityTransformed().bind<&PhysicsModuleImpl::onEntityMoved>(impl);
	impl->m_world.entityDestroyed().bind<&PhysicsModuleImpl::onEntityDestroyed>(impl);
	PxSceneDesc sceneDesc(system.getPhysics()->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.8f, 0.0f);
	sceneDesc.cpuDispatcher = &impl->m_cpu_dispatcher;

	sceneDesc.filterShader = impl->filterShader;
	sceneDesc.simulationEventCallback = &impl->m_contact_callback;

	impl->m_scene = system.getPhysics()->createScene(sceneDesc);
	if (!impl->m_scene)
	{
		LUMIX_DELETE(allocator, impl);
		return UniquePtr<PhysicsModule>(nullptr, nullptr);
	}

	impl->m_controller_manager = PxCreateControllerManager(*impl->m_scene);

	impl->m_default_material = impl->m_system->getPhysics()->createMaterial(0.5f, 0.5f, 0.1f);
	PxSphereGeometry geom(1);
	impl->m_dummy_actor = PxCreateDynamic(impl->m_scene->getPhysics(), PxTransform(PxIdentity), geom, *impl->m_default_material, 1);
	impl->m_vehicle_batch_query = impl->createVehicleBatchQuery(impl->m_vehicle_query_mem);
	return UniquePtr<PhysicsModuleImpl>(impl, &allocator);
}

void PhysicsModule::reflect() {
	struct LayerEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { 
			PhysicsModule* module = (PhysicsModule*)cmp.module;
			PhysicsSystem& system = (PhysicsSystem&)module->getSystem();
			return system.getCollisionsLayersCount();
		}
		const char* name(ComponentUID cmp, u32 idx) const override { 
			PhysicsModule* module = (PhysicsModule*)cmp.module;
			PhysicsSystem& system = (PhysicsSystem&)module->getSystem();
			return system.getCollisionLayerName(idx);
		}
	};

	struct DynamicTypeEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 3; }
		const char* name(ComponentUID cmp, u32 idx) const override { 
			switch ((PhysicsModule::DynamicType)idx) {
				case PhysicsModule::DynamicType::DYNAMIC: return "Dynamic";
				case PhysicsModule::DynamicType::STATIC: return "Static";
				case PhysicsModule::DynamicType::KINEMATIC: return "Kinematic";
			}
			ASSERT(false);
			return "N/A";
		}
	};

	struct D6MotionEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 3; }
		const char* name(ComponentUID cmp, u32 idx) const override { 
			switch ((PhysicsModule::D6Motion)idx) {
				case PhysicsModule::D6Motion::LOCKED: return "Locked";
				case PhysicsModule::D6Motion::LIMITED: return "Limited";
				case PhysicsModule::D6Motion::FREE: return "Free";
			}
			ASSERT(false);
			return "N/A";
		}
	};

	struct WheelSlotEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 4; }
		const char* name(ComponentUID cmp, u32 idx) const override { 
			switch ((PhysicsModule::WheelSlot)idx) {
				case PhysicsModule::WheelSlot::FRONT_LEFT: return "Front left";
				case PhysicsModule::WheelSlot::FRONT_RIGHT: return "Front right";
				case PhysicsModule::WheelSlot::REAR_LEFT: return "Rear left";
				case PhysicsModule::WheelSlot::REAR_RIGHT: return "Rear right";
			}
			ASSERT(false);
			return "N/A";
		}
	};

	LUMIX_MODULE(PhysicsModuleImpl, "physics")
		.LUMIX_FUNC(PhysicsModule::raycast)
		.LUMIX_CMP(D6Joint, "d6_joint", "Physics / Joint / D6")
			.LUMIX_PROP(JointConnectedBody, "Connected body")
			.LUMIX_PROP(JointAxisPosition, "Axis position")
			.LUMIX_PROP(JointAxisDirection, "Axis direction")
			.LUMIX_ENUM_PROP(D6JointXMotion, "X motion").attribute<D6MotionEnum>()
			.LUMIX_ENUM_PROP(D6JointYMotion, "Y motion").attribute<D6MotionEnum>()
			.LUMIX_ENUM_PROP(D6JointZMotion, "Z motion").attribute<D6MotionEnum>()
			.LUMIX_ENUM_PROP(D6JointSwing1Motion, "Swing 1").attribute<D6MotionEnum>()
			.LUMIX_ENUM_PROP(D6JointSwing2Motion, "Swing 2").attribute<D6MotionEnum>()
			.LUMIX_ENUM_PROP(D6JointTwistMotion, "Twist").attribute<D6MotionEnum>()
			.LUMIX_PROP(D6JointLinearLimit, "Linear limit").minAttribute(0)
			.LUMIX_PROP(D6JointSwingLimit, "Swing limit").radiansAttribute()
			.LUMIX_PROP(D6JointTwistLimit, "Twist limit").radiansAttribute()
			.LUMIX_PROP(D6JointDamping, "Damping")
			.LUMIX_PROP(D6JointStiffness, "Stiffness")
			.LUMIX_PROP(D6JointRestitution, "Restitution")
		.LUMIX_CMP(SphericalJoint, "spherical_joint", "Physics / Joint / Spherical")
			.LUMIX_PROP(JointConnectedBody, "Connected body")
			.LUMIX_PROP(JointAxisPosition, "Axis position")
			.LUMIX_PROP(JointAxisDirection, "Axis direction")
			.LUMIX_PROP(SphericalJointUseLimit, "Use limit")
			.LUMIX_PROP(SphericalJointLimit, "Limit").radiansAttribute()
		.LUMIX_CMP(DistanceJoint, "distance_joint", "Physics / Joint / Distance")
			.LUMIX_PROP(JointConnectedBody, "Connected body")
			.LUMIX_PROP(JointAxisPosition, "Axis position")
			.LUMIX_PROP(DistanceJointDamping, "Damping").minAttribute(0)
			.LUMIX_PROP(DistanceJointStiffness, "Stiffness").minAttribute(0)
			.LUMIX_PROP(DistanceJointTolerance, "Tolerance").minAttribute(0)
			.LUMIX_PROP(DistanceJointLimits, "Limits")
		.LUMIX_CMP(HingeJoint, "hinge_joint", "Physics / Joint / Hinge")
			.LUMIX_PROP(JointConnectedBody, "Connected body")
			.LUMIX_PROP(JointAxisPosition, "Axis position")
			.LUMIX_PROP(JointAxisDirection, "Axis direction")
			.LUMIX_PROP(HingeJointDamping, "Damping").minAttribute(0)
			.LUMIX_PROP(HingeJointStiffness, "Stiffness").minAttribute(0)
			.LUMIX_PROP(HingeJointUseLimit, "Use limit")
			.LUMIX_PROP(HingeJointLimit, "Limit").radiansAttribute()
		.LUMIX_CMP(InstancedCube, "physical_instanced_cube", "Physics / Instanced cube")
			.LUMIX_PROP(InstancedCubeHalfExtents, "Half extents")
			.LUMIX_ENUM_PROP(InstancedCubeLayer, "Layer").attribute<LayerEnum>()
		.LUMIX_CMP(InstancedMesh, "physical_instanced_mesh", "Physics / Instanced mesh")
			.LUMIX_PROP(InstancedMeshGeomPath, "Mesh").resourceAttribute(PhysicsGeometry::TYPE)
			.LUMIX_ENUM_PROP(InstancedMeshLayer, "Layer").attribute<LayerEnum>()
		.LUMIX_CMP(Controller, "physical_controller", "Physics / Controller")
			.LUMIX_FUNC_EX(PhysicsModule::moveController, "move")
			.LUMIX_FUNC_EX(PhysicsModule::isControllerCollisionDown, "isCollisionDown")
			.LUMIX_PROP(ControllerRadius, "Radius")
			.LUMIX_PROP(ControllerHeight, "Height")
			.LUMIX_ENUM_PROP(ControllerLayer, "Layer").attribute<LayerEnum>()
			.LUMIX_PROP(ControllerUseRootMotion, "Use root motion")
			.LUMIX_PROP(ControllerCustomGravity, "Use custom gravity")
			.LUMIX_PROP(ControllerCustomGravityAcceleration, "Custom gravity acceleration")
		.LUMIX_CMP(RigidActor, "rigid_actor", "Physics / Rigid actor")
			.icon(ICON_FA_VOLLEYBALL_BALL)
			.LUMIX_FUNC_EX(PhysicsModule::putToSleep, "putToSleep")
			.LUMIX_FUNC_EX(PhysicsModule::getActorSpeed, "getSpeed")
			.LUMIX_FUNC_EX(PhysicsModule::getActorVelocity, "getVelocity")
			.LUMIX_FUNC_EX(PhysicsModule::applyForceToActor, "applyForce")
			.LUMIX_FUNC_EX(PhysicsModule::applyImpulseToActor, "applyImpulse")
			.LUMIX_FUNC_EX(PhysicsModule::addForceAtPos, "addForceAtPos")
			.LUMIX_ENUM_PROP(ActorLayer, "Layer").attribute<LayerEnum>()
			.LUMIX_ENUM_PROP(DynamicType, "Dynamic").attribute<DynamicTypeEnum>()
			.LUMIX_PROP(IsTrigger, "Trigger")
			.begin_array<&PhysicsModule::getBoxGeometryCount, &PhysicsModule::addBoxGeometry, &PhysicsModule::removeBoxGeometry>("Box geometry")	
				.LUMIX_PROP(BoxGeomHalfExtents, "Size")
				.LUMIX_PROP(BoxGeomOffsetPosition, "Position offset")
				.LUMIX_PROP(BoxGeomOffsetRotation, "Rotation offset").radiansAttribute()
			.end_array()
			.begin_array<&PhysicsModule::getSphereGeometryCount, &PhysicsModule::addSphereGeometry, &PhysicsModule::removeSphereGeometry>("Sphere geometry")
				.LUMIX_PROP(SphereGeomRadius, "Radius").minAttribute(0)
				.LUMIX_PROP(SphereGeomOffsetPosition, "Position offset")
			.end_array()
			.LUMIX_PROP(MeshGeomPath, "Mesh").resourceAttribute(PhysicsGeometry::TYPE)
			.LUMIX_PROP(RigidActorMaterial, "Material").resourceAttribute(PhysicsMaterial::TYPE)
		.LUMIX_CMP(Vehicle, "vehicle", "Physics / Vehicle")
			.icon(ICON_FA_CAR_ALT)
			.LUMIX_FUNC_EX(PhysicsModule::setVehicleAccel, "setAccel")
			.LUMIX_FUNC_EX(PhysicsModule::setVehicleSteer, "setSteer")
			.LUMIX_FUNC_EX(PhysicsModule::setVehicleBrake, "setBrake")
			.prop<&PhysicsModuleImpl::getVehicleSpeed>("Speed")
			.prop<&PhysicsModuleImpl::getVehicleCurrentGear>("Current gear")
			.prop<&PhysicsModuleImpl::getVehicleRPM>("RPM")
			.LUMIX_PROP(VehicleMass, "Mass").minAttribute(0)
			.LUMIX_PROP(VehicleCenterOfMass, "Center of mass")
			.LUMIX_PROP(VehicleMOIMultiplier, "MOI multiplier")
			.LUMIX_PROP(VehicleChassis, "Chassis").resourceAttribute(PhysicsGeometry::TYPE)
			.LUMIX_ENUM_PROP(VehicleChassisLayer, "Chassis layer").attribute<LayerEnum>()
			.LUMIX_ENUM_PROP(VehicleWheelsLayer, "Wheels layer").attribute<LayerEnum>()
		.LUMIX_CMP(Wheel, "wheel", "Physics / Wheel")
			.LUMIX_PROP(WheelRadius, "Radius").minAttribute(0)
			.LUMIX_PROP(WheelWidth, "Width").minAttribute(0)
			.LUMIX_PROP(WheelMass, "Mass").minAttribute(0)
			.LUMIX_PROP(WheelMOI, "MOI").minAttribute(0)
			.LUMIX_PROP(WheelSpringMaxCompression, "Max compression").minAttribute(0)
			.LUMIX_PROP(WheelSpringMaxDroop, "Max droop").minAttribute(0)
			.LUMIX_PROP(WheelSpringStrength, "Spring strength").minAttribute(0)
			.LUMIX_PROP(WheelSpringDamperRate, "Spring damper rate").minAttribute(0)
			.LUMIX_ENUM_PROP(WheelSlot, "Slot").attribute<WheelSlotEnum>()
			.prop<&PhysicsModuleImpl::getWheelRPM>("RPM")
		.LUMIX_CMP(Heightfield, "physical_heightfield", "Physics / Heightfield")
			.LUMIX_ENUM_PROP(HeightfieldLayer, "Layer").attribute<LayerEnum>()
			.LUMIX_PROP(HeightmapSource, "Heightmap").resourceAttribute(Texture::TYPE)
			.LUMIX_PROP(HeightmapYScale, "Y scale").minAttribute(0)
			.LUMIX_PROP(HeightmapXZScale, "XZ scale").minAttribute(0)
	;
}

void PhysicsModuleImpl::RigidActor::setIsTrigger(bool is) {
	is_trigger = is;
	if (physx_actor) {
		PxShape* shape;
		if (physx_actor->getShapes(&shape, 1) == 1) {
			if (is_trigger) {
				shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false); // must set false first
				shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
			} else {
				shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
				shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
			}
		}
	}
}

void PhysicsModuleImpl::RigidActor::onStateChanged(Resource::State, Resource::State new_state, Resource&)
{
	if (new_state == Resource::State::READY)
	{
#if 0
		moveShapeIndices(entity, index, PxGeometryType::eBOX);
		PxRigidActor* actor = m_actors[entity].physx_actor;
		PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*physx_actor, geom, *m_default_material);
		shape->userData = (void*)(intptr_t)index;
#endif
		scale = module.getWorld().getScale(entity);
		PxMeshScale pxscale(toPhysx(scale));
		PxConvexMeshGeometry convex_geom(mesh->convex_mesh, pxscale);
		PxTriangleMeshGeometry tri_geom(mesh->tri_mesh, pxscale);
		const PxGeometry* geom = mesh->convex_mesh ? static_cast<PxGeometry*>(&convex_geom) : static_cast<PxGeometry*>(&tri_geom);
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*physx_actor, *geom, material ? *material->material : *module.m_default_material);
		(void)shape;
		module.updateFilterData(physx_actor, layer);
	}
}


void PhysicsModuleImpl::RigidActor::rescale()
{
	if (!mesh || !mesh->isReady()) return;

	onStateChanged(mesh->getState(), mesh->getState(), *mesh);
}


void PhysicsModuleImpl::RigidActor::setPhysxActor(PxRigidActor* actor)
{
	if (physx_actor)
	{
		module.m_scene->removeActor(*physx_actor);
		physx_actor->release();
	}
	physx_actor = actor;
	if (actor)
	{
		module.m_scene->addActor(*actor);
		actor->userData = (void*)(intptr_t)entity.index;
		module.updateFilterData(actor, layer);
		setIsTrigger(is_trigger);
	}
}


void PhysicsModuleImpl::RigidActor::setMesh(PhysicsGeometry* new_value)
{
	if (physx_actor) {
		const i32 shape_count = physx_actor->getNbShapes();
		PxShape* shape;
		for (int i = 0; i < shape_count; ++i) {
			physx_actor->getShapes(&shape, 1, i);
			if (shape->getGeometryType() == physx::PxGeometryType::eCONVEXMESH ||
				shape->getGeometryType() == physx::PxGeometryType::eTRIANGLEMESH)
			{
				physx_actor->detachShape(*shape);
				break;
			}
		}
	}

	if (mesh) {
		if (!next_with_mesh.isValid() && !prev_with_mesh.isValid()) {
			mesh->getObserverCb().unbind<&PhysicsModuleImpl::onActorResourceStateChanged>(&module);
			module.m_resource_actor_map.erase(mesh);
			mesh->decRefCount();
		} else {
			auto iter = module.m_resource_actor_map.find(mesh);
			if (iter.value() == entity) {
				module.m_resource_actor_map[mesh] = *next_with_mesh;
			}

			if (next_with_mesh.isValid()) module.m_actors[*next_with_mesh].prev_with_mesh = prev_with_mesh;
			if (prev_with_mesh.isValid()) module.m_actors[*prev_with_mesh].next_with_mesh = next_with_mesh;
		}
	}
	mesh = new_value;
	if (mesh) {
		auto iter = module.m_resource_actor_map.find(mesh);
		if (iter.isValid()) {
			EntityRef e = iter.value();
			next_with_mesh = e;
			module.m_actors[*next_with_mesh].prev_with_mesh = entity;
			prev_with_mesh = INVALID_ENTITY;
			module.m_resource_actor_map[mesh] = entity;
			if (mesh->isReady()) {
				onStateChanged(Resource::State::READY, Resource::State::READY, *new_value);
			}
			mesh->decRefCount();
		} else {
			module.m_resource_actor_map.insert(mesh, entity);
			mesh->onLoaded<&PhysicsModuleImpl::onActorResourceStateChanged>(&module);
			prev_with_mesh = INVALID_ENTITY;
			next_with_mesh = INVALID_ENTITY;
		}
	}
}


Heightfield::~Heightfield()
{
	if (m_actor) m_actor->release();
	if (m_heightmap)
	{
		m_heightmap->decRefCount();
		m_heightmap->getObserverCb().unbind<&Heightfield::heightmapLoaded>(this);
	}
}


void Heightfield::heightmapLoaded(Resource::State, Resource::State new_state, Resource&)
{
	if (new_state == Resource::State::READY)
	{
		m_module->heightmapLoaded(*this);
	}
}


} // namespace Lumix
