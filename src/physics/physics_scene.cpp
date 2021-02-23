#include <PxPhysicsAPI.h>

#include "physics/physics_scene.h"
#include "animation/animation_scene.h"
#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/atomic.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/universe.h"
#include "lua_script/lua_script_system.h"
#include "physics/physics_geometry.h"
#include "physics/physics_system.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "imgui/IconsFontAwesome5.h"


using namespace physx;


namespace Lumix
{


static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");
static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType RIGID_ACTOR_TYPE = reflection::getComponentType("rigid_actor");
static const ComponentType RAGDOLL_TYPE = reflection::getComponentType("ragdoll");
static const ComponentType CONTROLLER_TYPE = reflection::getComponentType("physical_controller");
static const ComponentType HEIGHTFIELD_TYPE = reflection::getComponentType("physical_heightfield");
static const ComponentType DISTANCE_JOINT_TYPE = reflection::getComponentType("distance_joint");
static const ComponentType HINGE_JOINT_TYPE = reflection::getComponentType("hinge_joint");
static const ComponentType SPHERICAL_JOINT_TYPE = reflection::getComponentType("spherical_joint");
static const ComponentType D6_JOINT_TYPE = reflection::getComponentType("d6_joint");
static const ComponentType VEHICLE_TYPE = reflection::getComponentType("vehicle");
static const ComponentType WHEEL_TYPE = reflection::getComponentType("wheel");
static const u32 RENDERER_HASH = crc32("renderer");

enum class PhysicsSceneVersion
{
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

struct RagdollBone
{
	enum Type : int
	{
		BOX,
		CAPSULE
	};

	int pose_bone_idx;
	PxRigidDynamic* actor;
	PxJoint* parent_joint;
	RagdollBone* child;
	RagdollBone* next;
	RagdollBone* prev;
	RagdollBone* parent;
	RigidTransform bind_transform;
	RigidTransform inv_bind_transform;
	bool is_kinematic;
};


struct Ragdoll
{
	EntityRef entity;
	RagdollBone* root = nullptr;
	RigidTransform root_transform;
	int layer;
};


struct OutputStream final : PxOutputStream
{
	explicit OutputStream(IAllocator& allocator)
		: allocator(allocator)
	{
		data = (u8*)allocator.allocate(sizeof(u8) * 4096);
		capacity = 4096;
		size = 0;
	}

	~OutputStream() { allocator.deallocate(data); }


	PxU32 write(const void* src, PxU32 count) override
	{
		if (size + (int)count > capacity)
		{
			int new_capacity = maximum(size + (int)count, capacity + 4096);
			u8* new_data = (u8*)allocator.allocate(sizeof(u8) * new_capacity);
			memcpy(new_data, data, size);
			allocator.deallocate(data);
			data = new_data;
			capacity = new_capacity;
		}
		memcpy(data + size, src, count);
		size += count;
		return count;
	}

	u8* data;
	IAllocator& allocator;
	int capacity;
	int size;
};


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
	PhysicsScene::WheelSlot slot = PhysicsScene::WheelSlot::FRONT_LEFT;
	
	static_assert((int)PhysicsScene::WheelSlot::FRONT_LEFT == PxVehicleDrive4WWheelOrder::eFRONT_LEFT);
	static_assert((int)PhysicsScene::WheelSlot::FRONT_RIGHT == PxVehicleDrive4WWheelOrder::eFRONT_RIGHT);
	static_assert((int)PhysicsScene::WheelSlot::REAR_LEFT == PxVehicleDrive4WWheelOrder::eREAR_LEFT);
	static_assert((int)PhysicsScene::WheelSlot::REAR_RIGHT == PxVehicleDrive4WWheelOrder::eREAR_RIGHT);
};


struct Heightfield
{
	Heightfield();
	~Heightfield();
	void heightmapLoaded(Resource::State, Resource::State new_state, Resource&);

	struct PhysicsSceneImpl* m_scene;
	EntityRef m_entity;
	PxRigidActor* m_actor;
	Texture* m_heightmap;
	float m_xz_scale;
	float m_y_scale;
	int m_layer;
};


struct PhysicsSceneImpl final : PhysicsScene
{
	struct CPUDispatcher : physx::PxCpuDispatcher
	{
		void submitTask(PxBaseTask& task) override
		{
			jobs::run(&task,
				[](void* data) {
					PxBaseTask* task = (PxBaseTask*)data;
					PROFILE_FUNCTION();
					task->run();
					task->release();
				},
				nullptr);
		}
		PxU32 getWorkerCount() const override { return os::getCPUsCount(); }
	};


	struct PhysxContactCallback final : PxSimulationEventCallback
	{
		explicit PhysxContactCallback(PhysicsSceneImpl& scene)
			: m_scene(scene)
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

				m_scene.onContact(contact_data);
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

				m_scene.onTrigger(e1, e2, pairs[i].status == PxPairFlag::eNOTIFY_TOUCH_LOST);
			}
		}


		void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
		void onWake(PxActor**, PxU32) override {}
		void onSleep(PxActor**, PxU32) override {}


		PhysicsSceneImpl& m_scene;
	};


	struct RigidActor {
		RigidActor(PhysicsSceneImpl& scene, EntityRef entity)
			: scene(scene)
			, entity(entity)
		{}

		~RigidActor() {
			setResource(nullptr);
			if (physx_actor) physx_actor->release();
		}

		void rescale();
		void setResource(PhysicsGeometry* resource);
		void setPhysxActor(PxRigidActor* actor);
		void onStateChanged(Resource::State old_state, Resource::State new_state, Resource&);

		PhysicsSceneImpl& scene;
		EntityRef entity;
		float scale = 1;
		int layer = 0;
		PxRigidActor* physx_actor = nullptr;
		PhysicsGeometry* resource = nullptr;
		RigidActor* prev_with_resource = nullptr;
		RigidActor* next_with_resource = nullptr;
		DynamicType dynamic_type = DynamicType::STATIC;
		bool is_trigger = false;
	};


	PhysicsSceneImpl(Engine& engine, Universe& context, PhysicsSystem& system, IAllocator& allocator);


	PxBatchQuery* createVehicleBatchQuery(u8* mem)
	{
		const PxU32 maxNumQueriesInBatch = 64;
		const PxU32 maxNumHitResultsInBatch = 64;

		PxBatchQueryDesc desc(maxNumQueriesInBatch, maxNumQueriesInBatch, 0);

		desc.queryMemory.userRaycastResultBuffer = (PxRaycastQueryResult*)(mem + sizeof(PxRaycastHit) * 64);
		desc.queryMemory.userRaycastTouchBuffer = (PxRaycastHit*)mem;
		desc.queryMemory.raycastTouchBufferSize = maxNumHitResultsInBatch;

		m_vehicle_results = desc.queryMemory.userRaycastResultBuffer;

		auto f = [](PxFilterData queryFilterData, PxFilterData objectFilterData, const void* constantBlock, PxU32 constantBlockSize, PxHitFlags& hitFlags)->PxQueryHitType::Enum {
			if (objectFilterData.word3 == 4) return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		};

		desc.preFilterShader = f;
		//desc.postFilterShader = vehicleSceneQueryData.mPostFilterShader;

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


	~PhysicsSceneImpl()
	{
		m_vehicle_batch_query->release();
		m_vehicle_frictions->release();
		m_controller_manager->release();
		m_default_material->release();
		m_dummy_actor->release();
		m_scene->release();
	}


	int getVersion() const override { return (int)PhysicsSceneVersion::LATEST; }


	void clear() override
	{
		for (auto& controller : m_controllers)
		{
			controller.controller->release();
		}
		m_controllers.clear();

		for (auto& ragdoll : m_ragdolls)
		{
			destroySkeleton(ragdoll.root);
		}
		m_ragdolls.clear();

		for (auto& v : m_vehicles) {
			if (v->geom) {
				v->geom->getObserverCb().unbind<&Vehicle::onStateChanged>(v.get());
				v->geom->decRefCount();
			}
		}

		m_vehicles.clear();
		m_wheels.clear();

		for (auto& joint : m_joints)
		{
			joint.physx->release();
		}
		m_joints.clear();

		for (auto* actor : m_actors)
		{
			LUMIX_DELETE(m_allocator, actor);
		}
		m_actors.clear();
		m_dynamic_actors.clear();

		m_terrains.clear();
	}


	void onTrigger(EntityRef e1, EntityRef e2, bool touch_lost)
	{
		if (!m_script_scene) return;

		auto send = [this, touch_lost](EntityRef e1, EntityRef e2) {
			if (!m_script_scene->getUniverse().hasComponent(e1, LUA_SCRIPT_TYPE)) return;

			for (int i = 0, c = m_script_scene->getScriptCount(e1); i < c; ++i)
			{
				auto* call = m_script_scene->beginFunctionCall(e1, i, "onTrigger");
				if (!call) continue;

				call->add(e2);
				call->add(touch_lost);
				m_script_scene->endFunctionCall();
			}
		};

		send(e1, e2);
		send(e2, e1);
	}

	void onControllerHit(EntityRef controller, EntityRef obj) {
		if (!m_script_scene) return;
		if (!m_script_scene->getUniverse().hasComponent(controller, LUA_SCRIPT_TYPE)) return;

		for (int i = 0, c = m_script_scene->getScriptCount(controller); i < c; ++i) {
			auto* call = m_script_scene->beginFunctionCall(controller, i, "onControllerHit");
			if (!call) continue;

			call->add(obj);
			m_script_scene->endFunctionCall();
		}
	}

	void onContact(const ContactData& contact_data)
	{
		if (!m_script_scene) return;

		auto send = [this](EntityRef e1, EntityRef e2, const Vec3& position) {
			if (!m_script_scene->getUniverse().hasComponent(e1, LUA_SCRIPT_TYPE)) return;

			for (int i = 0, c = m_script_scene->getScriptCount(e1); i < c; ++i)
			{
				auto* call = m_script_scene->beginFunctionCall(e1, i, "onContact");
				if (!call) continue;

				call->add(e2.index);
				call->add(position.x);
				call->add(position.y);
				call->add(position.z);
				m_script_scene->endFunctionCall();
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


	Universe& getUniverse() override { return m_universe; }


	IPlugin& getPlugin() const override { return *m_system; }


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


	void setRagdollLayer(EntityRef entity, u32 layer) override
	{
		auto& ragdoll = m_ragdolls[entity];
		ragdoll.layer = layer;
		struct Tmp
		{
			void operator()(RagdollBone* bone, int layer)
			{
				if (!bone) return;
				if (bone->actor) scene.updateFilterData(bone->actor, layer);
				(*this)(bone->child, layer);
				(*this)(bone->next, layer);
			}

			PhysicsSceneImpl& scene;
		};
		Tmp tmp{*this};
		tmp(ragdoll.root, layer);
	}


	u32 getRagdollLayer(EntityRef entity) override { return m_ragdolls[entity].layer; }


	void setActorLayer(EntityRef entity, u32 layer) override
	{
		ASSERT(layer < lengthOf(m_layers.names));
		auto* actor = m_actors[entity];
		actor->layer = layer;
		if (actor->physx_actor)
		{
			updateFilterData(actor->physx_actor, actor->layer);
		}
	}


	u32 getActorLayer(EntityRef entity) override { return m_actors[entity]->layer; }

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

	void setVehicleAccel(EntityRef entity, bool accel) override {
		m_vehicles[entity]->raw_input.setAnalogAccel(accel ? 1.f : 0.f);
	}

	void setVehicleSteer(EntityRef entity, float value) override {
		m_vehicles[entity]->raw_input.setAnalogSteer(value);
	}

	void setVehicleBrake(EntityRef entity, float value) override {
		m_vehicles[entity]->raw_input.setAnalogBrake(value);
	}

	void rebuildWheel(EntityRef entity)
	{
		if (!m_is_game_running) return;

		const EntityPtr veh_entity = m_universe.getParent(entity);
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
					heights[idx].setTessFlag();
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
					heights[idx].setTessFlag();
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

		Transform connected_body_tr = m_universe.getTransform((EntityRef)joint.connected_body);
		RigidTransform unscaled_connected_body_tr = {connected_body_tr.pos, connected_body_tr.rot};
		Transform tr = m_universe.getTransform(entity);
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
		m_universe.onComponentDestroyed(entity, HEIGHTFIELD_TYPE, this);
	}


	void destroyController(EntityRef entity)
	{
		m_controllers[entity].controller->release();
		m_controllers.erase(entity);
		m_universe.onComponentDestroyed(entity, CONTROLLER_TYPE, this);
	}


	void destroyRagdoll(EntityRef entity)
	{
		auto iter = m_ragdolls.find(entity);
		destroySkeleton(iter.value().root);
		m_ragdolls.erase(iter);
		m_universe.onComponentDestroyed(entity, RAGDOLL_TYPE, this);
	}


	void destroyWheel(EntityRef entity)
	{
		m_wheels.erase(entity);
		m_universe.onComponentDestroyed(entity, WHEEL_TYPE, this);

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
		m_universe.onComponentDestroyed(entity, VEHICLE_TYPE, this);
	}


	void destroySphericalJoint(EntityRef entity) { destroyJointGeneric(entity, SPHERICAL_JOINT_TYPE); }
	void destroyHingeJoint(EntityRef entity) { destroyJointGeneric(entity, HINGE_JOINT_TYPE); }
	void destroyD6Joint(EntityRef entity) { destroyJointGeneric(entity, D6_JOINT_TYPE); }
	void destroyDistanceJoint(EntityRef entity) { destroyJointGeneric(entity, DISTANCE_JOINT_TYPE); }


	void destroyRigidActor(EntityRef entity)
	{
		auto* actor = m_actors[entity];
		actor->setPhysxActor(nullptr);
		LUMIX_DELETE(m_allocator, actor);
		m_actors.erase(entity);
		m_dynamic_actors.eraseItem(actor);
		m_universe.onComponentDestroyed(entity, RIGID_ACTOR_TYPE, this);
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
		m_universe.onComponentDestroyed(entity, type, this);
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

		m_universe.onComponentCreated(entity, DISTANCE_JOINT_TYPE, this);
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

		m_universe.onComponentCreated(entity, SPHERICAL_JOINT_TYPE, this);
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

		m_universe.onComponentCreated(entity, D6_JOINT_TYPE, this);
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

		m_universe.onComponentCreated(entity, HINGE_JOINT_TYPE, this);
	}


	void createHeightfield(EntityRef entity)
	{
		Heightfield& terrain = m_terrains.insert(entity);
		terrain.m_heightmap = nullptr;
		terrain.m_scene = this;
		terrain.m_actor = nullptr;
		terrain.m_entity = entity;

		m_universe.onComponentCreated(entity, HEIGHTFIELD_TYPE, this);
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


	void createController(EntityRef entity)
	{
		PxCapsuleControllerDesc cDesc;
		initControllerDesc(cDesc);
		DVec3 position = m_universe.getPosition(entity);
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

		m_universe.onComponentCreated(entity, CONTROLLER_TYPE, this);
	}

	void createWheel(EntityRef entity)
	{
		m_wheels.insert(entity, {});

		m_universe.onComponentCreated(entity, WHEEL_TYPE, this);
	}


	void createVehicle(EntityRef entity)
	{
		m_vehicles.insert(entity, UniquePtr<Vehicle>::create(m_allocator));
		m_universe.onComponentCreated(entity, VEHICLE_TYPE, this);
	}


	void createRagdoll(EntityRef entity)
	{
		Ragdoll& ragdoll = m_ragdolls.insert(entity);
		ragdoll.entity = entity;
		ragdoll.root = nullptr;
		ragdoll.layer = 0;
		ragdoll.root_transform.pos = DVec3(0, 0, 0);
		ragdoll.root_transform.rot.set(0, 0, 0, 1);

		m_universe.onComponentCreated(entity, RAGDOLL_TYPE, this);
	}


	void createRigidActor(EntityRef entity)
	{
		if (m_actors.find(entity).isValid()) {
			logError("Entity ", entity.index, " already has rigid actor");
			return;
		}
		RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this, entity);
		m_actors.insert(entity, actor);

		Transform transform = m_universe.getTransform(entity);
		PxTransform px_transform = toPhysx(transform.getRigidPart());

		PxRigidStatic* physx_actor = m_system->getPhysics()->createRigidStatic(px_transform);
		actor->setPhysxActor(physx_actor);

		m_universe.onComponentCreated(entity, RIGID_ACTOR_TYPE, this);
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
		auto& resource_manager = m_engine.getResourceManager();
		auto& terrain = m_terrains[entity];
		auto* old_hm = terrain.m_heightmap;
		if (old_hm)
		{
			old_hm->decRefCount();
			auto& cb = old_hm->getObserverCb();
			cb.unbind<&Heightfield::heightmapLoaded>(&terrain);
		}

		if (str.isValid())
		{
			auto* new_hm = resource_manager.load<Texture>(str);
			terrain.m_heightmap = new_hm;
			new_hm->onLoaded<&Heightfield::heightmapLoaded>(&terrain);
			new_hm->addDataReference();
		}
		else
		{
			terrain.m_heightmap = nullptr;
		}
	}


	bool isActorDebugEnabled(EntityRef entity) const override
	{
		auto* px_actor = m_actors[entity]->physx_actor;
		if (!px_actor) return false;
		return px_actor->getActorFlags().isSet(PxActorFlag::eVISUALIZATION);
	}


	void enableActorDebug(EntityRef entity, bool enable) const override
	{
		auto* px_actor = m_actors[entity]->physx_actor;
		if (!px_actor) return;
		px_actor->setActorFlag(PxActorFlag::eVISUALIZATION, enable);
		PxShape* shape;
		px_actor->getShapes(&shape, 1);
		if(shape) {
			shape->setFlag(PxShapeFlag::eVISUALIZATION, enable);
		}
	}


	void render() override
	{
		auto& render_scene = *static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		const PxRenderBuffer& rb = m_scene->getRenderBuffer();
		const PxU32 num_lines = minimum(100000U, rb.getNbLines());
		if (num_lines) {
			const PxDebugLine* PX_RESTRICT lines = rb.getLines();
			DebugLine* tmp = render_scene.addDebugLines(num_lines);
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
			DebugTriangle* tmp = render_scene.addDebugTriangles(num_tris);
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


	void updateDynamicActors()
	{
		PROFILE_FUNCTION();
		for (auto* actor : m_dynamic_actors)
		{
			m_update_in_progress = actor;
			PxTransform trans = actor->physx_actor->getGlobalPose();
			m_universe.setTransform(actor->entity, fromPhysx(trans));
		}
		m_update_in_progress = nullptr;

		for (auto iter = m_vehicles.begin(), end = m_vehicles.end(); iter != end; ++iter) {
			Vehicle* veh = iter.value().get();
			if (veh->actor) {
				const PxTransform car_trans = veh->actor->getGlobalPose();
				m_universe.setTransform(iter.key(), fromPhysx(car_trans));

				EntityPtr wheels[4];
				getWheels(iter.key(), Span(wheels));

				PxShape* shapes[5];
				veh->actor->getShapes(shapes, 5);
				for (u32 i = 0; i < 4; ++i) {
					if (!wheels[i].isValid()) continue;
					const PxTransform trans = shapes[i]->getLocalPose();
					m_universe.setTransform((EntityRef)wheels[i], fromPhysx(car_trans * trans));
				
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

			m_filter_callback.m_filter_data = controller.filter_data;
			PxControllerFilters filters(nullptr, &m_filter_callback);
			controller.controller->move(toPhysx(dif), 0.001f, time_delta, filters);
			PxExtendedVec3 p = controller.controller->getFootPosition();

			m_universe.setPosition(controller.entity, {p.x, p.y, p.z});
		}
	}


	static RagdollBone* getBone(RagdollBone* bone, int pose_bone_idx)
	{
		if (!bone) return nullptr;
		if (bone->pose_bone_idx == pose_bone_idx) return bone;

		auto* handle = getBone(bone->child, pose_bone_idx);
		if (handle) return handle;

		handle = getBone(bone->next, pose_bone_idx);
		if (handle) return handle;

		return nullptr;
	}


	PxCapsuleGeometry getCapsuleGeometry(RagdollBone* bone)
	{
		PxShape* shape;
		int count = bone->actor->getShapes(&shape, 1);
		ASSERT(count == 1);

		PxCapsuleGeometry geom;
		bool is_capsule = shape->getCapsuleGeometry(geom);
		ASSERT(is_capsule);

		return geom;
	}


	PxJoint* getRagdollBoneJoint(RagdollBone* bone) const override { return bone->parent_joint; }


	RagdollBone* getRagdollRootBone(EntityRef entity) const override { return m_ragdolls[entity].root; }


	RagdollBone* getRagdollBoneChild(RagdollBone* bone) override { return bone->child; }


	RagdollBone* getRagdollBoneSibling(RagdollBone* bone) override { return bone->next; }


	float getRagdollBoneHeight(RagdollBone* bone) override { return getCapsuleGeometry(bone).halfHeight * 2.0f; }


	float getRagdollBoneRadius(RagdollBone* bone) override { return getCapsuleGeometry(bone).radius; }


	void setRagdollBoneHeight(RagdollBone* bone, float value) override
	{
		if (value < 0) return;
		auto geom = getCapsuleGeometry(bone);
		geom.halfHeight = value * 0.5f;
		PxShape* shape;
		bone->actor->getShapes(&shape, 1);
		shape->setGeometry(geom);
	}


	void setRagdollBoneRadius(RagdollBone* bone, float value) override
	{
		if (value < 0) return;
		auto geom = getCapsuleGeometry(bone);
		geom.radius = value;
		PxShape* shape;
		bone->actor->getShapes(&shape, 1);
		shape->setGeometry(geom);
	}


	RigidTransform getRagdollBoneTransform(RagdollBone* bone) override
	{
		auto px_pose = bone->actor->getGlobalPose();
		return fromPhysx(px_pose);
	}


	void setRagdollBoneTransform(RagdollBone* bone, const RigidTransform& transform) override
	{
		EntityRef entity = {(int)(intptr_t)bone->actor->userData};

		auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(RENDERER_HASH));
		if (!render_scene) return;

		if (!render_scene->getUniverse().hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = render_scene->getModelInstanceModel(entity);
		RigidTransform entity_transform = m_universe.getTransform(entity).getRigidPart();

		bone->bind_transform =
			(entity_transform.inverted() * transform).inverted() * model->getBone(bone->pose_bone_idx).transform;
		bone->inv_bind_transform = bone->bind_transform.inverted();
		PxTransform delta = toPhysx(transform).getInverse() * bone->actor->getGlobalPose();
		bone->actor->setGlobalPose(toPhysx(transform));

		if (bone->parent_joint)
		{
			PxTransform local_pose1 = bone->parent_joint->getLocalPose(PxJointActorIndex::eACTOR1);
			bone->parent_joint->setLocalPose(PxJointActorIndex::eACTOR1, delta * local_pose1);
		}
		auto* child = bone->child;
		while (child && child->parent_joint)
		{
			PxTransform local_pose0 = child->parent_joint->getLocalPose(PxJointActorIndex::eACTOR0);
			child->parent_joint->setLocalPose(PxJointActorIndex::eACTOR0, delta * local_pose0);
			child = child->next;
		}
	}


	const char* getRagdollBoneName(RagdollBone* bone) override
	{
		EntityRef entity = {(int)(intptr_t)(void*)bone->actor->userData};
		auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(RENDERER_HASH));
		ASSERT(render_scene);

		Model* model = render_scene->getModelInstanceModel(entity);
		ASSERT(model && model->isReady());

		return model->getBone(bone->pose_bone_idx).name.c_str();
	}


	RagdollBone* getRagdollBoneByName(EntityRef entity, u32 bone_name_hash) override
	{
		auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(RENDERER_HASH));
		ASSERT(render_scene);

		Model* model = render_scene->getModelInstanceModel(entity);
		ASSERT(model && model->isReady());

		auto iter = model->getBoneIndex(bone_name_hash);
		ASSERT(iter.isValid());

		return getBone(m_ragdolls[entity].root, iter.value());
	}


	RagdollBone* getPhyParent(EntityRef entity, Model* model, int bone_index)
	{
		auto* bone = &model->getBone(bone_index);
		if (bone->parent_idx < 0) return nullptr;
		RagdollBone* phy_bone;
		do
		{
			bone = &model->getBone(bone->parent_idx);
			phy_bone = getRagdollBoneByName(entity, crc32(bone->name.c_str()));
		} while (!phy_bone && bone->parent_idx >= 0);
		return phy_bone;
	}


	void destroyRagdollBone(EntityRef entity, RagdollBone* bone) override
	{
		disconnect(m_ragdolls[entity], bone);
		bone->actor->release();
		LUMIX_DELETE(m_allocator, bone);
	}


	void getRagdollData(EntityRef entity, OutputMemoryStream& blob) override
	{
		auto& ragdoll = m_ragdolls[entity];
		serializeRagdollBone(ragdoll, ragdoll.root, blob);
	}


	void setRagdollRoot(Ragdoll& ragdoll, RagdollBone* bone) const
	{
		ragdoll.root = bone;
		if (!bone) return;
		RigidTransform root_transform = fromPhysx(ragdoll.root->actor->getGlobalPose());
		RigidTransform entity_transform = m_universe.getTransform(ragdoll.entity).getRigidPart();
		ragdoll.root_transform = root_transform.inverted() * entity_transform;
	}


	void setRagdollData(EntityRef entity, InputMemoryStream& blob) override
	{
		auto& ragdoll = m_ragdolls[entity];
		setRagdollRoot(ragdoll, deserializeRagdollBone(ragdoll, nullptr, blob));
	}


	void setRagdollBoneKinematic(RagdollBone* bone, bool is_kinematic) override
	{
		bone->is_kinematic = is_kinematic;
		bone->actor->is<PxRigidBody>()->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, is_kinematic);
	}


	void setRagdollBoneKinematicRecursive(RagdollBone* bone, bool is_kinematic) override
	{
		if (!bone) return;
		bone->is_kinematic = is_kinematic;
		bone->actor->is<PxRigidBody>()->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, is_kinematic);
		setRagdollBoneKinematicRecursive(bone->child, is_kinematic);
		setRagdollBoneKinematicRecursive(bone->next, is_kinematic);
	}


	bool isRagdollBoneKinematic(RagdollBone* bone) override { return bone->is_kinematic; }


	void changeRagdollBoneJoint(RagdollBone* child, int type) override
	{
		if (child->parent_joint) child->parent_joint->release();

		PxJointConcreteType::Enum px_type = (PxJointConcreteType::Enum)type;
		auto d1 = child->actor->getGlobalPose().q.rotate(PxVec3(1, 0, 0));
		auto d2 = child->parent->actor->getGlobalPose().q.rotate(PxVec3(1, 0, 0));
		auto axis = d1.cross(d2).getNormalized();
		auto pos = child->parent->actor->getGlobalPose().p;
		auto diff = (pos - child->actor->getGlobalPose().p).getNormalized();
		if (diff.dot(d2) < 0) d2 = -d2;

		PxShape* shape;
		if (child->parent->actor->getShapes(&shape, 1) == 1)
		{
			PxCapsuleGeometry capsule;
			if (shape->getCapsuleGeometry(capsule))
			{
				pos -= (capsule.halfHeight + capsule.radius) * d2;
			}
		}

		PxMat44 mat(d1, axis, d1.cross(axis).getNormalized(), pos);
		PxTransform tr0 = child->parent->actor->getGlobalPose().getInverse() * PxTransform(mat);
		PxTransform tr1 = child->actor->getGlobalPose().getInverse() * child->parent->actor->getGlobalPose() * tr0;

		PxJoint* joint = nullptr;
		switch (px_type)
		{
			case PxJointConcreteType::eFIXED:
				joint = PxFixedJointCreate(m_scene->getPhysics(), child->parent->actor, tr0, child->actor, tr1);
				if (joint)
				{
					auto* fixed_joint = static_cast<PxFixedJoint*>(joint);
					fixed_joint->setProjectionLinearTolerance(0.1f);
					fixed_joint->setProjectionAngularTolerance(0.01f);
				}
				break;
			case PxJointConcreteType::eREVOLUTE:
				joint = PxRevoluteJointCreate(m_scene->getPhysics(), child->parent->actor, tr0, child->actor, tr1);
				if (joint)
				{
					auto* hinge = static_cast<PxRevoluteJoint*>(joint);
					hinge->setProjectionLinearTolerance(0.1f);
					hinge->setProjectionAngularTolerance(0.01f);
				}
				break;
			case PxJointConcreteType::eSPHERICAL:
				joint = PxSphericalJointCreate(m_scene->getPhysics(), child->parent->actor, tr0, child->actor, tr1);
				if (joint)
				{
					auto* spherical = static_cast<PxSphericalJoint*>(joint);
					spherical->setProjectionLinearTolerance(0.1f);
				}
				break;
			case PxJointConcreteType::eD6:
				joint = PxD6JointCreate(m_scene->getPhysics(), child->parent->actor, tr0, child->actor, tr1);
				if (joint)
				{
					PxD6Joint* d6 = ((PxD6Joint*)joint);
					d6->setProjectionLinearTolerance(0.1f);
					d6->setProjectionAngularTolerance(0.01f);
					PxJointLinearLimit l = d6->getLinearLimit();
					l.value = 0.01f;
					d6->setLinearLimit(l);
					d6->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
					d6->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);
					d6->setProjectionAngularTolerance(0.01f);
					d6->setProjectionLinearTolerance(0.1f);
					d6->setSwingLimit(PxJointLimitCone(degreesToRadians(30), degreesToRadians(30)));
				}
				break;
			default: ASSERT(false); break;
		}

		if (joint)
		{
			joint->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
			joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);
			joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, true);
		}
		child->parent_joint = joint;
	}


	void disconnect(Ragdoll& ragdoll, RagdollBone* bone)
	{
		auto* child = bone->child;
		auto* parent = bone->parent;
		if (parent && parent->child == bone) parent->child = bone->next;
		if (ragdoll.root == bone) setRagdollRoot(ragdoll, bone->next);
		if (bone->prev) bone->prev->next = bone->next;
		if (bone->next) bone->next->prev = bone->prev;

		while (child)
		{
			auto* next = child->next;

			if (child->parent_joint) child->parent_joint->release();
			child->parent_joint = nullptr;

			if (parent)
			{
				child->next = parent->child;
				child->prev = nullptr;
				if (child->next) child->next->prev = child;
				parent->child = child;
				child->parent = parent;
				changeRagdollBoneJoint(child, PxJointConcreteType::eREVOLUTE);
			}
			else
			{
				child->parent = nullptr;
				child->next = ragdoll.root;
				child->prev = nullptr;
				if (child->next) child->next->prev = child;
				setRagdollRoot(ragdoll, child);
			}
			child = next;
		}
		if (bone->parent_joint) bone->parent_joint->release();
		bone->parent_joint = nullptr;

		bone->parent = nullptr;
		bone->child = nullptr;
		bone->prev = nullptr;
		bone->next = ragdoll.root;
		if (bone->next) bone->next->prev = bone;
	}


	void connect(Ragdoll& ragdoll, RagdollBone* child, RagdollBone* parent)
	{
		ASSERT(!child->parent);
		ASSERT(!child->child);
		if (child->next) child->next->prev = child->prev;
		if (child->prev) child->prev->next = child->next;
		if (ragdoll.root == child) setRagdollRoot(ragdoll, child->next);
		child->next = parent->child;
		if (child->next) child->next->prev = child;
		parent->child = child;
		child->parent = parent;
		changeRagdollBoneJoint(child, PxJointConcreteType::eD6);
	}


	void findCloserChildren(Ragdoll& ragdoll, EntityRef entity, Model* model, RagdollBone* bone)
	{
		for (auto* root = ragdoll.root; root; root = root->next)
		{
			if (root == bone) continue;

			auto* tmp = getPhyParent(entity, model, root->pose_bone_idx);
			if (tmp != bone) continue;

			disconnect(ragdoll, root);
			connect(ragdoll, root, bone);
			break;
		}
		if (!bone->parent) return;

		for (auto* child = bone->parent->child; child; child = child->next)
		{
			if (child == bone) continue;

			auto* tmp = getPhyParent(entity, model, bone->pose_bone_idx);
			if (tmp != bone) continue;

			disconnect(ragdoll, child);
			connect(ragdoll, child, bone);
		}
	}

	RigidTransform getNewBoneTransform(const Model* model, int bone_idx, float& length)
	{
		/*const Model::Bone& bone = model->getBone(bone_idx);

		length = 0.1f;
		for (int i = 0; i < model->getBoneCount(); ++i)
		{
			if (model->getBone(i).parent_idx == bone_idx)
			{
				length = (bone.transform.pos - model->getBone(i).transform.pos).length();
				break;
			}
		}

		Matrix mtx = bone.transform.toMatrix();
		if (m_new_bone_orientation == BoneOrientation::X)
		{
			Vec3 x = mtx.getXVector();
			mtx.setXVector(-mtx.getYVector());
			mtx.setYVector(x);
			mtx.setTranslation(mtx.getTranslation() - mtx.getXVector() * length * 0.5f);
			return mtx.toTransform();
		}
		mtx.setTranslation(mtx.getTranslation() + mtx.getXVector() * length * 0.5f);
		return mtx.toTransform();*/
		// TODO
		return {};
	}


	BoneOrientation getNewBoneOrientation() const override { return m_new_bone_orientation; }
	void setNewBoneOrientation(BoneOrientation orientation) override { m_new_bone_orientation = orientation; }


	RagdollBone* createRagdollBone(EntityRef entity, u32 bone_name_hash) override
	{
		auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(RENDERER_HASH));
		ASSERT(render_scene);

		Model* model = render_scene->getModelInstanceModel(entity);
		ASSERT(model && model->isReady());
		auto iter = model->getBoneIndex(bone_name_hash);
		ASSERT(iter.isValid());

		auto* new_bone = LUMIX_NEW(m_allocator, RagdollBone);
		new_bone->child = new_bone->next = new_bone->prev = new_bone->parent = nullptr;
		new_bone->parent_joint = nullptr;
		new_bone->is_kinematic = false;
		new_bone->pose_bone_idx = iter.value();

		float bone_height;
		RigidTransform transform = getNewBoneTransform(model, iter.value(), bone_height);

		new_bone->bind_transform = transform.inverted() * model->getBone(iter.value()).transform;
		new_bone->inv_bind_transform = new_bone->bind_transform.inverted();
		transform = m_universe.getTransform(entity).getRigidPart() * transform;

		PxCapsuleGeometry geom;
		geom.halfHeight = bone_height * 0.3f;
		if (geom.halfHeight < 0.001f) geom.halfHeight = 1.0f;
		geom.radius = geom.halfHeight * 0.5f;

		PxTransform px_transform = toPhysx(transform);
		new_bone->actor = PxCreateDynamic(m_scene->getPhysics(), px_transform, geom, *m_default_material, 1.0f);
		new_bone->actor->is<PxRigidDynamic>()->setMass(0.0001f);
		new_bone->actor->userData = (void*)(intptr_t)entity.index;
		new_bone->actor->setActorFlag(PxActorFlag::eVISUALIZATION, true);
		new_bone->actor->is<PxRigidDynamic>()->setSolverIterationCounts(8, 8);
		m_scene->addActor(*new_bone->actor);
		updateFilterData(new_bone->actor, 0);

		auto& ragdoll = m_ragdolls[entity];
		new_bone->next = ragdoll.root;
		if (new_bone->next) new_bone->next->prev = new_bone;
		setRagdollRoot(ragdoll, new_bone);
		auto* parent = getPhyParent(entity, model, iter.value());
		if (parent) connect(ragdoll, new_bone, parent);

		findCloserChildren(ragdoll, entity, model, new_bone);

		return new_bone;
	}


	void setSkeletonPose(const RigidTransform& root_transform, RagdollBone* bone, const Pose* pose)
	{
		if (!bone) return;

		RigidTransform bone_transform(
			DVec3(pose->positions[bone->pose_bone_idx]), pose->rotations[bone->pose_bone_idx]);
		bone->actor->setGlobalPose(toPhysx(root_transform * bone_transform * bone->inv_bind_transform));

		setSkeletonPose(root_transform, bone->next, pose);
		setSkeletonPose(root_transform, bone->child, pose);
	}


	void updateBone(const RigidTransform& root_transform, const RigidTransform& inv_root, RagdollBone* bone, Pose* pose)
	{
		/*if (!bone) return;

		if (bone->is_kinematic)
		{
			RigidTransform bone_transform(DVec3(pose->positions[bone->pose_bone_idx]),
		pose->rotations[bone->pose_bone_idx]); bone->actor->setKinematicTarget(toPhysx(root_transform * bone_transform *
		bone->inv_bind_transform));
		}
		else
		{
			PxTransform bone_pose = bone->actor->getGlobalPose();
			auto tr = inv_root * RigidTransform(DVec3(fromPhysx(bone_pose.p)), fromPhysx(bone_pose.q)) *
		bone->bind_transform; pose->rotations[bone->pose_bone_idx] = tr.rot; pose->positions[bone->pose_bone_idx] =
		tr.pos;
		}

		updateBone(root_transform, inv_root, bone->next, pose);
		updateBone(root_transform, inv_root, bone->child, pose);*/

		// TODO
	}


	void updateRagdolls()
	{
		auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(RENDERER_HASH));
		if (!render_scene) return;

		for (auto& ragdoll : m_ragdolls)
		{
			EntityRef entity = ragdoll.entity;

			if (!render_scene->getUniverse().hasComponent(entity, MODEL_INSTANCE_TYPE)) continue;
			Pose* pose = render_scene->lockPose(entity);
			if (!pose) continue;

			RigidTransform root_transform;
			root_transform.rot = m_universe.getRotation(ragdoll.entity);
			root_transform.pos = m_universe.getPosition(ragdoll.entity);

			if (ragdoll.root && !ragdoll.root->is_kinematic)
			{
				PxTransform bone_pose = ragdoll.root->actor->getGlobalPose();
				m_is_updating_ragdoll = true;

				RigidTransform rigid_tr = fromPhysx(bone_pose) * ragdoll.root_transform;
				m_universe.setTransform(ragdoll.entity, {rigid_tr.pos, rigid_tr.rot, 1.0f});

				m_is_updating_ragdoll = false;
			}
			updateBone(root_transform, root_transform.inverted(), ragdoll.root, pose);
			render_scene->unlockPose(entity, true);
		}
	}

	void updateVehicles(float time_delta) {
		PxVehicleWheels* vehicles[16];
		const u32 count = (u32)m_vehicles.size();
		ASSERT(count <= lengthOf(vehicles)); // TODO

		u32 valid_count = 0;
		for (auto iter = m_vehicles.begin(), end = m_vehicles.end(); iter != end; ++iter) {
			Vehicle* veh = iter.value().get();
			if (veh->drive) {
				vehicles[valid_count] = veh->drive;
				PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(pad_smoothing, steer_vs_forward_speed, veh->raw_input, time_delta, false, *veh->drive);
				++valid_count;
			}
		}
		if (valid_count > 0) {
			PxVehicleSuspensionRaycasts(m_vehicle_batch_query, valid_count, vehicles, valid_count * 4, m_vehicle_results);
			PxVehicleUpdates(time_delta, m_scene->getGravity(), *m_vehicle_frictions, valid_count, vehicles, nullptr);
		}
	}

	void lateUpdate(float time_delta, bool paused) override {
		if (!m_is_game_running || paused) return;

		AnimationScene* anim_scene = (AnimationScene*)m_universe.getScene(crc32("animation"));
		if (!anim_scene) return;

		for (Controller& ctrl : m_controllers) {
			if (ctrl.use_root_motion) {
				const LocalRigidTransform tr = anim_scene->getAnimatorRootMotion(ctrl.entity);
				const Quat rot = m_universe.getRotation(ctrl.entity);
				ctrl.frame_change += rot.rotate(tr.pos);
				m_universe.setRotation(ctrl.entity, rot * tr.rot);
			}
		}
	}

	void update(float time_delta, bool paused) override
	{
		if (!m_is_game_running || paused) return;

		time_delta = minimum(1 / 20.0f, time_delta);
		updateVehicles(time_delta);
		simulateScene(time_delta);
		fetchResults();
		updateRagdolls();
		updateDynamicActors();
		updateControllers(time_delta);

		render();
	}


	DelegateList<void(const ContactData&)>& onContact() override { return m_contact_callbacks; }


	void initJoint(EntityRef entity, Joint& joint)
	{
		PxRigidActor* actors[2] = {nullptr, nullptr};
		auto iter = m_actors.find(entity);
		if (iter.isValid()) actors[0] = iter.value()->physx_actor;
		iter = joint.connected_body.isValid() ? m_actors.find((EntityRef)joint.connected_body) : m_actors.end();
		if (iter.isValid()) actors[1] = iter.value()->physx_actor;
		if (!actors[0] || !actors[1]) return;

		DVec3 pos0 = m_universe.getPosition(entity);
		Quat rot0 = m_universe.getRotation(entity);
		DVec3 pos1 = m_universe.getPosition((EntityRef)joint.connected_body);
		Quat rot1 = m_universe.getRotation((EntityRef)joint.connected_body);
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
		const Transform& chassis_tr = m_universe.getTransform(entity);
		const PxF32 camber_angle_at_rest = 0.0;
		const PxF32 camber_angle_at_max_droop = 0.01f;
		const PxF32 camber_angle_at_max_compression = -0.01f;

		wheels[PxVehicleDrive4WWheelOrder::eREAR_LEFT].mMaxHandBrakeTorque = 4000.0f;
		wheels[PxVehicleDrive4WWheelOrder::eREAR_RIGHT].mMaxHandBrakeTorque = 4000.0f;
		wheels[PxVehicleDrive4WWheelOrder::eFRONT_LEFT].mMaxSteer = PxPi * 0.3333f;
		wheels[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT].mMaxSteer = PxPi * 0.3333f;

		for (EntityPtr e = m_universe.getFirstChild(entity); e.isValid(); e = m_universe.getNextSibling((EntityRef)e)) {
			if (!m_universe.hasComponent((EntityRef)e, WHEEL_TYPE)) continue;

			const Wheel& w = m_wheels[(EntityRef)e];
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

			const Transform& wheel_tr = m_universe.getTransform((EntityRef)e);
			offsets[idx] = toPhysx((chassis_tr.inverted() * wheel_tr).pos - vehicle.center_of_mass);
			
			wheel_sim_data->setTireData(idx, tire);
			wheel_sim_data->setSuspTravelDirection(idx, PxVec3(0, -1, 0));
			wheel_sim_data->setWheelCentreOffset(idx, offsets[idx]);
			wheel_sim_data->setSuspForceAppPointOffset(idx, offsets[idx] + PxVec3(0, 0.1f, 0));
			wheel_sim_data->setTireForceAppPointOffset(idx, offsets[idx] + PxVec3(0, 0.1f, 0));
			wheel_sim_data->setWheelShapeMapping(idx, idx);
			// TODO
			physx::PxFilterData filter;
			filter.word0 = 5;
			filter.word1 = 5;
			filter.word2 = 5;
			filter.word3 = 4;
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


	static void setupDriveSimData(const PxVehicleWheelsSimData& wheel_sim_data, PxVehicleDriveSimData4W& drive_sim_data)
	{
		//Diff
		PxVehicleDifferential4WData diff;
		diff.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
		drive_sim_data.setDiffData(diff);

		//Engine
		PxVehicleEngineData engine;
		engine.mPeakTorque = 500.0f;
		engine.mMaxOmega = 600.0f;//approx 6000 rpm
		drive_sim_data.setEngineData(engine);

		//Gears
		PxVehicleGearsData gears;
		gears.mSwitchTime = 0.5f;
		drive_sim_data.setGearsData(gears);

		//Clutch
		PxVehicleClutchData clutch;
		clutch.mStrength = 10.0f;
		drive_sim_data.setClutchData(clutch);

		//Ackermann steer accuracy
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


	PxRigidDynamic* createVehicleActor(const RigidTransform& transform, Span<const EntityRef> wheels_entities, Vehicle& vehicle)
	{
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
			filter.word3 = 4;
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
			filter.word3 = 4;
			PxMeshScale pxscale(1.f);
			PxConvexMeshGeometry convex_geom(vehicle.geom->convex_mesh, pxscale);
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
		setupDriveSimData(*wheel_sim_data, drive_sim_data);

		const RigidTransform tr = m_universe.getTransform(entity).getRigidPart();

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
		for (EntityPtr& e : wheels) e= INVALID_ENTITY;
		for (EntityPtr e = m_universe.getFirstChild(car); e.isValid(); e = m_universe.getNextSibling((EntityRef)e)) {
			if (m_universe.hasComponent((EntityRef)e, WHEEL_TYPE)) {
				const Wheel& w = m_wheels[(EntityRef)e];
				wheels[(i32)w.slot] = e;
			}
		}
	}

	void getTransforms(Span<const EntityRef> entities, Span<RigidTransform> transforms) {
		for (u32 i = 0; i < entities.length(); ++i) {
			transforms[i] = m_universe.getTransform(entities[i]).getRigidPart();
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
		auto* scene = m_universe.getScene(crc32("lua_script"));
		m_script_scene = static_cast<LuaScriptScene*>(scene);
		m_is_game_running = true;

		initJoints();
		initVehicles();

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

		RigidActor* actor = iter.value();
		if (!actor->physx_actor) return;

		PxRigidBody* rigid_body = actor->physx_actor->is<PxRigidBody>();
		if (!rigid_body) return;

		PxRigidBodyExt::addForceAtPos(*rigid_body, toPhysx(force), toPhysx(pos));
	}


	void setRagdollKinematic(EntityRef entity, bool is_kinematic) override
	{
		setRagdollBoneKinematicRecursive(m_ragdolls[entity].root, is_kinematic);
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
		bool canLayersCollide(int layer1, int layer2) const { return (scene->m_layers.filter[layer1] & (1 << layer2)) != 0; }

		PxQueryHitType::Enum preFilter(const PxFilterData& filterData,
			const PxShape* shape,
			const PxRigidActor* actor,
			PxHitFlags& queryFlags) override
		{
			if (layer >= 0)
			{
				const EntityRef hit_entity = {(int)(intptr_t)actor->userData};
				const auto iter = scene->m_actors.find(hit_entity);
				if (iter.isValid())
				{
					const RigidActor* actor = iter.value();
					if (!canLayersCollide(actor->layer, layer)) return PxQueryHitType::eNONE;
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
		PhysicsSceneImpl* scene;
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
		filter.scene = this;
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
			if (actor) result.entity = {(int)(intptr_t)actor->userData};
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
		const u64 cmp_mask = m_universe.getComponentsMask(entity);
		if ((cmp_mask & m_physics_cmps_mask) == 0) return;
		
		if (m_universe.hasComponent(entity, CONTROLLER_TYPE)) {
			auto iter = m_controllers.find(entity);
			if (iter.isValid())
			{
				Controller& controller = iter.value();
				DVec3 pos = m_universe.getPosition(entity);
				PxExtendedVec3 pvec(pos.x, pos.y, pos.z);
				controller.controller->setFootPosition(pvec);
			}
		}

		if (m_universe.hasComponent(entity, RAGDOLL_TYPE)) {
			auto iter = m_ragdolls.find(entity);
			if (iter.isValid() && !m_is_updating_ragdoll)
			{
				auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(RENDERER_HASH));
				if (!render_scene) return;
				if (!m_universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

				const Pose* pose = render_scene->lockPose(entity);
				if (!pose) return;
				setSkeletonPose(m_universe.getTransform(entity).getRigidPart(), iter.value().root, pose);
				render_scene->unlockPose(entity, false);
			}
		}

		if (m_universe.hasComponent(entity, RIGID_ACTOR_TYPE)) {
			auto iter = m_actors.find(entity);
			if (iter.isValid())
			{
				RigidActor* actor = iter.value();
				if (actor->physx_actor && m_update_in_progress != actor)
				{
					Transform trans = m_universe.getTransform(entity);
					if (actor->dynamic_type == DynamicType::KINEMATIC)
					{
						auto* rigid_dynamic = (PxRigidDynamic*)actor->physx_actor;
						rigid_dynamic->setKinematicTarget(toPhysx(trans.getRigidPart()));
					}
					else
					{
						actor->physx_actor->setGlobalPose(toPhysx(trans.getRigidPart()), false);
					}
					if (actor->resource && actor->scale != trans.scale)
					{
						actor->rescale();
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

			PxTransform transform = toPhysx(m_universe.getTransform(terrain.m_entity).getRigidPart());
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
		PxShape* shapes[8];
		int shapes_count = actor->getShapes(shapes, lengthOf(shapes));
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}
	}


	void updateFilterData()
	{
		for (auto& ragdoll : m_ragdolls)
		{
			struct Tmp
			{
				void operator()(RagdollBone* bone)
				{
					if (!bone) return;
					int shapes_count = bone->actor->getShapes(shapes, lengthOf(shapes));
					for (int i = 0; i < shapes_count; ++i)
					{
						shapes[i]->setSimulationFilterData(data);
					}
					(*this)(bone->child);
					(*this)(bone->next);
				}
				PxShape* shapes[8];
				PxFilterData data;
			};

			Tmp tmp;
			int layer = ragdoll.layer;
			tmp.data.word0 = 1 << layer;
			tmp.data.word1 = m_layers.filter[layer];
			tmp(ragdoll.root);
		}

		for (auto* actor : m_actors)
		{
			if (!actor->physx_actor) continue;
			PxFilterData data;
			int actor_layer = actor->layer;
			data.word0 = 1 << actor_layer;
			data.word1 = m_layers.filter[actor_layer];
			PxShape* shapes[8];
			int shapes_count = actor->physx_actor->getShapes(shapes, lengthOf(shapes));
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


	bool getIsTrigger(EntityRef entity) override { return m_actors[entity]->is_trigger; }


	void setIsTrigger(EntityRef entity, bool is_trigger) override
	{
		RigidActor* actor = m_actors[entity];
		actor->is_trigger = is_trigger;
		if (actor->physx_actor)
		{
			PxShape* shape;
			if (actor->physx_actor->getShapes(&shape, 1) == 1)
			{
				if (is_trigger)
				{
					shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false); // must set false first
					shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
				}
				else
				{
					shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
					shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
				}
			}
		}
	}


	DynamicType getDynamicType(EntityRef entity) override { return m_actors[entity]->dynamic_type; }


	void moveShapeIndices(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxRigidActor* actor = m_actors[entity]->physx_actor;
		int count = getGeometryCount(actor, type);
		for (int i = index; i < count; ++i)
		{
			PxShape* shape = getShape(entity, i, type);
			shape->userData = (void*)(intptr_t)(i + 1); //-V1028
		}
	}


	void addBoxGeometry(EntityRef entity, int index) override
	{
		if (index == -1) index = getBoxGeometryCount(entity);
		moveShapeIndices(entity, index, PxGeometryType::eBOX);
		PxRigidActor* actor = m_actors[entity]->physx_actor;
		PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
		shape->userData = (void*)(intptr_t)index;
	}


	void removeGeometry(EntityRef entity, int index, PxGeometryType::Enum type)
	{
		PxRigidActor* actor = m_actors[entity]->physx_actor;
		int count = getGeometryCount(actor, type);
		PxShape* shape = getShape(entity, index, type);
		actor->detachShape(*shape);

		for (int i = index; i < count - 1; ++i)
		{
			PxShape* shape = getShape(entity, i, type);
			shape->userData = (void*)(intptr_t)(i - 1);
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
		PxRigidActor* actor = m_actors[entity]->physx_actor;
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
		PxRigidActor* actor = m_actors[entity]->physx_actor;
		return getGeometryCount(actor, PxGeometryType::eBOX);
	}

	Path getMeshGeomPath(EntityRef entity) override {
		RigidActor* actor = m_actors[entity];
		return actor->resource ? actor->resource->getPath() : Path();
	}
	
	void setMeshGeomPath(EntityRef entity, const Path& path) override {
		ResourceManagerHub& manager = m_engine.getResourceManager();
		PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(path);
		m_actors[entity]->setResource(geom_res);
	}

	void addSphereGeometry(EntityRef entity, int index) override
	{
		if (index == -1) index = getSphereGeometryCount(entity);
		moveShapeIndices(entity, index, PxGeometryType::eSPHERE);
		PxRigidActor* actor = m_actors[entity]->physx_actor;
		PxSphereGeometry geom;
		geom.radius = 1;
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
		shape->userData = (void*)(intptr_t)index;
	}


	void removeSphereGeometry(EntityRef entity, int index) override
	{
		removeGeometry(entity, index, PxGeometryType::eSPHERE);
	}


	int getSphereGeometryCount(EntityRef entity) override 
	{ 
		PxRigidActor* actor = m_actors[entity]->physx_actor;
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
		RigidActor* actor = m_actors[entity];
		if (actor->dynamic_type == new_value) return;

		actor->dynamic_type = new_value;
		if (new_value == DynamicType::DYNAMIC)
		{
			m_dynamic_actors.push(actor);
		}
		else
		{
			m_dynamic_actors.swapAndPopItem(actor);
		}
		if (!actor->physx_actor) return;

		PxTransform transform = toPhysx(m_universe.getTransform(actor->entity).getRigidPart());
		PxRigidActor* new_physx_actor;
		switch (actor->dynamic_type)
		{
			case DynamicType::DYNAMIC: new_physx_actor = m_system->getPhysics()->createRigidDynamic(transform); break;
			case DynamicType::KINEMATIC:
				new_physx_actor = m_system->getPhysics()->createRigidDynamic(transform);
				new_physx_actor->is<PxRigidBody>()->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
				break;
			case DynamicType::STATIC: new_physx_actor = m_system->getPhysics()->createRigidStatic(transform); break;
		}
		for (int i = 0, c = actor->physx_actor->getNbShapes(); i < c; ++i)
		{
			PxShape* shape;
			actor->physx_actor->getShapes(&shape, 1, i);
			duplicateShape(shape, new_physx_actor);
		}
		PxRigidBody* rigid_body = new_physx_actor->is<PxRigidBody>();
		if (rigid_body) {
			PxRigidBodyExt::updateMassAndInertia(*rigid_body, 1);
		}
		actor->setPhysxActor(new_physx_actor);
	}


	void duplicateShape(PxShape* shape, PxRigidActor* actor)
	{
		PxShape* new_shape;
		switch (shape->getGeometryType())
		{
			case PxGeometryType::eBOX:
			{
				PxBoxGeometry geom;
				shape->getBoxGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			case PxGeometryType::eSPHERE:
			{
				PxSphereGeometry geom;
				shape->getSphereGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			case PxGeometryType::eCONVEXMESH:
			{
				PxConvexMeshGeometry geom;
				shape->getConvexMeshGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			case PxGeometryType::eTRIANGLEMESH:
			{
				PxTriangleMeshGeometry geom;
				shape->getTriangleMeshGeometry(geom);
				new_shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *m_default_material);
				new_shape->setLocalPose(shape->getLocalPose());
				break;
			}
			default: ASSERT(false); return;
		}
		new_shape->userData = shape->userData;
	}


	void serializeActor(OutputMemoryStream& serializer, RigidActor* actor)
	{
		serializer.write(actor->layer);
		auto* px_actor = actor->physx_actor;
		PxShape* shape;
		int shape_count = px_actor->getNbShapes();
		serializer.writeString(actor->resource ? actor->resource->getPath().c_str() : "");
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
		for (auto* actor : m_actors)
		{
			serializer.write(actor->entity);
			serializer.write(actor->dynamic_type);
			serializer.write(actor->is_trigger);
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
		serializeRagdolls(serializer);
		serializeJoints(serializer);
		serializeVehicles(serializer);
	}


	void serializeRagdollJoint(RagdollBone* bone, OutputMemoryStream& serializer)
	{
		serializer.write(bone->parent_joint != nullptr);
		if (!bone->parent_joint) return;

		serializer.write((int)bone->parent_joint->getConcreteType());
		serializer.write(bone->parent_joint->getLocalPose(PxJointActorIndex::eACTOR0));
		serializer.write(bone->parent_joint->getLocalPose(PxJointActorIndex::eACTOR1));

		switch ((PxJointConcreteType::Enum)bone->parent_joint->getConcreteType())
		{
			case PxJointConcreteType::eFIXED: break;
			case PxJointConcreteType::eDISTANCE:
			{
				auto* joint = bone->parent_joint->is<PxDistanceJoint>();
				serializer.write(joint->getMinDistance());
				serializer.write(joint->getMaxDistance());
				serializer.write(joint->getTolerance());
				serializer.write(joint->getStiffness());
				serializer.write(joint->getDamping());
				u32 flags = (PxU32)joint->getDistanceJointFlags();
				serializer.write(flags);
				break;
			}
			case PxJointConcreteType::eREVOLUTE:
			{
				auto* joint = bone->parent_joint->is<PxRevoluteJoint>();
				serializer.write(joint->getLimit());
				u32 flags = (PxU32)joint->getRevoluteJointFlags();
				serializer.write(flags);
				break;
			}
			case PxJointConcreteType::eD6:
			{
				auto* joint = bone->parent_joint->is<PxD6Joint>();
				serializer.write(joint->getLinearLimit());
				serializer.write(joint->getSwingLimit());
				serializer.write(joint->getTwistLimit());
				serializer.write(joint->getMotion(PxD6Axis::eX));
				serializer.write(joint->getMotion(PxD6Axis::eY));
				serializer.write(joint->getMotion(PxD6Axis::eZ));
				serializer.write(joint->getMotion(PxD6Axis::eSWING1));
				serializer.write(joint->getMotion(PxD6Axis::eSWING2));
				serializer.write(joint->getMotion(PxD6Axis::eTWIST));
				break;
			}
			case PxJointConcreteType::eSPHERICAL:
			{
				auto* joint = bone->parent_joint->is<PxSphericalJoint>();
				serializer.write(joint->getLimitCone());
				u32 flags = (PxU32)joint->getSphericalJointFlags();
				serializer.write(flags);
				break;
			}
			default: ASSERT(false); break;
		}
	}


	void serializeRagdollBone(const Ragdoll& ragdoll, RagdollBone* bone, OutputMemoryStream& serializer)
	{
		if (!bone)
		{
			serializer.write(-1);
			return;
		}
		serializer.write(bone->pose_bone_idx);
		PxTransform pose = bone->actor->getGlobalPose();
		pose = toPhysx(m_universe.getTransform(ragdoll.entity).getRigidPart()).getInverse() * pose;
		serializer.write(fromPhysx(pose));
		serializer.write(bone->bind_transform);

		PxShape* shape;
		int shape_count = bone->actor->getShapes(&shape, 1);
		ASSERT(shape_count == 1);
		PxBoxGeometry box_geom;
		if (shape->getBoxGeometry(box_geom))
		{
			serializer.write(RagdollBone::BOX);
			serializer.write(box_geom.halfExtents);
		}
		else
		{
			PxCapsuleGeometry capsule_geom;
			bool is_capsule = shape->getCapsuleGeometry(capsule_geom);
			ASSERT(is_capsule);
			serializer.write(RagdollBone::CAPSULE);
			serializer.write(capsule_geom.halfHeight);
			serializer.write(capsule_geom.radius);
		}
		serializer.write(bone->actor->is<PxRigidBody>()->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC));

		serializeRagdollBone(ragdoll, bone->child, serializer);
		serializeRagdollBone(ragdoll, bone->next, serializer);

		serializeRagdollJoint(bone, serializer);
	}


	void deserializeRagdollJoint(RagdollBone* bone, InputMemoryStream& serializer)
	{
		bool has_joint;
		serializer.read(has_joint);
		if (!has_joint) return;

		int type;
		serializer.read(type);
		changeRagdollBoneJoint(bone, type);

		PxTransform local_poses[2];
		serializer.read(local_poses);
		bone->parent_joint->setLocalPose(PxJointActorIndex::eACTOR0, local_poses[0]);
		bone->parent_joint->setLocalPose(PxJointActorIndex::eACTOR1, local_poses[1]);

		switch ((PxJointConcreteType::Enum)type)
		{
			case PxJointConcreteType::eFIXED: break;
			case PxJointConcreteType::eDISTANCE:
			{
				auto* joint = bone->parent_joint->is<PxDistanceJoint>();
				PxReal value;
				serializer.read(value);
				joint->setMinDistance(value);
				serializer.read(value);
				joint->setMaxDistance(value);
				serializer.read(value);
				joint->setTolerance(value);
				serializer.read(value);
				joint->setStiffness(value);
				serializer.read(value);
				joint->setDamping(value);
				u32 flags;
				serializer.read(flags);
				joint->setDistanceJointFlags((PxDistanceJointFlags)flags);
				break;
			}
			case PxJointConcreteType::eREVOLUTE:
			{
				auto* joint = bone->parent_joint->is<PxRevoluteJoint>();
				PxJointAngularLimitPair limit(0, 0);
				serializer.read(limit);
				joint->setLimit(limit);
				u32 flags;
				serializer.read(flags);
				joint->setRevoluteJointFlags((PxRevoluteJointFlags)flags);
				break;
			}
			case PxJointConcreteType::eSPHERICAL:
			{
				auto* joint = bone->parent_joint->is<PxSphericalJoint>();
				PxJointLimitCone limit(0, 0);
				serializer.read(limit);
				joint->setLimitCone(limit);
				u32 flags;
				serializer.read(flags);
				joint->setSphericalJointFlags((PxSphericalJointFlags)flags);
				break;
			}
			case PxJointConcreteType::eD6:
			{
				auto* joint = bone->parent_joint->is<PxD6Joint>();

				PxJointLinearLimit linear_limit(0, PxSpring(0, 0));
				serializer.read(linear_limit);
				joint->setLinearLimit(linear_limit);

				PxJointLimitCone swing_limit(0, 0);
				serializer.read(swing_limit);
				joint->setSwingLimit(swing_limit);

				PxJointAngularLimitPair twist_limit(0, 0);
				serializer.read(twist_limit);
				joint->setTwistLimit(twist_limit);

				PxD6Motion::Enum motions[6];
				serializer.read(motions);
				joint->setMotion(PxD6Axis::eX, motions[0]);
				joint->setMotion(PxD6Axis::eY, motions[1]);
				joint->setMotion(PxD6Axis::eZ, motions[2]);
				joint->setMotion(PxD6Axis::eSWING1, motions[3]);
				joint->setMotion(PxD6Axis::eSWING2, motions[4]);
				joint->setMotion(PxD6Axis::eTWIST, motions[5]);
				break;
			}
			default: ASSERT(false); break;
		}
	}


	RagdollBone* deserializeRagdollBone(Ragdoll& ragdoll, RagdollBone* parent, InputMemoryStream& serializer)
	{
		int pose_bone_idx;
		serializer.read(pose_bone_idx);
		if (pose_bone_idx < 0) return nullptr;
		auto* bone = LUMIX_NEW(m_allocator, RagdollBone);
		bone->pose_bone_idx = pose_bone_idx;
		bone->parent_joint = nullptr;
		bone->is_kinematic = false;
		bone->prev = nullptr;
		RigidTransform transform;
		serializer.read(transform);
		serializer.read(bone->bind_transform);
		bone->inv_bind_transform = bone->bind_transform.inverted();

		PxTransform px_transform = toPhysx(m_universe.getTransform(ragdoll.entity).getRigidPart()) * toPhysx(transform);

		RagdollBone::Type type;
		serializer.read(type);

		switch (type)
		{
			case RagdollBone::CAPSULE:
			{
				PxCapsuleGeometry shape;
				serializer.read(shape.halfHeight);
				serializer.read(shape.radius);
				bone->actor = PxCreateDynamic(m_scene->getPhysics(), px_transform, shape, *m_default_material, 1.0f);
				break;
			}
			case RagdollBone::BOX:
			{
				PxBoxGeometry shape;
				serializer.read(shape.halfExtents);
				bone->actor = PxCreateDynamic(m_scene->getPhysics(), px_transform, shape, *m_default_material, 1.0f);
				break;
			}
			default: ASSERT(false); break;
		}
		serializer.read(bone->is_kinematic);
		bone->actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, bone->is_kinematic);
		bone->actor->is<PxRigidDynamic>()->setSolverIterationCounts(8, 8);
		bone->actor->is<PxRigidDynamic>()->setMass(0.0001f);
		bone->actor->userData = (void*)(intptr_t)ragdoll.entity.index;

		bone->actor->setActorFlag(PxActorFlag::eVISUALIZATION, true);
		m_scene->addActor(*bone->actor);
		updateFilterData(bone->actor, ragdoll.layer);

		bone->parent = parent;

		bone->child = deserializeRagdollBone(ragdoll, bone, serializer);
		bone->next = deserializeRagdollBone(ragdoll, parent, serializer);
		if (bone->next) bone->next->prev = bone;

		deserializeRagdollJoint(bone, serializer);

		return bone;
	}


	void serializeRagdolls(OutputMemoryStream& serializer)
	{
		serializer.write(m_ragdolls.size());
		for (const Ragdoll& ragdoll : m_ragdolls)
		{
			serializer.write(ragdoll.entity);
			serializer.write(ragdoll.layer);
			serializeRagdollBone(ragdoll, ragdoll.root, serializer);
		}
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


	void deserializeActors(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_actors.reserve(count + m_actors.size());
		for (u32 j = 0; j < count; ++j) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this, entity);
			serializer.read(actor->dynamic_type);
			serializer.read(actor->is_trigger);
			if (actor->dynamic_type == DynamicType::DYNAMIC) m_dynamic_actors.push(actor);
			m_actors.insert(actor->entity, actor);
			actor->layer = 0;
			serializer.read(actor->layer);
			
			const char* path = serializer.readString();

			PxTransform transform = toPhysx(m_universe.getTransform(actor->entity).getRigidPart());
			PxRigidActor* physx_actor = actor->dynamic_type == DynamicType::STATIC
				? (PxRigidActor*)m_system->getPhysics()->createRigidStatic(transform)
				: (PxRigidActor*)m_system->getPhysics()->createRigidDynamic(transform);
			if (actor->dynamic_type == DynamicType::KINEMATIC) {
				physx_actor->is<PxRigidBody>()->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
			}
			int geoms_count = serializer.read<int>();
			for (int i = 0; i < geoms_count; ++i)
			{
				int type = serializer.read<int>();
				int index = serializer.read<int>();
				PxTransform tr = toPhysx(serializer.read<RigidTransform>());
				PxShape* shape = nullptr;
				switch (type)
				{
					case PxGeometryType::eBOX: {
						PxBoxGeometry box_geom;
						serializer.read(box_geom.halfExtents.x);
						serializer.read(box_geom.halfExtents.y);
						serializer.read(box_geom.halfExtents.z);

						shape = PxRigidActorExt::createExclusiveShape(*physx_actor, box_geom, *m_default_material);
						shape->setLocalPose(tr);
						break;
					}
					case PxGeometryType::eSPHERE: {
						PxSphereGeometry geom;
						serializer.read(geom.radius);
						shape = PxRigidActorExt::createExclusiveShape(*physx_actor, geom, *m_default_material);
						shape->setLocalPose(tr);
						break;
					}
					case PxGeometryType::eCONVEXMESH:
					case PxGeometryType::eTRIANGLEMESH:
						break;
					default: ASSERT(false); break;
				}
				if (shape) {
					shape->userData = (void*)(intptr_t)index;
					if (actor->is_trigger) {
						shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false); // must set false first
						shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
					}
				}
			}
			actor->setPhysxActor(physx_actor);
			if (path[0]) {
				ResourceManagerHub& manager = m_engine.getResourceManager();
				PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(Path(path));
				actor->setResource(geom_res);
			}
			m_universe.onComponentCreated(actor->entity, RIGID_ACTOR_TYPE, this);
		}
	}


	void deserializeControllers(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		for (u32 i = 0; i < count; ++i) {
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
			DVec3 position = m_universe.getPosition(entity);
			cDesc.position.set(position.x, position.y - cDesc.height * 0.5f, position.z);
			c.controller = m_controller_manager->createController(cDesc);
			c.controller->getActor()->userData = (void*)(intptr_t)entity.index;
			c.entity = entity;
			c.controller->setFootPosition({position.x, position.y, position.z});
			m_universe.onComponentCreated(entity, CONTROLLER_TYPE, this);
		}
	}


	void destroySkeleton(RagdollBone* bone)
	{
		if (!bone) return;
		destroySkeleton(bone->next);
		destroySkeleton(bone->child);
		if (bone->parent_joint) bone->parent_joint->release();
		if (bone->actor) bone->actor->release();
		LUMIX_DELETE(m_allocator, bone);
	}


	void deserializeVehicles(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		const u32 vehicles_count = serializer.read<u32>();
		m_vehicles.reserve(vehicles_count + m_vehicles.size());
		for (u32 i = 0; i < vehicles_count; ++i) {
			EntityRef e = serializer.read<EntityRef>();
			e = entity_map.get(e);
			auto iter = m_vehicles.insert(e, UniquePtr<Vehicle>::create(m_allocator));
			serializer.read(iter.value()->mass);
			serializer.read(iter.value()->center_of_mass);
			serializer.read(iter.value()->moi_multiplier);
			serializer.read(iter.value()->chassis_layer);
			serializer.read(iter.value()->wheels_layer);
			const char* path = serializer.readString();
			if (path[0]) {
				ResourceManagerHub& manager = m_engine.getResourceManager();
				PhysicsGeometry* geom_res = manager.load<PhysicsGeometry>(Path(path));
				iter.value()->geom = geom_res;
			}
			m_universe.onComponentCreated(e, VEHICLE_TYPE, this);
		}

		const u32 wheels_count = serializer.read<u32>();
		m_wheels.reserve(wheels_count);
		for (u32 i = 0; i < wheels_count; ++i) {
			EntityRef e = serializer.read<EntityRef>();
			e = entity_map.get(e);
			Wheel& w = m_wheels.insert(e);
			serializer.read(w);
			m_universe.onComponentCreated(e, WHEEL_TYPE, this);
		}
	}


	void deserializeRagdolls(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_ragdolls.reserve(count + m_ragdolls.size());
		for (u32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			Ragdoll& ragdoll = m_ragdolls.insert(entity);
			ragdoll.layer = 0;
			ragdoll.root_transform.pos = DVec3(0, 0, 0);
			ragdoll.root_transform.rot.set(0, 0, 0, 1);

			serializer.read(ragdoll.layer);
			ragdoll.entity = entity;
			setRagdollRoot(ragdoll, deserializeRagdollBone(ragdoll, nullptr, serializer));
			m_universe.onComponentCreated(ragdoll.entity, RAGDOLL_TYPE, this);
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
			switch (PxJointConcreteType::Enum(type))
			{
				case PxJointConcreteType::eSPHERICAL:
				{
					cmp_type = SPHERICAL_JOINT_TYPE;
					auto* px_joint = PxSphericalJointCreate(
						m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
					joint.physx = px_joint;
					u32 flags;
					serializer.read(flags);
					px_joint->setSphericalJointFlags(PxSphericalJointFlags(flags));
					PxJointLimitCone limit(0, 0);
					serializer.read(limit);
					px_joint->setLimitCone(limit);
					break;
				}
				case PxJointConcreteType::eREVOLUTE:
				{
					cmp_type = HINGE_JOINT_TYPE;
					auto* px_joint = PxRevoluteJointCreate(
						m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
					joint.physx = px_joint;
					u32 flags;
					serializer.read(flags);
					px_joint->setRevoluteJointFlags(PxRevoluteJointFlags(flags));
					PxJointAngularLimitPair limit(0, 0);
					serializer.read(limit);
					px_joint->setLimit(limit);
					break;
				}
				case PxJointConcreteType::eDISTANCE:
				{
					cmp_type = DISTANCE_JOINT_TYPE;
					auto* px_joint = PxDistanceJointCreate(
						m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
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
				case PxJointConcreteType::eD6:
				{
					cmp_type = D6_JOINT_TYPE;
					auto* px_joint = PxD6JointCreate(
						m_scene->getPhysics(), m_dummy_actor, joint.local_frame0, nullptr, PxTransform(PxIdentity));
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

			m_universe.onComponentCreated(entity, cmp_type, this);
		}
	}


	void deserializeTerrains(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		for (u32 i = 0; i < count; ++i) {
			Heightfield terrain;
			terrain.m_scene = this;
			serializer.read(terrain.m_entity);
			terrain.m_entity = entity_map.get(terrain.m_entity);
			const char* tmp = serializer.readString();
			serializer.read(terrain.m_xz_scale);
			serializer.read(terrain.m_y_scale);
			serializer.read(terrain.m_layer);

			m_terrains.insert(terrain.m_entity, terrain);
			EntityRef entity = terrain.m_entity;
			if (terrain.m_heightmap == nullptr || !equalStrings(tmp, terrain.m_heightmap->getPath().c_str()))
			{
				setHeightmapSource(entity, Path(tmp));
			}
			m_universe.onComponentCreated(terrain.m_entity, HEIGHTFIELD_TYPE, this);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		deserializeActors(serializer, entity_map);
		deserializeControllers(serializer, entity_map);
		deserializeTerrains(serializer, entity_map);
		deserializeRagdolls(serializer, entity_map);
		deserializeJoints(serializer, entity_map);
		deserializeVehicles(serializer, entity_map);
	}


	PhysicsSystem& getSystem() const override { return *m_system; }


	Vec3 getActorVelocity(EntityRef entity) override
	{
		auto* actor = m_actors[entity];
		if (actor->dynamic_type != DynamicType::DYNAMIC)
		{
			logWarning("Trying to get speed of static object");
			return Vec3::ZERO;
		}

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor->physx_actor);
		if (!physx_actor) return Vec3::ZERO;
		return fromPhysx(physx_actor->getLinearVelocity());
	}


	float getActorSpeed(EntityRef entity) override
	{
		auto* actor = m_actors[entity];
		if (actor->dynamic_type != DynamicType::DYNAMIC)
		{
			logWarning("Trying to get speed of static object");
			return 0;
		}

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor->physx_actor);
		if (!physx_actor) return 0;
		return physx_actor->getLinearVelocity().magnitude();
	}


	void putToSleep(EntityRef entity) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;
		RigidActor* actor = iter.value();

		if (actor->dynamic_type != DynamicType::DYNAMIC)
		{
			logWarning("Trying to put static object to sleep");
			return;
		}

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor->physx_actor);
		if (!physx_actor) return;
		physx_actor->putToSleep();
	}


	void applyForceToActor(EntityRef entity, const Vec3& force) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;
		RigidActor* actor = iter.value();

		if (actor->dynamic_type != DynamicType::DYNAMIC) return;

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor->physx_actor);
		if (!physx_actor) return;
		physx_actor->addForce(toPhysx(force));
	}


	void applyImpulseToActor(EntityRef entity, const Vec3& impulse) override
	{
		auto iter = m_actors.find(entity);
		if (!iter.isValid()) return;
		RigidActor* actor = iter.value();

		if (actor->dynamic_type != DynamicType::DYNAMIC) return;

		auto* physx_actor = static_cast<PxRigidDynamic*>(actor->physx_actor);
		if (!physx_actor) return;
		physx_actor->addForce(toPhysx(impulse), PxForceMode::eIMPULSE);
	}
	
	void onActorResourceStateChanged(Resource::State prev_state, Resource::State new_state, Resource& res) {
		auto iter = m_resource_actor_map.find((PhysicsGeometry*)&res);
		ASSERT(iter.isValid());
		const EntityRef e = iter.value();
		RigidActor* actor = m_actors[e];
		while (actor) {
			actor->onStateChanged(prev_state, new_state, res);
			actor = actor->next_with_resource;
		}
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
		HitReport(PhysicsSceneImpl& scene) : scene(scene) {}
		void onShapeHit(const PxControllerShapeHit& hit) override {
			void* user_data = hit.controller->getActor()->userData;
			const EntityRef e1 {(i32)(uintptr)user_data};
			const EntityRef e2 {(i32)(uintptr)hit.actor->userData};

			scene.onControllerHit(e1, e2);
		}
		void onControllerHit(const PxControllersHit& hit) override {}
		void onObstacleHit(const PxControllerObstacleHit& hit) override {}

		PhysicsSceneImpl& scene;
	} ;

	IAllocator& m_allocator;
	Engine& m_engine;
	Universe& m_universe;
	HitReport m_hit_report;
	PhysxContactCallback m_contact_callback;
	BoneOrientation m_new_bone_orientation = BoneOrientation::X;
	PxScene* m_scene;
	LuaScriptScene* m_script_scene;
	PhysicsSystem* m_system;
	PxRigidDynamic* m_dummy_actor;
	PxControllerManager* m_controller_manager;
	PxMaterial* m_default_material;
	FilterCallback m_filter_callback;

	HashMap<EntityRef, RigidActor*> m_actors;
	HashMap<PhysicsGeometry*, EntityRef> m_resource_actor_map;
	HashMap<EntityRef, Ragdoll> m_ragdolls;
	AssociativeArray<EntityRef, Joint> m_joints;
	HashMap<EntityRef, Controller> m_controllers;
	HashMap<EntityRef, Heightfield> m_terrains;
	HashMap<EntityRef, UniquePtr<Vehicle>> m_vehicles;
	HashMap<EntityRef, Wheel> m_wheels;
	PxVehicleDrivableSurfaceToTireFrictionPairs* m_vehicle_frictions;
	PxBatchQuery* m_vehicle_batch_query;
	u8 m_vehicle_query_mem[sizeof(PxRaycastQueryResult) * 64 + sizeof(PxRaycastHit) * 64];
	PxRaycastQueryResult* m_vehicle_results;
	u64 m_physics_cmps_mask;

	Array<RigidActor*> m_dynamic_actors;
	RigidActor* m_update_in_progress;
	DelegateList<void(const ContactData&)> m_contact_callbacks;
	bool m_is_game_running;
	bool m_is_updating_ragdoll;
	u32 m_debug_visualization_flags;
	CPUDispatcher m_cpu_dispatcher;
	CollisionLayers& m_layers;
};

PhysicsSceneImpl::PhysicsSceneImpl(Engine& engine, Universe& context, PhysicsSystem& system, IAllocator& allocator)
	: m_allocator(allocator)
	, m_engine(engine)
	, m_controllers(m_allocator)
	, m_actors(m_allocator)
	, m_ragdolls(m_allocator)
	, m_vehicles(m_allocator)
	, m_wheels(m_allocator)
	, m_terrains(m_allocator)
	, m_dynamic_actors(m_allocator)
	, m_universe(context)
	, m_is_game_running(false)
	, m_contact_callback(*this)
	, m_contact_callbacks(m_allocator)
	, m_joints(m_allocator)
	, m_script_scene(nullptr)
	, m_debug_visualization_flags(0)
	, m_is_updating_ragdoll(false)
	, m_update_in_progress(nullptr)
	, m_vehicle_batch_query(nullptr)
	, m_system(&system)
	, m_hit_report(*this)
	, m_layers(m_system->getCollisionLayers())
	, m_resource_actor_map(m_allocator)
{
	m_physics_cmps_mask = 0;

	const u32 hash = crc32("physics");
	for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
		if (cmp.scene == hash) {
			m_physics_cmps_mask |= (u64)1 << cmp.cmp->component_type.index;
		}
	}

	m_vehicle_frictions = createFrictionPairs();
}


UniquePtr<PhysicsScene> PhysicsScene::create(PhysicsSystem& system, Universe& context, Engine& engine, IAllocator& allocator)
{
	PhysicsSceneImpl* impl = LUMIX_NEW(allocator, PhysicsSceneImpl)(engine, context, system, allocator);
	impl->m_universe.entityTransformed().bind<&PhysicsSceneImpl::onEntityMoved>(impl);
	impl->m_universe.entityDestroyed().bind<&PhysicsSceneImpl::onEntityDestroyed>(impl);
	PxSceneDesc sceneDesc(system.getPhysics()->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.8f, 0.0f);
	sceneDesc.cpuDispatcher = &impl->m_cpu_dispatcher;

	sceneDesc.filterShader = impl->filterShader;
	sceneDesc.simulationEventCallback = &impl->m_contact_callback;

	impl->m_scene = system.getPhysics()->createScene(sceneDesc);
	if (!impl->m_scene)
	{
		LUMIX_DELETE(allocator, impl);
		return UniquePtr<PhysicsScene>(nullptr, nullptr);
	}

	impl->m_controller_manager = PxCreateControllerManager(*impl->m_scene);

	impl->m_default_material = impl->m_system->getPhysics()->createMaterial(0.5f, 0.5f, 0.1f);
	PxSphereGeometry geom(1);
	impl->m_dummy_actor =
		PxCreateDynamic(impl->m_scene->getPhysics(), PxTransform(PxIdentity), geom, *impl->m_default_material, 1);
	impl->m_vehicle_batch_query = impl->createVehicleBatchQuery(impl->m_vehicle_query_mem);
	return UniquePtr<PhysicsSceneImpl>(impl, &allocator);
}

void PhysicsScene::reflect() {
	struct LayerEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { 
			return ((PhysicsScene*)cmp.scene)->getSystem().getCollisionsLayersCount();
		}
		const char* name(ComponentUID cmp, u32 idx) const override { 
			PhysicsSystem& system = ((PhysicsScene*)cmp.scene)->getSystem();
			return system.getCollisionLayerName(idx);
		}
	};

	struct DynamicTypeEnum : reflection::EnumAttribute {
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

	struct D6MotionEnum : reflection::EnumAttribute {
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

	struct WheelSlotEnum : reflection::EnumAttribute {
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

// "temporary" hack to fix crash at startup on linux
// last good commmit around eb0e5c9b6fc7ce3de197c5c0b2b30e9f8c9653eb
#ifdef _WIN32
	#define VEHICLE_CMP \
		, \
		LUMIX_FUNC_EX(PhysicsScene::setVehicleAccel, "setAccel"), \
		LUMIX_FUNC_EX(PhysicsScene::setVehicleSteer, "setSteer"), \
		LUMIX_PROP(VehicleMass, "Mass", MinAttribute(0)), \
		LUMIX_PROP(VehicleCenterOfMass, "Center of mass"), \
		LUMIX_PROP(VehicleMOIMultiplier, "MOI multiplier"), \
		LUMIX_PROP(VehicleChassis, "Chassis", ResourceAttribute(PhysicsGeometry::TYPE)), \
		LUMIX_PROP(VehicleChassisLayer, "Chassis layer", LayerEnum()), \
		LUMIX_PROP(VehicleWheelsLayer, "Wheels layer", LayerEnum())
#else 
	#define VEHICLE_CMP
#endif

	LUMIX_SCENE(PhysicsSceneImpl, "physics",
		LUMIX_FUNC(PhysicsSceneImpl::raycast),
		//function(LUMIX_FUNC(PhysicsScene::raycastEx))
		LUMIX_CMP(Ragdoll, "ragdoll", "Physics / Ragdoll",
			icon(ICON_FA_MALE),
			LUMIX_FUNC(PhysicsScene::setRagdollKinematic),
			blob_property("data", &PhysicsScene::getRagdollData, &PhysicsScene::setRagdollData),
			LUMIX_PROP(RagdollLayer, "Layer", LayerEnum())
		),
		LUMIX_CMP(D6Joint, "d6_joint", "Physics / Joint / D6",
			LUMIX_PROP(JointConnectedBody, "Connected body"),
			LUMIX_PROP(JointAxisPosition, "Axis position"),
			LUMIX_PROP(JointAxisDirection, "Axis direction"),
			enum_property("X motion", &PhysicsScene::getD6JointXMotion, &PhysicsScene::setD6JointXMotion, D6MotionEnum()),
			enum_property("Y motion", &PhysicsScene::getD6JointYMotion, &PhysicsScene::setD6JointYMotion, D6MotionEnum()),
			enum_property("Z motion", &PhysicsScene::getD6JointZMotion, &PhysicsScene::setD6JointZMotion, D6MotionEnum()),
			enum_property("Swing 1", &PhysicsScene::getD6JointSwing1Motion, &PhysicsScene::setD6JointSwing1Motion, D6MotionEnum()),
			enum_property("Swing 2", &PhysicsScene::getD6JointSwing2Motion, &PhysicsScene::setD6JointSwing2Motion, D6MotionEnum()),
			enum_property("Twist", &PhysicsScene::getD6JointTwistMotion, &PhysicsScene::setD6JointTwistMotion, D6MotionEnum()),
			LUMIX_PROP(D6JointLinearLimit, "Linear limit", MinAttribute(0)),
			LUMIX_PROP(D6JointSwingLimit, "Swing limit", RadiansAttribute()),
			LUMIX_PROP(D6JointTwistLimit, "Twist limit", RadiansAttribute()),
			LUMIX_PROP(D6JointDamping, "Damping"),
			LUMIX_PROP(D6JointStiffness, "Stiffness"),
			LUMIX_PROP(D6JointRestitution, "Restitution")
		),
		LUMIX_CMP(SphericalJoint, "spherical_joint", "Physics / Joint / Spherical",
			LUMIX_PROP(JointConnectedBody, "Connected body"),
			LUMIX_PROP(JointAxisPosition, "Axis position"),
			LUMIX_PROP(JointAxisDirection, "Axis direction"),
			LUMIX_PROP(SphericalJointUseLimit, "Use limit"),
			LUMIX_PROP(SphericalJointLimit, "Limit", RadiansAttribute())
		),
		LUMIX_CMP(DistanceJoint, "distance_joint", "Physics / Joint / Distance",
			LUMIX_PROP(JointConnectedBody, "Connected body"),
			LUMIX_PROP(JointAxisPosition, "Axis position"),
			LUMIX_PROP(DistanceJointDamping, "Damping",	MinAttribute(0)),
			LUMIX_PROP(DistanceJointStiffness, "Stiffness", MinAttribute(0)),
			LUMIX_PROP(DistanceJointTolerance, "Tolerance", MinAttribute(0)),
			LUMIX_PROP(DistanceJointLimits, "Limits")
		),
		LUMIX_CMP(HingeJoint, "hinge_joint", "Physics / Joint / Hinge",
			LUMIX_PROP(JointConnectedBody, "Connected body"),
			LUMIX_PROP(JointAxisPosition, "Axis position"),
			LUMIX_PROP(JointAxisDirection, "Axis direction"),
			LUMIX_PROP(HingeJointDamping, "Damping", MinAttribute(0)),
			LUMIX_PROP(HingeJointStiffness, "Stiffness", MinAttribute(0)),
			LUMIX_PROP(HingeJointUseLimit, "Use limit"),
			LUMIX_PROP(HingeJointLimit, "Limit", RadiansAttribute())
		),
		LUMIX_CMP(Controller, "physical_controller", "Physics / Controller",
			LUMIX_FUNC_EX(PhysicsScene::moveController, "move"),
			LUMIX_FUNC_EX(PhysicsScene::isControllerCollisionDown, "isCollisionDown"),
			LUMIX_PROP(ControllerRadius, "Radius"),
			LUMIX_PROP(ControllerHeight, "Height"),
			LUMIX_PROP(ControllerLayer, "Layer", LayerEnum()),
			LUMIX_PROP(ControllerUseRootMotion, "Use root motion"),
			LUMIX_PROP(ControllerCustomGravity, "Use custom gravity"),
			LUMIX_PROP(ControllerCustomGravityAcceleration, "Custom gravity acceleration")
		),
		LUMIX_CMP(RigidActor, "rigid_actor", "Physics / Rigid actor",
			icon(ICON_FA_VOLLEYBALL_BALL),
			LUMIX_FUNC_EX(PhysicsScene::putToSleep, "putToSleep"),
			LUMIX_FUNC_EX(PhysicsScene::getActorSpeed, "getSpeed"),
			LUMIX_FUNC_EX(PhysicsScene::getActorVelocity, "getVelocity"),
			LUMIX_FUNC_EX(PhysicsScene::applyForceToActor, "applyForce"),
			LUMIX_FUNC_EX(PhysicsScene::applyForceToActor, "applyImpulse"),
			LUMIX_FUNC_EX(PhysicsScene::addForceAtPos, "addForceAtPos"),
			LUMIX_PROP(ActorLayer, "Layer", LayerEnum()),
			enum_property("Dynamic", &PhysicsSceneImpl::getDynamicType, &PhysicsSceneImpl::setDynamicType, DynamicTypeEnum()),
			LUMIX_PROP(IsTrigger, "Trigger"),
			array("Box geometry", &PhysicsScene::getBoxGeometryCount, &PhysicsScene::addBoxGeometry, &PhysicsScene::removeBoxGeometry,
				LUMIX_PROP(BoxGeomHalfExtents, "Size"),
				LUMIX_PROP(BoxGeomOffsetPosition, "Position offset"),
				LUMIX_PROP(BoxGeomOffsetRotation, "Rotation offset", RadiansAttribute())),
			array("Sphere geometry", &PhysicsScene::getSphereGeometryCount, &PhysicsScene::addSphereGeometry, &PhysicsScene::removeSphereGeometry,
				LUMIX_PROP(SphereGeomRadius, "Radius", MinAttribute(0)),
				LUMIX_PROP(SphereGeomOffsetPosition, "Position offset")),
			LUMIX_PROP(MeshGeomPath, "Mesh", ResourceAttribute(PhysicsGeometry::TYPE))
		),
		LUMIX_CMP(Vehicle, "vehicle", "Physics / Vehicle", 
			icon(ICON_FA_CAR_ALT)
			VEHICLE_CMP
		),
		LUMIX_CMP(Wheel, "wheel", "Physics / Wheel",
			LUMIX_PROP(WheelRadius, "Radius", MinAttribute(0)),
			LUMIX_PROP(WheelWidth, "Width", MinAttribute(0)),
			LUMIX_PROP(WheelMass, "Mass", MinAttribute(0)),
			LUMIX_PROP(WheelMOI, "MOI", MinAttribute(0)),
			LUMIX_PROP(WheelSpringMaxCompression, "Max compression", MinAttribute(0)),
			LUMIX_PROP(WheelSpringMaxDroop, "Max droop", MinAttribute(0)),
			LUMIX_PROP(WheelSpringStrength, "Spring strength", MinAttribute(0)),
			LUMIX_PROP(WheelSpringDamperRate, "Spring damper rate", MinAttribute(0)),
			enum_property("Slot", &PhysicsSceneImpl::getWheelSlot, &PhysicsSceneImpl::setWheelSlot, WheelSlotEnum())
		),
		LUMIX_CMP(Heightfield, "physical_heightfield", "Physics / Heightfield",
			LUMIX_PROP(HeightfieldLayer, "Layer", LayerEnum()),
			LUMIX_PROP(HeightmapSource, "Heightmap", ResourceAttribute(Texture::TYPE)),
			LUMIX_PROP(HeightmapYScale, "Y scale", MinAttribute(0)),
			LUMIX_PROP(HeightmapXZScale, "XZ scale", MinAttribute(0))
		)
	);
}

void PhysicsSceneImpl::RigidActor::onStateChanged(Resource::State, Resource::State new_state, Resource&)
{
	if (new_state == Resource::State::READY)
	{
#if 0
		moveShapeIndices(entity, index, PxGeometryType::eBOX);
		PxRigidActor* actor = m_actors[entity]->physx_actor;
		PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*physx_actor, geom, *m_default_material);
		shape->userData = (void*)(intptr_t)index;
#endif
		scale = scene.getUniverse().getScale(entity);
		PxMeshScale pxscale(scale);
		PxConvexMeshGeometry convex_geom(resource->convex_mesh, pxscale);
		PxTriangleMeshGeometry tri_geom(resource->tri_mesh, pxscale);
		const PxGeometry* geom = resource->convex_mesh ? static_cast<PxGeometry*>(&convex_geom) : static_cast<PxGeometry*>(&tri_geom);
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*physx_actor, *geom, *scene.m_default_material);
		(void)shape;
	}
}


void PhysicsSceneImpl::RigidActor::rescale()
{
	if (!resource || !resource->isReady()) return;

	onStateChanged(resource->getState(), resource->getState(), *resource);
}


void PhysicsSceneImpl::RigidActor::setPhysxActor(PxRigidActor* actor)
{
	if (physx_actor)
	{
		scene.m_scene->removeActor(*physx_actor);
		physx_actor->release();
	}
	physx_actor = actor;
	if (actor)
	{
		scene.m_scene->addActor(*actor);
		actor->userData = (void*)(intptr_t)entity.index;
		scene.updateFilterData(actor, layer);
		scene.setIsTrigger({entity.index}, is_trigger);
	}
}


void PhysicsSceneImpl::RigidActor::setResource(PhysicsGeometry* new_value)
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

	if (resource) 
	{
		if (!next_with_resource && !prev_with_resource) {
			resource->getObserverCb().unbind<&PhysicsSceneImpl::onActorResourceStateChanged>(&scene);
			scene.m_resource_actor_map.erase(resource);
			resource->decRefCount();
		}
		else {
			auto iter = scene.m_resource_actor_map.find(resource);
			if (iter.value() == entity) {
				scene.m_resource_actor_map[resource] = next_with_resource->entity;
			}

			if (next_with_resource) next_with_resource->prev_with_resource = prev_with_resource;
			if (prev_with_resource) prev_with_resource->next_with_resource = next_with_resource;
		}
	}
	resource = new_value;
	if (resource)
	{
		auto iter = scene.m_resource_actor_map.find(resource);
		if (iter.isValid()) {
			EntityRef e = iter.value();
			next_with_resource = scene.m_actors[e];
			next_with_resource->prev_with_resource = this;
			prev_with_resource = nullptr;
			scene.m_resource_actor_map[resource] = entity;
			if (resource->isReady()) {
				onStateChanged(Resource::State::READY, Resource::State::READY, *new_value);
			}
			resource->decRefCount();
		}
		else {
			scene.m_resource_actor_map.insert(resource, entity);
			resource->onLoaded<&PhysicsSceneImpl::onActorResourceStateChanged>(&scene);
		}
	}
}


Heightfield::Heightfield()
{
	m_heightmap = nullptr;
	m_xz_scale = 1.0f;
	m_y_scale = 1.0f;
	m_actor = nullptr;
	m_layer = 0;
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
		m_scene->heightmapLoaded(*this);
	}
}


} // namespace Lumix
