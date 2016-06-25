#include "physics/physics_scene.h"
#include "cooking/PxCooking.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/engine.h"
#include "lua_script/lua_script_system.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "physics/physics_system.h"
#include "physics/physics_geometry_manager.h"
#include "engine/universe/universe.h"
#include <PxPhysicsAPI.h>


namespace Lumix
{


static const ComponentType BOX_ACTOR_TYPE = PropertyRegister::getComponentType("box_rigid_actor");
static const ComponentType MESH_ACTOR_TYPE = PropertyRegister::getComponentType("mesh_rigid_actor");
static const ComponentType CONTROLLER_TYPE = PropertyRegister::getComponentType("physical_controller");
static const ComponentType HEIGHTFIELD_TYPE = PropertyRegister::getComponentType("physical_heightfield");
static const ComponentType DISTANCE_JOINT_TYPE = PropertyRegister::getComponentType("distance_joint");
static const ComponentType HINGE_JOINT_TYPE = PropertyRegister::getComponentType("hinge_joint");
static const uint32 TEXTURE_HASH = crc32("TEXTURE");
static const uint32 PHYSICS_HASH = crc32("PHYSICS");


enum class PhysicsSceneVersion : int
{
	LAYERS,
	JOINTS,
	HINGE_JOINT,

	LATEST
};


struct OutputStream : public physx::PxOutputStream
{
	explicit OutputStream(IAllocator& allocator)
		: allocator(allocator)
	{
		data = (uint8*)allocator.allocate(sizeof(uint8) * 4096);
		capacity = 4096;
		size = 0;
	}

	~OutputStream() { allocator.deallocate(data); }


	virtual physx::PxU32 write(const void* src, physx::PxU32 count)
	{
		if (size + (int)count > capacity)
		{
			int new_capacity =
				Math::maximum(size + (int)count, capacity + 4096);
			uint8* new_data =
				(uint8*)allocator.allocate(sizeof(uint8) * new_capacity);
			copyMemory(new_data, data, size);
			allocator.deallocate(data);
			data = new_data;
			capacity = new_capacity;
		}
		copyMemory(data + size, src, count);
		size += count;
		return count;
	}

	uint8* data;
	IAllocator& allocator;
	int capacity;
	int size;
};


struct InputStream : public physx::PxInputStream
{
	InputStream(unsigned char* data, int size)
	{
		this->data = data;
		this->size = size;
		pos = 0;
	}

	virtual physx::PxU32 read(void* dest, physx::PxU32 count)
	{
		if (pos + (int)count <= size)
		{
			copyMemory(dest, data + pos, count);
			pos += count;
			return count;
		}
		else
		{
			copyMemory(dest, data + pos, size - pos);
			int real_count = size - pos;
			pos = size;
			return real_count;
		}
	}


	int pos;
	int size;
	unsigned char* data;
};


static void matrix2Transform(const Matrix& mtx, physx::PxTransform& transform)
{
	transform.p.x = mtx.m41;
	transform.p.y = mtx.m42;
	transform.p.z = mtx.m43;
	Quat q;
	mtx.getRotation(q);
	transform.q.x = q.x;
	transform.q.y = q.y;
	transform.q.z = q.z;
	transform.q.w = q.w;
}


struct DistanceJoint
{
	Entity connected_body;
	physx::PxDistanceJoint* physx;
	float damping;
	float stiffness;
	float tolerance;
	Vec2 distance_limit;
};


struct HingeJoint
{
	Entity connected_body;
	physx::PxRevoluteJoint* physx;
	float damping;
	float stiffness;
	float tolerance;
	Vec3 axis_position;
	Vec3 axis_direction;
	bool use_limit;
	Vec2 limit;
	Vec3 initial_pos;
};


struct Heightfield
{
	Heightfield();
	~Heightfield();
	void heightmapLoaded(Resource::State, Resource::State new_state);

	struct PhysicsSceneImpl* m_scene;
	Entity m_entity;
	physx::PxRigidActor* m_actor;
	Texture* m_heightmap;
	float m_xz_scale;
	float m_y_scale;
	int m_layer;
};


struct PhysicsSceneImpl : public PhysicsScene
{
	struct ContactCallback : public physx::PxSimulationEventCallback
	{
		explicit ContactCallback(PhysicsSceneImpl& scene)
			: m_scene(scene)
		{
		}


		void onContact(const physx::PxContactPairHeader& pairHeader,
			const physx::PxContactPair* pairs,
			physx::PxU32 nbPairs) override
		{
			for (physx::PxU32 i = 0; i < nbPairs; i++)
			{
				const auto& cp = pairs[i];

				if (!(cp.events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)) continue;

				physx::PxContactPairPoint contact;
				auto contact_count = cp.extractContacts(&contact, 1);

				auto pos = toVec3(contact.position);
				Entity e1 = { (int)(intptr_t)(pairHeader.actors[0]->userData) };
				Entity e2 = { (int)(intptr_t)(pairHeader.actors[1]->userData) };
				m_scene.onContact(e1, e2, pos);
			}
		}


		void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override {}
		void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
		void onWake(physx::PxActor**, physx::PxU32) override {}
		void onSleep(physx::PxActor**, physx::PxU32) override {}


		PhysicsSceneImpl& m_scene;
	};


	enum ActorType
	{
		BOX,
		TRIMESH,
		CONVEX
	};


	class RigidActor
	{
	public:
		explicit RigidActor(PhysicsSceneImpl& scene)
			: m_resource(nullptr)
			, m_physx_actor(nullptr)
			, m_scene(scene)
			, m_is_dynamic(false)
			, m_layer(0)
		{
		}

		void setResource(PhysicsGeometry* resource);
		void setEntity(Entity entity) { m_entity = entity; }
		Entity getEntity() { return m_entity; }
		void setPhysxActor(physx::PxRigidActor* actor);
		physx::PxRigidActor* getPhysxActor() const { return m_physx_actor; }
		PhysicsGeometry* getResource() const { return m_resource; }
		bool isDynamic() const { return m_is_dynamic; }
		void setDynamic(bool dynamic) { m_is_dynamic = dynamic; }
		int getLayer() const { return m_layer; }
		void setLayer(int layer) { m_layer = layer; }

	private:
		void onStateChanged(Resource::State old_state, Resource::State new_state);

	private:
		physx::PxRigidActor* m_physx_actor;
		PhysicsGeometry* m_resource;
		Entity m_entity;
		int m_layer;
		PhysicsSceneImpl& m_scene;
		bool m_is_dynamic;
	};


	PhysicsSceneImpl(Universe& context, IAllocator& allocator)
		: m_allocator(allocator)
		, m_controllers(m_allocator)
		, m_actors(m_allocator)
		, m_terrains(m_allocator)
		, m_dynamic_actors(m_allocator)
		, m_universe(context)
		, m_is_game_running(false)
		, m_contact_callback(*this)
		, m_queued_forces(m_allocator)
		, m_layers_count(2)
		, m_distance_joints(m_allocator)
		, m_hinge_joints(m_allocator)
		, m_script_scene(nullptr)
	{
		setMemory(m_layers_names, 0, sizeof(m_layers_names));
		for (int i = 0; i < lengthOf(m_layers_names); ++i)
		{
			copyString(m_layers_names[i], "Layer");
			char tmp[3];
			toCString(i, tmp, lengthOf(tmp));
			catString(m_layers_names[i], tmp);
			m_collision_filter[i] = 0xffffFFFF;
		}

		m_queued_forces.reserve(64);
	}


	~PhysicsSceneImpl()
	{
		for (int i = 0; i < m_actors.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_actors[i]);
		}
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_terrains[i]);
		}
	}


	void onContact(Entity e1, Entity e2, const Vec3& position)
	{
		if (!m_script_scene) return;

		auto send = [this](Entity e1, Entity e2, const Vec3& position)
		{
			auto cmp = m_script_scene->getComponent(e1);
			if (cmp == INVALID_COMPONENT) return;

			for (int i = 0, c = m_script_scene->getScriptCount(cmp); i < c; ++i)
			{
				auto* call = m_script_scene->beginFunctionCall(cmp, i, "onContact");
				if (!call) continue;

				call->add(e2.index);
				call->add(position.x);
				call->add(position.y);
				call->add(position.z);
				m_script_scene->endFunctionCall(*call);
			}
		};

		send(e1, e2, position);
		send(e2, e1, position);
	}


	void enableVisualization()
	{
		m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
		m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.0);
		m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eACTOR_AXES, 1.0f);
		m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
		m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eWORLD_AXES, 1.0f);
		m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCONTACT_POINT, 1.0f);
	}


	Universe& getUniverse() override { return m_universe; }


	bool ownComponentType(ComponentType type) const override
	{
		return type == BOX_ACTOR_TYPE || type == MESH_ACTOR_TYPE || type == HEIGHTFIELD_TYPE ||
			   type == CONTROLLER_TYPE || type == DISTANCE_JOINT_TYPE || type == HINGE_JOINT_TYPE;
	}


	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		ASSERT(ownComponentType(type));
		if (type == BOX_ACTOR_TYPE || type == MESH_ACTOR_TYPE)
		{
			for (int i = 0; i < m_actors.size(); ++i)
			{
				if (m_actors[i] && m_actors[i]->getEntity() == entity) return {i};
			}
			return INVALID_COMPONENT;
		}
		if (type == CONTROLLER_TYPE)
		{
			for (int i = 0; i < m_controllers.size(); ++i)
			{
				if (!m_controllers[i].m_is_free && m_controllers[i].m_entity == entity) return {i};
			}
			return INVALID_COMPONENT;
		}
		if (type == HEIGHTFIELD_TYPE)
		{
			for (int i = 0; i < m_terrains.size(); ++i)
			{
				if (m_terrains[i] && m_terrains[i]->m_entity == entity) return {i};
			}
			return INVALID_COMPONENT;
		}
		if (type == DISTANCE_JOINT_TYPE)
		{
			int index = m_distance_joints.find(entity);
			if(index < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		if (type == HINGE_JOINT_TYPE)
		{
			int index = m_hinge_joints.find(entity);
			if (index < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		return INVALID_COMPONENT;
	}


	IPlugin& getPlugin() const override { return *m_system; }


	int getControllerLayer(ComponentHandle cmp) override
	{
		return m_controllers[cmp.index].m_layer;
	}


	void setControllerLayer(ComponentHandle cmp, int layer) override
	{
		ASSERT(layer < lengthOf(m_layers_names));
		m_controllers[cmp.index].m_layer = layer;

		physx::PxFilterData data;
		data.word0 = 1 << layer;
		data.word1 = m_collision_filter[layer];
		physx::PxShape* shapes[8];
		int shapes_count = m_controllers[cmp.index].m_controller->getActor()->getShapes(shapes, lengthOf(shapes));
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}
	}


	void setActorLayer(ComponentHandle cmp, int layer) override
	{
		ASSERT(layer < lengthOf(m_layers_names));
		auto* actor = m_actors[cmp.index];
		actor->setLayer(layer);
		updateFilterData(actor->getPhysxActor(), actor->getLayer());
	}


	int getActorLayer(ComponentHandle cmp) override { return m_actors[cmp.index]->getLayer(); }
	int getHeightfieldLayer(ComponentHandle cmp) override { return m_terrains[cmp.index]->m_layer; }
	

	void setHeightfieldLayer(ComponentHandle cmp, int layer) override
	{
		ASSERT(layer < lengthOf(m_layers_names));
		m_terrains[cmp.index]->m_layer = layer;

		if (m_terrains[cmp.index]->m_actor)
		{
			physx::PxFilterData data;
			data.word0 = 1 << layer;
			data.word1 = m_collision_filter[layer];
			physx::PxShape* shapes[8];
			int shapes_count = m_terrains[cmp.index]->m_actor->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}
	}


	Vec3 getDistanceJointLinearForce(ComponentHandle cmp) override
	{
		if (!m_distance_joints[{cmp.index}].physx) return Vec3(0, 0, 0);
		physx::PxVec3 linear, angular;
		m_distance_joints[{cmp.index}].physx->getConstraint()->getForce(linear, angular);
		return Vec3(linear.x, linear.y, linear.z);
	}


	float getDistanceJointDamping(ComponentHandle cmp) override { return m_distance_joints[{cmp.index}].damping; }


	void setDistanceJointDamping(ComponentHandle cmp, float value) override
	{
		m_distance_joints[{cmp.index}].damping = value;
	}


	float getDistanceJointStiffness(ComponentHandle cmp) override { return m_distance_joints[{cmp.index}].stiffness; }


	void setDistanceJointStiffness(ComponentHandle cmp, float value) override
	{
		m_distance_joints[{cmp.index}].stiffness = value;
	}


	float getDistanceJointTolerance(ComponentHandle cmp) override { return m_distance_joints[{cmp.index}].tolerance; }


	void setDistanceJointTolerance(ComponentHandle cmp, float value) override
	{
		m_distance_joints[{cmp.index}].tolerance = value;
	}


	Entity getDistanceJointConnectedBody(ComponentHandle cmp) override
	{
		return m_distance_joints[{cmp.index}].connected_body;
	}


	void setDistanceJointConnectedBody(ComponentHandle cmp, Entity entity) override
	{
		ASSERT(entity.index != cmp.index);
		m_distance_joints[{cmp.index}].connected_body = entity;
	}


	Vec2 getDistanceJointLimits(ComponentHandle cmp) override
	{
		return m_distance_joints[{cmp.index}].distance_limit;
	}


	void setDistanceJointLimits(ComponentHandle cmp, const Vec2& value) override
	{
		m_distance_joints[{cmp.index}].distance_limit = value;
	}


	void setHingeJointAxisPosition(ComponentHandle cmp, const Vec3& value) override
	{
		m_hinge_joints[{cmp.index}].axis_position = value;
	}


	void setHingeJointAxisDirection(ComponentHandle cmp, const Vec3& value) override
	{
		m_hinge_joints[{cmp.index}].axis_direction = value;
	}


	Vec3 getHingeJointAxisPosition(ComponentHandle cmp) override
	{
		return m_hinge_joints[{cmp.index}].axis_position;
	}


	Vec3 getHingeJointAxisDirection(ComponentHandle cmp) override
	{
		return m_hinge_joints[{cmp.index}].axis_direction;
	}


	Entity getHingeJointConnectedBody(ComponentHandle cmp) override
	{
		return m_hinge_joints[{cmp.index}].connected_body;
	}


	bool getHingeJointUseLimit(ComponentHandle cmp) override
	{
		return m_hinge_joints[{cmp.index}].use_limit;
	}


	void setHingeJointUseLimit(ComponentHandle cmp, bool use_limit) override
	{
		m_hinge_joints[{cmp.index}].use_limit = use_limit;
	}


	Vec2 getHingeJointLimit(ComponentHandle cmp) override
	{
		return m_hinge_joints[{cmp.index}].limit;
	}


	Vec3 getHingeJointConnectedBodyInitialPosition(ComponentHandle cmp) override
	{
		auto& joint = m_hinge_joints[{cmp.index}];
		if (!m_is_game_running && isValid(joint.connected_body)) return m_universe.getPosition(joint.connected_body);
		return joint.initial_pos;
	}


	void setHingeJointLimit(ComponentHandle cmp, const Vec2& limit) override
	{
		m_hinge_joints[{cmp.index}].limit = limit;
	}


	void setHingeJointConnectedBody(ComponentHandle cmp, Entity entity) override
	{
		m_hinge_joints[{cmp.index}].connected_body = entity;
	}


	float getHingeJointDamping(ComponentHandle cmp) override { return m_hinge_joints[{cmp.index}].damping; }


	void setHingeJointDamping(ComponentHandle cmp, float value) override
	{
		m_hinge_joints[{cmp.index}].damping = value;
	}


	float getHingeJointStiffness(ComponentHandle cmp) override { return m_hinge_joints[{cmp.index}].stiffness; }


	void setHingeJointStiffness(ComponentHandle cmp, float value) override
	{
		m_hinge_joints[{cmp.index}].stiffness = value;
	}


	ComponentHandle createComponent(ComponentType component_type, Entity entity) override
	{
		if (component_type == DISTANCE_JOINT_TYPE)
		{
			return createDistanceJoint(entity);
		}
		else if (component_type == HINGE_JOINT_TYPE)
		{
			return createHingeJoint(entity);
		}
		else if (component_type == HEIGHTFIELD_TYPE)
		{
			return createHeightfield(entity);
		}
		else if (component_type == CONTROLLER_TYPE)
		{
			return createController(entity);
		}
		else if (component_type == BOX_ACTOR_TYPE)
		{
			return createBoxRigidActor(entity);
		}
		else if (component_type == MESH_ACTOR_TYPE)
		{
			return createMeshRigidActor(entity);
		}
		return INVALID_COMPONENT;
	}


	void destroyComponent(ComponentHandle cmp, ComponentType type) override
	{
		if (type == HEIGHTFIELD_TYPE)
		{
			Entity entity = m_terrains[cmp.index]->m_entity;
			LUMIX_DELETE(m_allocator, m_terrains[cmp.index]);
			m_terrains[cmp.index] = nullptr;
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else if (type == CONTROLLER_TYPE)
		{
			Entity entity = m_controllers[cmp.index].m_entity;
			m_controllers[cmp.index].m_is_free = true;
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else if (type == MESH_ACTOR_TYPE || type == BOX_ACTOR_TYPE)
		{
			Entity entity = m_actors[cmp.index]->getEntity();
			m_actors[cmp.index]->setEntity(INVALID_ENTITY);
			m_actors[cmp.index]->setPhysxActor(nullptr);
			m_dynamic_actors.eraseItem(m_actors[cmp.index]);
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else if (type == DISTANCE_JOINT_TYPE)
		{
			Entity entity = {cmp.index};
			auto& joint = m_distance_joints[entity];
			if (joint.physx) joint.physx->release();
			m_distance_joints.erase(entity);
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else if (type == HINGE_JOINT_TYPE)
		{
			Entity entity = {cmp.index};
			auto& joint = m_hinge_joints[entity];
			if(joint.physx) joint.physx->release();
			m_hinge_joints.erase(entity);
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else
		{
			ASSERT(false);
		}
	}


	ComponentHandle createDistanceJoint(Entity entity)
	{
		DistanceJoint joint;
		joint.physx = nullptr;
		joint.connected_body = INVALID_ENTITY;
		joint.stiffness = -1;
		joint.damping = -1;
		joint.tolerance = 0.025f;
		joint.distance_limit.set(-1, 10);
		m_distance_joints.insert(entity, joint);

		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, DISTANCE_JOINT_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createHingeJoint(Entity entity)
	{
		HingeJoint joint;
		joint.physx = nullptr;
		joint.connected_body = INVALID_ENTITY;
		joint.stiffness = -1;
		joint.damping = -1;
		joint.tolerance = 0.025f;
		joint.axis_direction.set(1, 0, 0);
		joint.axis_position.set(0, 0, 0);
		joint.use_limit = false;
		joint.limit.set(-1, 1);
		joint.initial_pos.set(0, 0, 0);
		m_hinge_joints.insert(entity, joint);

		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, HINGE_JOINT_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createHeightfield(Entity entity)
	{
		Heightfield* terrain = LUMIX_NEW(m_allocator, Heightfield)();
		m_terrains.push(terrain);
		terrain->m_heightmap = nullptr;
		terrain->m_scene = this;
		terrain->m_actor = nullptr;
		terrain->m_entity = entity;
		ComponentHandle cmp = {m_terrains.size() - 1};
		m_universe.addComponent(entity, HEIGHTFIELD_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createController(Entity entity)
	{
		physx::PxCapsuleControllerDesc cDesc;
		cDesc.material = m_default_material;
		cDesc.height = 1.8f;
		cDesc.radius = 0.25f;
		cDesc.slopeLimit = 0.0f;
		cDesc.contactOffset = 0.1f;
		cDesc.stepOffset = 0.02f;
		cDesc.callback = nullptr;
		cDesc.behaviorCallback = nullptr;
		Vec3 position = m_universe.getPosition(entity);
		cDesc.position.set(position.x, position.y, position.z);
		PhysicsSceneImpl::Controller& c = m_controllers.emplace();
		c.m_controller = m_controller_manager->createController(cDesc);
		c.m_entity = entity;
		c.m_is_free = false;
		c.m_frame_change.set(0, 0, 0);
		c.m_radius = cDesc.radius;
		c.m_height = cDesc.height;
		c.m_layer = 0;

		physx::PxFilterData data;
		int controller_layer = c.m_layer;
		data.word0 = 1 << controller_layer;
		data.word1 = m_collision_filter[controller_layer];
		physx::PxShape* shapes[8];
		int shapes_count = c.m_controller->getActor()->getShapes(shapes, lengthOf(shapes));
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}

		ComponentHandle cmp = { m_controllers.size() - 1 };
		m_universe.addComponent(entity, CONTROLLER_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createBoxRigidActor(Entity entity)
	{
		RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this);
		m_actors.push(actor);
		actor->setEntity(entity);

		physx::PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		physx::PxTransform transform;
		Matrix mtx = m_universe.getPositionAndRotation(entity);
		matrix2Transform(mtx, transform);

		physx::PxRigidStatic* physx_actor =
			PxCreateStatic(*m_system->getPhysics(), transform, geom, *m_default_material);
		actor->setPhysxActor(physx_actor);

		ComponentHandle cmp = {m_actors.size() - 1};
		m_universe.addComponent(entity, BOX_ACTOR_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createMeshRigidActor(Entity entity)
	{
		RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this);
		m_actors.push(actor);
		actor->setEntity(entity);

		ComponentHandle cmp = {m_actors.size() - 1};
		m_universe.addComponent(entity, MESH_ACTOR_TYPE, this, cmp);
		return cmp;
	}


	Path getHeightmap(ComponentHandle cmp) override
	{
		return m_terrains[cmp.index]->m_heightmap
				   ? m_terrains[cmp.index]->m_heightmap->getPath()
				   : Path("");
	}


	float getHeightmapXZScale(ComponentHandle cmp) override
	{
		return m_terrains[cmp.index]->m_xz_scale;
	}


	void setHeightmapXZScale(ComponentHandle cmp, float scale) override
	{
		auto* terrain = m_terrains[cmp.index];
		if (scale != terrain->m_xz_scale)
		{
			terrain->m_xz_scale = scale;
			if (terrain->m_heightmap && terrain->m_heightmap->isReady())
			{
				heightmapLoaded(terrain);
			}
		}
	}


	float getHeightmapYScale(ComponentHandle cmp) override
	{
		return m_terrains[cmp.index]->m_y_scale;
	}


	void setHeightmapYScale(ComponentHandle cmp, float scale) override
	{
		auto* terrain = m_terrains[cmp.index];
		if (scale != terrain->m_y_scale)
		{
			terrain->m_y_scale = scale;
			if (terrain->m_heightmap && terrain->m_heightmap->isReady())
			{
				heightmapLoaded(terrain);
			}
		}
	}


	void setHeightmap(ComponentHandle cmp, const Path& str) override
	{
		auto& resource_manager = m_engine->getResourceManager();
		auto* terrain = m_terrains[cmp.index];
		auto* old_hm = terrain->m_heightmap;
		if (old_hm)
		{
			resource_manager.get(TEXTURE_HASH)->unload(*old_hm);
			auto& cb = old_hm->getObserverCb();
			cb.unbind<Heightfield, &Heightfield::heightmapLoaded>(terrain);
		}
		auto* texture_manager = resource_manager.get(TEXTURE_HASH);
		if (str.isValid())
		{
			auto* new_hm = static_cast<Texture*>(texture_manager->load(str));
			terrain->m_heightmap = new_hm;
			new_hm->onLoaded<Heightfield, &Heightfield::heightmapLoaded>(terrain);
			new_hm->addDataReference();
		}
		else
		{
			terrain->m_heightmap = nullptr;
		}
	}


	Path getShapeSource(ComponentHandle cmp) override
	{
		return m_actors[cmp.index]->getResource() ? m_actors[cmp.index]->getResource()->getPath() : Path("");
	}


	void setShapeSource(ComponentHandle cmp, const Path& str) override
	{
		ASSERT(m_actors[cmp.index]);
		bool is_dynamic = isDynamic(cmp);
		auto& actor = *m_actors[cmp.index];
		if (actor.getResource() && actor.getResource()->getPath() == str &&
			(!actor.getPhysxActor() || is_dynamic == !actor.getPhysxActor()->isRigidStatic()))
		{
			return;
		}

		ResourceManagerBase* manager = m_engine->getResourceManager().get(PHYSICS_HASH);
		PhysicsGeometry* geom_res = static_cast<PhysicsGeometry*>(manager->load(str));

		actor.setPhysxActor(nullptr);
		actor.setResource(geom_res);
	}


	void setControllerPosition(int index, const Vec3& pos)
	{
		physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
		m_controllers[index].m_controller->setPosition(p);
	}


	static Vec3 toVec3(const physx::PxVec3& v) { return Vec3(v.x, v.y, v.z); }
	static physx::PxVec3 fromVec3(const Vec3& v) { return physx::PxVec3(v.x, v.y, v.z); }
	static physx::PxQuat fromQuat(const Quat& v) { return physx::PxQuat(v.x, v.y, v.z, v.w); }


	void render(RenderScene& render_scene) override
	{
		m_scene->getNbActors(physx::PxActorTypeSelectionFlag::eRIGID_STATIC);
		const physx::PxRenderBuffer& rb = m_scene->getRenderBuffer();
		const physx::PxU32 num_lines = rb.getNbLines();
		if (num_lines)
		{
			const physx::PxDebugLine* PX_RESTRICT lines = rb.getLines();
			for (physx::PxU32 i = 0; i < num_lines; ++i)
			{
				const physx::PxDebugLine& line = lines[i];
				Vec3 from = toVec3(line.pos0);
				Vec3 to = toVec3(line.pos1);
				render_scene.addDebugLine(from, to, line.color0, 0);
			}
		}
	}


	void updateDynamicActors()
	{
		PROFILE_FUNCTION();
		for (auto* actor : m_dynamic_actors)
		{
			physx::PxTransform trans = actor->getPhysxActor()->getGlobalPose();
			m_universe.setPosition(
				actor->getEntity(), trans.p.x, trans.p.y, trans.p.z);
			m_universe.setRotation(
				actor->getEntity(), trans.q.x, trans.q.y, trans.q.z, trans.q.w);
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
		Vec3 g(0, time_delta * -9.8f, 0);
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			if (m_controllers[i].m_is_free) continue;

			Vec3 dif = g + m_controllers[i].m_frame_change;
			m_controllers[i].m_frame_change.set(0, 0, 0);
			const physx::PxExtendedVec3& p = m_controllers[i].m_controller->getPosition();
			m_controllers[i].m_controller->move(physx::PxVec3(dif.x, dif.y, dif.z),
				0.01f,
				time_delta,
				physx::PxControllerFilters());

			float y = (float)p.y - m_controllers[i].m_height * 0.5f - m_controllers[i].m_radius;
			m_universe.setPosition(m_controllers[i].m_entity, (float)p.x, y, (float)p.z);
		}
	}


	void applyQueuedForces()
	{
		for (auto& i : m_queued_forces)
		{
			auto* actor = m_actors[i.cmp.index];
			if (!actor->isDynamic())
			{
				g_log_warning.log("Physics") << "Trying to apply force to static object";
				return;
			}

			auto* physx_actor = static_cast<physx::PxRigidDynamic*>(actor->getPhysxActor());
			if (!physx_actor) return;
			physx::PxVec3 f(i.force.x, i.force.y, i.force.z);
			physx_actor->addForce(f);
		}
		m_queued_forces.clear();
	}


	void update(float time_delta, bool paused) override
	{
		if (!m_is_game_running || paused) return;
		
		applyQueuedForces();

		time_delta = Math::minimum(1 / 20.0f, time_delta);
		simulateScene(time_delta);
		fetchResults();
		updateDynamicActors();
		updateControllers(time_delta);
	}


	ComponentHandle getActorComponent(Entity entity) override
	{
		for (int i = 0; i < m_actors.size(); ++i)
		{
			if (m_actors[i]->getEntity() == entity) return {i};
		}
		return INVALID_COMPONENT;
	}


	void deinitDistanceJoints()
	{
		for (int i = 0, c = m_distance_joints.size(); i < c; ++i)
		{
			auto& joint = m_distance_joints.at(i);
			if (joint.physx)
			{
				joint.physx->release();
				joint.physx = nullptr;
			}
		}
	}


	void deinitHingeJoints()
	{
		for (int i = 0, c = m_hinge_joints.size(); i < c; ++i)
		{
			auto& joint = m_hinge_joints.at(i);
			if (joint.physx)
			{
				joint.physx->release();
				joint.physx = nullptr;
			}
		}
	}


	void initDistanceJoints()
	{
		for (int i = 0, c = m_distance_joints.size(); i < c; ++i)
		{
			auto& joint = m_distance_joints.at(i);
			Entity entity = m_distance_joints.getKey(i);

			physx::PxRigidActor* actors[2] = { nullptr, nullptr };
			for (auto* actor : m_actors)
			{
				if (actor->getEntity() == entity) actors[0] = actor->getPhysxActor();
				if (actor->getEntity() == joint.connected_body) actors[1] = actor->getPhysxActor();
			}

			if (!actors[0] || !actors[1]) continue;

			physx::PxTransform identity = physx::PxTransform::createIdentity();
			joint.physx = physx::PxDistanceJointCreate(*m_system->getPhysics(), actors[0], identity, actors[1], identity);
			if (!joint.physx)
			{
				g_log_error.log("Physics") << "Failed to create joint between " << entity.index << " and "
										   << joint.connected_body.index;
				continue;
			}

			physx::PxDistanceJointFlags flags;
			if (joint.distance_limit.y >= 0)
			{
				flags |= physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED;
				joint.physx->setMaxDistance(joint.distance_limit.y);
				auto x = joint.physx->getMaxDistance();
				x = x;
			}
			if (joint.distance_limit.x >= 0)
			{
				flags |= physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED;
				joint.physx->setMinDistance(joint.distance_limit.x);
			}
			if (joint.damping >= 0)
			{
				flags |= physx::PxDistanceJointFlag::eSPRING_ENABLED;
				joint.physx->setDamping(joint.damping);
			}
			if (joint.stiffness >= 0)
			{
				flags |= physx::PxDistanceJointFlag::eSPRING_ENABLED;
				joint.physx->setStiffness(joint.stiffness);
			}
			joint.physx->setTolerance(joint.tolerance);
			joint.physx->setDistanceJointFlags(flags);
		}
	}


	void initHingeJoints()
	{
		for (int i = 0, c = m_hinge_joints.size(); i < c; ++i)
		{
			auto& joint = m_hinge_joints.at(i);
			Entity entity = m_hinge_joints.getKey(i);

			physx::PxRigidActor* actors[2] = { nullptr, nullptr };
			for (auto* actor : m_actors)
			{
				if (actor->getEntity() == entity) actors[0] = actor->getPhysxActor();
				if (actor->getEntity() == joint.connected_body) actors[1] = actor->getPhysxActor();
			}

			if (!actors[0] || !actors[1]) continue;

			Vec3 pos0 = m_universe.getPosition(entity);
			Quat rot0 = m_universe.getRotation(entity);
			Vec3 pos1 = m_universe.getPosition(joint.connected_body);
			Quat rot1 = m_universe.getRotation(joint.connected_body);
			physx::PxTransform entity0_frame(fromVec3(pos0), fromQuat(rot0));
			physx::PxTransform entity1_frame(fromVec3(pos1), fromQuat(rot1));

			Vec3 local_axis_dir = joint.axis_direction.normalized();
			Quat axis_quat = Quat::vec3ToVec3(Vec3(1, 0, 0), local_axis_dir);
			physx::PxTransform axis_local_frame0(fromVec3(joint.axis_position), fromQuat(axis_quat));
			physx::PxTransform axis_local_frame1 = entity1_frame.getInverse() * entity0_frame * axis_local_frame0;

			joint.physx = physx::PxRevoluteJointCreate(
				*m_system->getPhysics(), actors[0], axis_local_frame0, actors[1], axis_local_frame1);
			if (!joint.physx)
			{
				g_log_error.log("Physics") << "Failed to create joint between " << entity.index << " and "
										   << joint.connected_body.index;
				continue;
			}

			joint.initial_pos = pos1;

			if (joint.use_limit)
			{
				physx::PxRevoluteJointFlags flags;
				flags |= physx::PxRevoluteJointFlag::eLIMIT_ENABLED;
				joint.physx->setRevoluteJointFlags(flags);

				physx::PxSpring spring(joint.stiffness, joint.damping);
				physx::PxJointAngularLimitPair limit(joint.limit.x, joint.limit.y, 0.1f);
				joint.physx->setLimit(limit);
			}
		}
	}


	void startGame() override
	{
		auto* scene = m_universe.getScene(crc32("lua_script"));
		m_script_scene = static_cast<LuaScriptScene*>(scene);
		m_is_game_running = true;

		initDistanceJoints();
		initHingeJoints();
	}


	void stopGame() override
	{
		deinitDistanceJoints();
		deinitHingeJoints();
		m_is_game_running = false;
	}


	float getControllerRadius(ComponentHandle cmp) override { return m_controllers[cmp.index].m_radius; }
	float getControllerHeight(ComponentHandle cmp) override { return m_controllers[cmp.index].m_height; }


	ComponentHandle getController(Entity entity) override
	{
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			if (m_controllers[i].m_entity == entity)
			{
				return {i};
			}
		}
		return INVALID_COMPONENT;
	}


	void moveController(ComponentHandle cmp, const Vec3& v) override { m_controllers[cmp.index].m_frame_change += v; }


	static int LUA_raycast(lua_State* L)
	{
		auto* scene = LuaWrapper::checkArg<PhysicsSceneImpl*>(L, 1);
		Vec3 origin = LuaWrapper::checkArg<Vec3>(L, 2);
		Vec3 dir = LuaWrapper::checkArg<Vec3>(L, 3);

		RaycastHit hit;
		if (scene->raycastEx(origin, dir, FLT_MAX, hit))
		{
			LuaWrapper::pushLua(L, hit.entity != INVALID_ENTITY);
			LuaWrapper::pushLua(L, hit.entity);
			LuaWrapper::pushLua(L, hit.position);
			return 3;
		}
		LuaWrapper::pushLua(L, false);
		return 1;
	}


	Entity raycast(const Vec3& origin, const Vec3& dir) override
	{
		RaycastHit hit;
		if (raycastEx(origin, dir, FLT_MAX, hit)) return hit.entity;
		return INVALID_ENTITY;
	}


	bool raycastEx(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result) override
	{
		physx::PxVec3 physx_origin(origin.x, origin.y, origin.z);
		physx::PxVec3 unit_dir(dir.x, dir.y, dir.z);
		physx::PxReal max_distance = distance;
		physx::PxRaycastHit hit;

		const physx::PxSceneQueryFlags outputFlags =
			physx::PxSceneQueryFlag::eDISTANCE | physx::PxSceneQueryFlag::eIMPACT | physx::PxSceneQueryFlag::eNORMAL;

		bool status = m_scene->raycastSingle(physx_origin, unit_dir, max_distance, outputFlags, hit);
		result.normal.x = hit.normal.x;
		result.normal.y = hit.normal.y;
		result.normal.z = hit.normal.z;
		result.position.x = hit.position.x;
		result.position.y = hit.position.y;
		result.position.z = hit.position.z;
		result.entity = INVALID_ENTITY;
		if (hit.shape)
		{
			physx::PxRigidActor* actor = hit.shape->getActor();
			if (actor && actor->userData) result.entity = {(int)(intptr_t)actor->userData};
		}
		return status;
	}


	void onEntityMoved(Entity entity)
	{
		for (int i = 0, c = m_dynamic_actors.size(); i < c; ++i)
		{
			if (m_dynamic_actors[i]->getEntity() == entity)
			{
				Vec3 pos = m_universe.getPosition(entity);
				physx::PxVec3 pvec(pos.x, pos.y, pos.z);
				Quat q = m_universe.getRotation(entity);
				physx::PxQuat pquat(q.x, q.y, q.z, q.w);
				physx::PxTransform trans(pvec, pquat);
				m_dynamic_actors[i]->getPhysxActor()->setGlobalPose(trans, false);
				return;
			}
		}

		for (int i = 0, c = m_controllers.size(); i < c; ++i)
		{
			if (m_controllers[i].m_entity == entity)
			{
				Vec3 pos = m_universe.getPosition(entity);
				pos.y += m_controllers[i].m_height * 0.5f;
				pos.y += m_controllers[i].m_radius;
				physx::PxExtendedVec3 pvec(pos.x, pos.y, pos.z);
				m_controllers[i].m_controller->setPosition(pvec);
				return;
			}
		}

		for (int i = 0, c = m_actors.size(); i < c; ++i)
		{
			if (m_actors[i]->getEntity() == entity)
			{
				Vec3 pos = m_universe.getPosition(entity);
				physx::PxVec3 pvec(pos.x, pos.y, pos.z);
				Quat q = m_universe.getRotation(entity);
				physx::PxQuat pquat(q.x, q.y, q.z, q.w);
				physx::PxTransform trans(pvec, pquat);
				m_actors[i]->getPhysxActor()->setGlobalPose(trans, false);
				return;
			}
		}
	}


	void heightmapLoaded(Heightfield* terrain)
	{
		PROFILE_FUNCTION();
		Array<physx::PxHeightFieldSample> heights(m_allocator);

		int width = terrain->m_heightmap->width;
		int height = terrain->m_heightmap->height;
		heights.resize(width * height);
		int bytes_per_pixel = terrain->m_heightmap->bytes_per_pixel;
		if (bytes_per_pixel == 2)
		{
			PROFILE_BLOCK("copyData");
			const int16* LUMIX_RESTRICT data = (const int16*)terrain->m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				int idx = j * width;
				for (int i = 0; i < width; ++i)
				{
					int idx2 = j + i * height;
					heights[idx].height = physx::PxI16((int32)data[idx2] - 0x7fff);
					heights[idx].materialIndex0 = heights[idx].materialIndex1 = 0;
					heights[idx].setTessFlag();
					++idx;
				}
			}
		}
		else
		{
			PROFILE_BLOCK("copyData");
			const uint8* data = terrain->m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				for (int i = 0; i < width; ++i)
				{
					int idx = i + j * width;
					int idx2 = j + i * height;
					heights[idx].height = physx::PxI16((int32)data[idx2 * bytes_per_pixel] - 0x7f);
					heights[idx].materialIndex0 = heights[idx].materialIndex1 = 0;
					heights[idx].setTessFlag();
				}
			}
		}

		{ // PROFILE_BLOCK scope
			PROFILE_BLOCK("PhysX");
			physx::PxHeightFieldDesc hfDesc;
			hfDesc.format = physx::PxHeightFieldFormat::eS16_TM;
			hfDesc.nbColumns = width;
			hfDesc.nbRows = height;
			hfDesc.samples.data = &heights[0];
			hfDesc.samples.stride = sizeof(physx::PxHeightFieldSample);
			hfDesc.thickness = -1;

			physx::PxHeightField* heightfield = m_system->getPhysics()->createHeightField(hfDesc);
			float height_scale = bytes_per_pixel == 2 ? 1 / (256 * 256.0f - 1) : 1 / 255.0f;
			physx::PxHeightFieldGeometry hfGeom(heightfield,
				physx::PxMeshGeometryFlags(),
				height_scale * terrain->m_y_scale,
				terrain->m_xz_scale,
				terrain->m_xz_scale);
			if (terrain->m_actor)
			{
				physx::PxRigidActor* actor = terrain->m_actor;
				m_scene->removeActor(*actor);
				actor->release();
				terrain->m_actor = nullptr;
			}

			physx::PxTransform transform;
			Matrix mtx = m_universe.getPositionAndRotation(terrain->m_entity);
			mtx.translate(0, terrain->m_y_scale * 0.5f, 0);
			matrix2Transform(mtx, transform);

			physx::PxRigidActor* actor;
			actor = PxCreateStatic(*m_system->getPhysics(), transform, hfGeom, *m_default_material);
			if (actor)
			{
				actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, width <= 1024);
				actor->userData = (void*)(intptr_t)terrain->m_entity.index;
				m_scene->addActor(*actor);
				terrain->m_actor = actor;

				physx::PxFilterData data;
				int terrain_layer = terrain->m_layer;
				data.word0 = 1 << terrain_layer;
				data.word1 = m_collision_filter[terrain_layer];
				physx::PxShape* shapes[8];
				int shapes_count = actor->getShapes(shapes, lengthOf(shapes));
				for (int i = 0; i < shapes_count; ++i)
				{
					shapes[i]->setSimulationFilterData(data);
				}
			}
			else
			{
				g_log_error.log("Physics") << "Could not create PhysX heightfield " << terrain->m_heightmap->getPath();
			}
		}
	}


	void addCollisionLayer() override 
	{
		m_layers_count = Math::minimum(lengthOf(m_layers_names), m_layers_count + 1);
	}


	void removeCollisionLayer() override
	{
		m_layers_count = Math::maximum(0, m_layers_count - 1);
		for (auto* actor : m_actors)
		{
			if (!actor->getPhysxActor()) continue;
			if (actor->getEntity() == INVALID_ENTITY) continue;
			actor->setLayer(Math::minimum(m_layers_count - 1, actor->getLayer()));
		}
		for (auto& controller : m_controllers)
		{
			if (controller.m_is_free) continue;
			controller.m_layer = Math::minimum(m_layers_count - 1, controller.m_layer);
		}
		for (auto* terrain : m_terrains)
		{
			if (!terrain) continue;
			if (!terrain->m_actor) continue;
			terrain->m_layer = Math::minimum(m_layers_count - 1, terrain->m_layer);
		}

		updateFilterData();
	}


	void setCollisionLayerName(int index, const char* name) override
	{
		copyString(m_layers_names[index], name);
	}


	const char* getCollisionLayerName(int index) override
	{
		return m_layers_names[index];
	}


	bool canLayersCollide(int layer1, int layer2) override
	{
		return (m_collision_filter[layer1] & (1 << layer2)) != 0;
	}


	void setLayersCanCollide(int layer1, int layer2, bool can_collide) override
	{
		if (can_collide)
		{
			m_collision_filter[layer1] |= 1 << layer2;
			m_collision_filter[layer2] |= 1 << layer1;
		}
		else
		{
			m_collision_filter[layer1] &= ~(1 << layer2);
			m_collision_filter[layer2] &= ~(1 << layer1);
		}

		updateFilterData();
	}


	void updateFilterData(physx::PxRigidActor* actor, int layer)
	{
		physx::PxFilterData data;
		data.word0 = 1 << layer;
		data.word1 = m_collision_filter[layer];
		physx::PxShape* shapes[8];
		int shapes_count = actor->getShapes(shapes, lengthOf(shapes));
		for (int i = 0; i < shapes_count; ++i)
		{
			shapes[i]->setSimulationFilterData(data);
		}
	}


	void updateFilterData()
	{
		for (auto* actor : m_actors)
		{
			if (!actor->getPhysxActor()) continue;
			if (actor->getEntity() == INVALID_ENTITY) continue;

			physx::PxFilterData data;
			int actor_layer = actor->getLayer();
			data.word0 = 1 << actor_layer;
			data.word1 = m_collision_filter[actor_layer];
			physx::PxShape* shapes[8];
			int shapes_count = actor->getPhysxActor()->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}

		for (auto& controller : m_controllers)
		{
			if (controller.m_is_free) continue;

			physx::PxFilterData data;
			int controller_layer = controller.m_layer;
			data.word0 = 1 << controller_layer;
			data.word1 = m_collision_filter[controller_layer];
			physx::PxShape* shapes[8];
			int shapes_count = controller.m_controller->getActor()->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}

		for (auto* terrain : m_terrains)
		{
			if (!terrain) continue;
			if (!terrain->m_actor) continue;

			physx::PxFilterData data;
			int terrain_layer = terrain->m_layer;
			data.word0 = 1 << terrain_layer;
			data.word1 = m_collision_filter[terrain_layer];
			physx::PxShape* shapes[8];
			int shapes_count = terrain->m_actor->getShapes(shapes, lengthOf(shapes));
			for (int i = 0; i < shapes_count; ++i)
			{
				shapes[i]->setSimulationFilterData(data);
			}
		}
	}


	int getCollisionsLayersCount() const override
	{
		return m_layers_count;
	}


	bool isDynamic(ComponentHandle cmp) override
	{
		RigidActor* actor = m_actors[cmp.index];
		return isDynamic(actor);
	}


	bool isDynamic(RigidActor* actor)
	{
		for (int i = 0, c = m_dynamic_actors.size(); i < c; ++i)
		{
			if (m_dynamic_actors[i] == actor)
			{
				return true;
			}
		}
		return false;
	}


	Vec3 getHalfExtents(ComponentHandle cmp) override
	{
		Vec3 size;
		physx::PxRigidActor* actor = m_actors[cmp.index]->getPhysxActor();
		physx::PxShape* shapes;
		if (actor->getNbShapes() == 1 &&
			m_actors[cmp.index]->getPhysxActor()->getShapes(&shapes, 1))
		{
			physx::PxVec3& half = shapes->getGeometry().box().halfExtents;
			size.x = half.x;
			size.y = half.y;
			size.z = half.z;
		}
		return size;
	}


	void setHalfExtents(ComponentHandle cmp, const Vec3& size) override
	{
		physx::PxRigidActor* actor = m_actors[cmp.index]->getPhysxActor();
		physx::PxShape* shapes;
		if (actor->getNbShapes() == 1 && m_actors[cmp.index]->getPhysxActor()->getShapes(&shapes, 1))
		{
			physx::PxBoxGeometry box;
			bool is_box = shapes->getBoxGeometry(box);
			ASSERT(is_box);
			physx::PxVec3& half = box.halfExtents;
			half.x = Math::maximum(0.01f, size.x);
			half.y = Math::maximum(0.01f, size.y);
			half.z = Math::maximum(0.01f, size.z);
			shapes->setGeometry(box);
		}
	}


	void setIsDynamic(ComponentHandle cmp, bool new_value) override
	{
		RigidActor* actor = m_actors[cmp.index];
		int dynamic_index = m_dynamic_actors.indexOf(actor);
		bool is_dynamic = dynamic_index != -1;
		if (is_dynamic == new_value) return;

		actor->setDynamic(new_value);
		if (new_value)
		{
			m_dynamic_actors.push(actor);
		}
		else
		{
			m_dynamic_actors.eraseItemFast(actor);
		}
		physx::PxShape* shapes;
		if (actor->getPhysxActor()->getNbShapes() == 1 && actor->getPhysxActor()->getShapes(&shapes, 1, 0))
		{
			physx::PxGeometryHolder geom = shapes->getGeometry();

			physx::PxTransform transform;
			matrix2Transform(m_universe.getPositionAndRotation(actor->getEntity()), transform);

			physx::PxRigidActor* physx_actor;
			if (new_value)
			{
				physx_actor = PxCreateDynamic(*m_system->getPhysics(), transform, geom.any(), *m_default_material, 1.0f);
			}
			else
			{
				physx_actor = PxCreateStatic(*m_system->getPhysics(), transform, geom.any(), *m_default_material);
			}
			ASSERT(actor);
			physx_actor->userData = (void*)(intptr_t)actor->getEntity().index;
			physx_actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
			actor->setPhysxActor(physx_actor);
		}
	}


	void serializeActor(OutputBlob& serializer, int idx)
	{
		serializer.write(m_actors[idx]->getLayer());
		physx::PxShape* shapes;
		if (m_actors[idx]->getPhysxActor()->getNbShapes() == 1 &&
			m_actors[idx]->getPhysxActor()->getShapes(&shapes, 1))
		{
			physx::PxBoxGeometry geom;
			physx::PxConvexMeshGeometry convex_geom;
			physx::PxHeightFieldGeometry hf_geom;
			physx::PxTriangleMeshGeometry trimesh_geom;
			if (shapes->getBoxGeometry(geom))
			{
				serializer.write((int32)BOX);
				serializer.write(geom.halfExtents.x);
				serializer.write(geom.halfExtents.y);
				serializer.write(geom.halfExtents.z);
			}
			else if (shapes->getConvexMeshGeometry(convex_geom))
			{
				serializer.write((int32)CONVEX);
				serializer.writeString(
					m_actors[idx]->getResource()
						? m_actors[idx]->getResource()->getPath().c_str()
						: "");
			}
			else if (shapes->getTriangleMeshGeometry(trimesh_geom))
			{
				serializer.write((int32)TRIMESH);
				serializer.writeString(
					m_actors[idx]->getResource()
						? m_actors[idx]->getResource()->getPath().c_str()
						: "");
			}
			else
			{
				ASSERT(false);
			}
		}
		else
		{
			ASSERT(false);
		}
	}


	void deserializeActor(InputBlob& serializer, ComponentHandle cmp, int version)
	{
		int layer = 0;
		if (version > (int)PhysicsSceneVersion::LAYERS) serializer.read(layer);
		auto* actor = m_actors[cmp.index];
		actor->setLayer(layer);

		ActorType type;
		serializer.read((int32&)type);

		switch (type)
		{
			case BOX:
			{
				physx::PxBoxGeometry box_geom;
				physx::PxTransform transform;
				Matrix mtx = m_universe.getPositionAndRotation(actor->getEntity());
				matrix2Transform(mtx, transform);
				serializer.read(box_geom.halfExtents.x);
				serializer.read(box_geom.halfExtents.y);
				serializer.read(box_geom.halfExtents.z);
				physx::PxRigidActor* physx_actor;
				if (isDynamic(cmp))
				{
					physx_actor =
						PxCreateDynamic(*m_system->getPhysics(), transform, box_geom, *m_default_material, 1.0f);
				}
				else
				{
					physx_actor = PxCreateStatic(*m_system->getPhysics(), transform, box_geom, *m_default_material);
				}
				actor->setPhysxActor(physx_actor);
				m_universe.addComponent(actor->getEntity(), BOX_ACTOR_TYPE, this, cmp);
			}
			break;
			case TRIMESH:
			case CONVEX:
			{
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, sizeof(tmp));
				ResourceManagerBase* manager = m_engine->getResourceManager().get(PHYSICS_HASH);
				auto* geometry = manager->load(Lumix::Path(tmp));
				actor->setResource(static_cast<PhysicsGeometry*>(geometry));
				m_universe.addComponent(actor->getEntity(), MESH_ACTOR_TYPE, this, cmp);
			}
			break;
			default:
				ASSERT(false);
				break;
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_layers_count);
		serializer.write(m_layers_names);
		serializer.write(m_collision_filter);
		serializer.write((int32)m_actors.size());
		for (int i = 0; i < m_actors.size(); ++i)
		{
			serializer.write(isDynamic({i}));
			serializer.write(m_actors[i]->getEntity());
			if (isValid(m_actors[i]->getEntity()))
			{
				serializeActor(serializer, i);
			}
		}
		serializer.write((int32)m_controllers.size());
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			serializer.write(m_controllers[i].m_entity);
			serializer.write(m_controllers[i].m_is_free);
			if (!m_controllers[i].m_is_free)
			{
				serializer.write(m_controllers[i].m_layer);
			}
		}
		serializer.write((int32)m_terrains.size());
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i])
			{
				serializer.write(true);
				serializer.write(m_terrains[i]->m_entity);
				serializer.writeString(m_terrains[i]->m_heightmap
										   ? m_terrains[i]->m_heightmap->getPath().c_str()
										   : "");
				serializer.write(m_terrains[i]->m_xz_scale);
				serializer.write(m_terrains[i]->m_y_scale);
				serializer.write(m_terrains[i]->m_layer);
			}
			else
			{
				serializer.write(false);
			}
		}
		serializeJoints(serializer);
	}


	void serializeJoints(OutputBlob& serializer)
	{
		serializer.write(m_distance_joints.size());
		for (int i = 0; i < m_distance_joints.size(); ++i)
		{
			const DistanceJoint& joint = m_distance_joints.at(i);
			serializer.write(m_distance_joints.getKey(i));
			serializer.write(joint.damping);
			serializer.write(joint.stiffness);
			serializer.write(joint.tolerance);
			serializer.write(joint.distance_limit);
			serializer.write(joint.connected_body);
		}

		serializer.write(m_hinge_joints.size());
		for (int i = 0; i < m_hinge_joints.size(); ++i)
		{
			const HingeJoint& joint = m_hinge_joints.at(i);
			serializer.write(m_hinge_joints.getKey(i));
			serializer.write(joint.damping);
			serializer.write(joint.stiffness);
			serializer.write(joint.tolerance);
			serializer.write(joint.connected_body);
			serializer.write(joint.axis_position);
			serializer.write(joint.axis_direction);
			serializer.write(joint.use_limit);
			serializer.write(joint.limit);
		}
	}



	void deserializeActors(InputBlob& serializer, int version)
	{
		int32 count;
		m_dynamic_actors.clear();
		serializer.read(count);
		for (int i = count; i < m_actors.size(); ++i)
		{
			m_actors[i]->setPhysxActor(nullptr);
		}
		int old_size = m_actors.size();
		m_actors.resize(count);
		for (int i = old_size; i < count; ++i)
		{
			RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this);
			m_actors[i] = actor;
		}
		for (int i = 0; i < m_actors.size(); ++i)
		{
			bool is_dynamic;
			serializer.read(is_dynamic);
			if (is_dynamic)
			{
				m_dynamic_actors.push(m_actors[i]);
			}
			m_actors[i]->setDynamic(is_dynamic);

			Entity e;
			serializer.read(e);
			m_actors[i]->setEntity(e);

			if (isValid(m_actors[i]->getEntity()))
			{
				deserializeActor(serializer, {i}, version);
			}
		}
	}


	void deserializeControllers(InputBlob& serializer, int version)
	{
		int32 count;
		serializer.read(count);
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			if (!m_controllers[i].m_is_free)
			{
				m_controllers[i].m_controller->release();
			}
		}
		m_controllers.clear();
		for (int i = 0; i < count; ++i)
		{
			Entity e;
			bool is_free;
			serializer.read(e);
			serializer.read(is_free);

			Controller& c = m_controllers.emplace();
			c.m_is_free = is_free;
			c.m_frame_change.set(0, 0, 0);

			if (is_free) continue;

			if (version > (int)PhysicsSceneVersion::LAYERS)
			{
				serializer.read(c.m_layer);
			}
			else
			{
				c.m_layer = 0;
			}
			physx::PxCapsuleControllerDesc cDesc;
			cDesc.material = m_default_material;
			cDesc.height = 1.8f;
			cDesc.radius = 0.25f;
			cDesc.slopeLimit = 0.0f;
			cDesc.contactOffset = 0.1f;
			cDesc.stepOffset = 0.02f;
			cDesc.callback = nullptr;
			cDesc.behaviorCallback = nullptr;
			Vec3 position = m_universe.getPosition(e);
			cDesc.position.set(position.x, position.y - cDesc.height * 0.5f, position.z);
			c.m_controller = m_controller_manager->createController(*m_system->getPhysics(), m_scene, cDesc);
			c.m_entity = e;
			m_universe.addComponent(e, CONTROLLER_TYPE, this, {i});
		}
	}


	void deserializeJoints(InputBlob& serializer, int version)
	{
		if (version <= int(PhysicsSceneVersion::JOINTS)) return;

		int count;
		serializer.read(count);
		m_distance_joints.clear();
		m_distance_joints.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			Entity entity;
			serializer.read(entity);
			DistanceJoint joint;
			serializer.read(joint.damping);
			serializer.read(joint.stiffness);
			serializer.read(joint.tolerance);
			serializer.read(joint.distance_limit);
			serializer.read(joint.connected_body);
			joint.physx = nullptr;
			m_distance_joints.insert(entity, joint);
			ComponentHandle cmp = {entity.index};
			m_universe.addComponent(entity, DISTANCE_JOINT_TYPE, this, cmp);
		}

		serializer.read(count);
		m_hinge_joints.clear();
		m_hinge_joints.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			Entity entity;
			serializer.read(entity);
			HingeJoint joint;
			serializer.read(joint.damping);
			serializer.read(joint.stiffness);
			serializer.read(joint.tolerance);
			serializer.read(joint.connected_body);
			serializer.read(joint.axis_position);
			serializer.read(joint.axis_direction);
			serializer.read(joint.use_limit);
			serializer.read(joint.limit);
			joint.physx = nullptr;
			m_hinge_joints.insert(entity, joint);
			ComponentHandle cmp = {entity.index};
			m_universe.addComponent(entity, HINGE_JOINT_TYPE, this, cmp);
		}
	}


	void deserializeTerrains(InputBlob& serializer, int version)
	{
		int32 count;
		serializer.read(count);
		for (int i = count; i < m_terrains.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_terrains[i]);
			m_terrains[i] = nullptr;
		}
		int old_size = m_terrains.size();
		m_terrains.resize(count);
		for (int i = old_size; i < count; ++i)
		{
			m_terrains[i] = nullptr;
		}
		for (int i = 0; i < count; ++i)
		{
			bool exists;
			serializer.read(exists);
			if (exists)
			{
				if (!m_terrains[i])
				{
					m_terrains[i] = LUMIX_NEW(m_allocator, Heightfield);
				}
				m_terrains[i]->m_scene = this;
				serializer.read(m_terrains[i]->m_entity);
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				serializer.read(m_terrains[i]->m_xz_scale);
				serializer.read(m_terrains[i]->m_y_scale);
				if (version > (int)PhysicsSceneVersion::LAYERS)
				{
					serializer.read(m_terrains[i]->m_layer);
				}
				else
				{
					m_terrains[i]->m_layer = 0;
				}

				if (m_terrains[i]->m_heightmap == nullptr ||
					!equalStrings(tmp, m_terrains[i]->m_heightmap->getPath().c_str()))
				{
					setHeightmap({i}, Path(tmp));
				}
				m_universe.addComponent(m_terrains[i]->m_entity, HEIGHTFIELD_TYPE, this, {i});
			}
		}
	}


	void deserialize(InputBlob& serializer, int version) override
	{
		if (version > (int)PhysicsSceneVersion::LAYERS)
		{
			serializer.read(m_layers_count);
			serializer.read(m_layers_names);
			serializer.read(m_collision_filter);
		}

		deserializeActors(serializer, version);
		deserializeControllers(serializer, version);
		deserializeTerrains(serializer, version);
		deserializeJoints(serializer, version);

		updateFilterData();
	}


	PhysicsSystem& getSystem() const override { return *m_system; }


	int getVersion() const override { return (int)PhysicsSceneVersion::LATEST; }


	float getActorSpeed(ComponentHandle cmp) override
	{
		auto* actor = m_actors[cmp.index];
		if (!actor->isDynamic())
		{
			g_log_warning.log("Physics") << "Trying to get speed of static object";
			return 0;
		}

		auto* physx_actor = static_cast<physx::PxRigidDynamic*>(actor->getPhysxActor());
		if (!physx_actor) return 0;
		return physx_actor->getLinearVelocity().magnitude();
	}


	void putToSleep(ComponentHandle cmp) override
	{
		auto* actor = m_actors[cmp.index];
		if (!actor->isDynamic())
		{
			g_log_warning.log("Physics") << "Trying to put static object to sleep";
			return;
		}

		auto* physx_actor = static_cast<physx::PxRigidDynamic*>(actor->getPhysxActor());
		if (!physx_actor) return;
		physx_actor->putToSleep();
	}


	void applyForceToActor(ComponentHandle cmp, const Vec3& force) override
	{
		auto& i = m_queued_forces.emplace();
		i.cmp = cmp;
		i.force = force;
	}


	static physx::PxFilterFlags filterShader(
		physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
		physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
		physx::PxPairFlags& pairFlags, const void* constantBlock, physx::PxU32 constantBlockSize)
	{
		if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
		{
			pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
			return physx::PxFilterFlag::eDEFAULT;
		}

		if (!(filterData0.word0 & filterData1.word1) || !(filterData1.word0 & filterData0.word1))
		{
			return physx::PxFilterFlag::eKILL;
		}
		pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND |
					physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return physx::PxFilterFlag::eDEFAULT;
	}


	struct QueuedForce
	{
		ComponentHandle cmp;
		Vec3 force;
	};


	struct Controller
	{
		physx::PxController* m_controller;
		Entity m_entity;
		Vec3 m_frame_change;
		float m_radius;
		float m_height;
		bool m_is_free;
		int m_layer;
	};

	IAllocator& m_allocator;

	Universe& m_universe;
	Engine* m_engine;
	ContactCallback m_contact_callback;
	physx::PxScene* m_scene;
	LuaScriptScene* m_script_scene;
	PhysicsSystem* m_system;
	physx::PxControllerManager* m_controller_manager;
	physx::PxMaterial* m_default_material;
	Array<RigidActor*> m_actors;
	Array<RigidActor*> m_dynamic_actors;
	AssociativeArray<Entity, DistanceJoint> m_distance_joints;
	AssociativeArray<Entity, HingeJoint> m_hinge_joints;
	bool m_is_game_running;

	Array<QueuedForce> m_queued_forces;
	Array<Controller> m_controllers;
	Array<Heightfield*> m_terrains;
	uint32 m_collision_filter[32];
	char m_layers_names[32][30];
	int m_layers_count;
};


PhysicsScene* PhysicsScene::create(PhysicsSystem& system,
	Universe& context,
	Engine& engine,
	IAllocator& allocator)
{
	PhysicsSceneImpl* impl = LUMIX_NEW(allocator, PhysicsSceneImpl)(context, allocator);
	impl->m_universe.entityTransformed().bind<PhysicsSceneImpl, &PhysicsSceneImpl::onEntityMoved>(
		impl);
	impl->m_engine = &engine;
	physx::PxSceneDesc sceneDesc(system.getPhysics()->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
	if (!sceneDesc.cpuDispatcher)
	{
		physx::PxDefaultCpuDispatcher* cpu_dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
		if (!cpu_dispatcher)
		{
			g_log_error.log("Physics") << "PxDefaultCpuDispatcherCreate failed!";
		}
		sceneDesc.cpuDispatcher = cpu_dispatcher;
	}

	sceneDesc.filterShader = impl->filterShader;
	sceneDesc.simulationEventCallback = &impl->m_contact_callback;

	impl->m_scene = system.getPhysics()->createScene(sceneDesc);
	if (!impl->m_scene)
	{
		LUMIX_DELETE(allocator, impl);
		return nullptr;
	}

	impl->m_controller_manager = PxCreateControllerManager(*impl->m_scene);

	impl->m_system = &system;
	impl->m_default_material =
		impl->m_system->getPhysics()->createMaterial(0.5, 0.5, 0.5);
	return impl;
}


void PhysicsScene::destroy(PhysicsScene* scene)
{
	PhysicsSceneImpl* impl = static_cast<PhysicsSceneImpl*>(scene);
	impl->m_controller_manager->release();
	impl->m_default_material->release();
	impl->m_scene->release();
	LUMIX_DELETE(impl->m_allocator, scene);
}


void PhysicsSceneImpl::RigidActor::onStateChanged(Resource::State, Resource::State new_state)
{
	if (new_state == Resource::State::READY)
	{
		setPhysxActor(nullptr);

		physx::PxTransform transform;
		Matrix mtx = m_scene.getUniverse().getPositionAndRotation(m_entity);
		matrix2Transform(mtx, transform);

		physx::PxRigidActor* actor;
		bool is_dynamic = m_scene.isDynamic(this);
		if (is_dynamic)
		{
			actor = PxCreateDynamic(*m_scene.m_system->getPhysics(),
									transform,
									*m_resource->getGeometry(),
									*m_scene.m_default_material,
									1.0f);
		}
		else
		{
			actor = PxCreateStatic(*m_scene.m_system->getPhysics(),
								   transform,
								   *m_resource->getGeometry(),
								   *m_scene.m_default_material);
		}
		if (actor)
		{
			setPhysxActor(actor);
		}
		else
		{
			g_log_error.log("Physics") << "Could not create PhysX mesh "
									 << m_resource->getPath().c_str();
		}
	}
}


void PhysicsSceneImpl::RigidActor::setPhysxActor(physx::PxRigidActor* actor)
{
	if (m_physx_actor)
	{
		m_scene.m_scene->removeActor(*m_physx_actor);
		m_physx_actor->release();
	}
	m_physx_actor = actor;
	if (actor)
	{
		m_scene.m_scene->addActor(*actor);
		actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
		actor->userData = (void*)(intptr_t)m_entity.index;
		m_scene.updateFilterData(actor, m_layer);
	}
}


void PhysicsSceneImpl::RigidActor::setResource(PhysicsGeometry* resource)
{
	if (m_resource)
	{
		m_resource->getObserverCb().unbind<RigidActor, &RigidActor::onStateChanged>(this);
		m_resource->getResourceManager().get(PHYSICS_HASH)->unload(*m_resource);
	}
	m_resource = resource;
	if (resource)
	{
		m_resource->onLoaded<RigidActor, &RigidActor::onStateChanged>(this);
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
	if (m_heightmap)
	{
		m_heightmap->getResourceManager()
			.get(TEXTURE_HASH)
			->unload(*m_heightmap);
		m_heightmap->getObserverCb().unbind<Heightfield, &Heightfield::heightmapLoaded>(
			this);
	}
}


void Heightfield::heightmapLoaded(Resource::State, Resource::State new_state)
{
	if (new_state == Resource::State::READY)
	{
		m_scene->heightmapLoaded(this);
	}
}


void PhysicsScene::registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<PhysicsSceneImpl, decltype(&PhysicsSceneImpl::name), &PhysicsSceneImpl::name>; \
			LuaWrapper::createSystemFunction(L, "Physics", #name, f); \
		} while(false) \

	REGISTER_FUNCTION(getActorComponent);
	REGISTER_FUNCTION(putToSleep);
	REGISTER_FUNCTION(getActorSpeed);
	REGISTER_FUNCTION(applyForceToActor);
	REGISTER_FUNCTION(moveController);
	
	LuaWrapper::createSystemFunction(L, "Physics", "raycast", &PhysicsSceneImpl::LUA_raycast);

	#undef REGISTER_FUNCTION
}


} // !namespace Lumix
