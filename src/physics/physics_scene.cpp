#include "physics/physics_scene.h"
#include "cooking/PxCooking.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/matrix.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine.h"
#include "lua_script/lua_script_system.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "physics/physics_system.h"
#include "physics/physics_geometry_manager.h"
#include "universe/universe.h"
#include <PxPhysicsAPI.h>


namespace Lumix
{


static const uint32 BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32 MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32 CONTROLLER_HASH = crc32("physical_controller");
static const uint32 HEIGHTFIELD_HASH = crc32("physical_heightfield");


namespace LuaAPI
{


static float getActorSpeed(IScene* scene, ComponentIndex component)
{
	return static_cast<PhysicsScene*>(scene)->getActorSpeed(component);
}


static void moveController(IScene* scene,
	ComponentIndex component,
	float x,
	float y,
	float z,
	float time_delta)
{
	static_cast<PhysicsScene*>(scene)->moveController(component, Vec3(x, y, z), time_delta);
}


static ComponentIndex getActorComponent(IScene* scene, Entity entity)
{
	return static_cast<PhysicsScene*>(scene)->getActorComponent(entity);
}


static void putToSleep(IScene* scene, Entity entity)
{
	static_cast<PhysicsScene*>(scene)->putToSleep(entity);
}


static void applyForceToActor(IScene* scene, int component, float x, float y, float z)
{
	static_cast<PhysicsScene*>(scene)->applyForceToActor(component, Vec3(x, y, z));
}


} // namespace LuaAPI


struct OutputStream : public physx::PxOutputStream
{
	OutputStream(IAllocator& allocator)
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
				Math::maxValue(size + (int)count, capacity + 4096);
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


class Terrain
{
public:
	Terrain();
	~Terrain();
	void heightmapLoaded(Resource::State, Resource::State new_state);

	struct PhysicsSceneImpl* m_scene;
	Entity m_entity;
	physx::PxRigidActor* m_actor;
	Texture* m_heightmap;
	float m_xz_scale;
	float m_y_scale;
};


struct PhysicsSceneImpl : public PhysicsScene
{
	struct ContactCallback : public physx::PxSimulationEventCallback
	{
		ContactCallback(PhysicsSceneImpl& scene)
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
				auto e1 = (Entity)pairHeader.actors[0]->userData;
				auto e2 = (Entity)pairHeader.actors[1]->userData;
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
		RigidActor(PhysicsSceneImpl& scene)
			: m_resource(nullptr)
			, m_physx_actor(nullptr)
			, m_scene(scene)
			, m_is_dynamic(false)
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

	private:
		void onStateChanged(Resource::State old_state,
							Resource::State new_state);

	private:
		physx::PxRigidActor* m_physx_actor;
		PhysicsGeometry* m_resource;
		Entity m_entity;
		PhysicsSceneImpl& m_scene;
		bool m_is_dynamic;
	};


	PhysicsSceneImpl(UniverseContext& context, IAllocator& allocator)
		: m_allocator(allocator)
		, m_controllers(m_allocator)
		, m_actors(m_allocator)
		, m_terrains(m_allocator)
		, m_dynamic_actors(m_allocator)
		, m_universe(*context.m_universe)
		, m_universe_context(context)
		, m_is_game_running(false)
		, m_contact_callback(*this)
		, m_queued_forces(m_allocator)
	{
		m_queued_forces.reserve(64);
		m_script_scene = nullptr;
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

			auto* call = m_script_scene->beginFunctionCall(cmp, "onContact");
			if (!call) return;

			call->add(e2);
			call->add(position.x);
			call->add(position.y);
			call->add(position.z);
			m_script_scene->endFunctionCall(*call);
		};

		send(e1, e2, position);
		send(e2, e1, position);
	}


	void sendMessage(uint32 type, void*) override
	{
		static const uint32 register_hash = crc32("registerLuaAPI");
		if (type == register_hash)
		{
			registerLuaAPI();
		}
	}


	void enableVisualization()
	{
		m_scene->setVisualizationParameter(
			physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
		m_scene->setVisualizationParameter(
			physx::PxVisualizationParameter::eSCALE, 1.0);
		m_scene->setVisualizationParameter(
			physx::PxVisualizationParameter::eACTOR_AXES, 1.0f);
		m_scene->setVisualizationParameter(
			physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
		m_scene->setVisualizationParameter(
			physx::PxVisualizationParameter::eWORLD_AXES, 1.0f);
		m_scene->setVisualizationParameter(
			physx::PxVisualizationParameter::eCONTACT_POINT, 1.0f);
	}


	Universe& getUniverse() override { return m_universe; }


	bool ownComponentType(uint32 type) const override
	{
		return type == BOX_ACTOR_HASH || type == MESH_ACTOR_HASH ||
			   type == HEIGHTFIELD_HASH || type == CONTROLLER_HASH;
	}


	IPlugin& getPlugin() const override { return *m_system; }


	ComponentIndex createComponent(uint32 component_type, Entity entity) override
	{
		if (component_type == HEIGHTFIELD_HASH)
		{
			return createHeightfield(entity);
		}
		else if (component_type == CONTROLLER_HASH)
		{
			return createController(entity);
		}
		else if (component_type == BOX_ACTOR_HASH)
		{
			return createBoxRigidActor(entity);
		}
		else if (component_type == MESH_ACTOR_HASH)
		{
			return createMeshRigidActor(entity);
		}
		return INVALID_COMPONENT;
	}


	void destroyComponent(ComponentIndex cmp, uint32 type) override
	{
		if (type == HEIGHTFIELD_HASH)
		{
			Entity entity = m_terrains[cmp]->m_entity;
			LUMIX_DELETE(m_allocator, m_terrains[cmp]);
			m_terrains[cmp] = nullptr;
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else if (type == CONTROLLER_HASH)
		{
			Entity entity = m_controllers[cmp].m_entity;
			m_controllers[cmp].m_is_free = true;
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else if (type == MESH_ACTOR_HASH || type == BOX_ACTOR_HASH)
		{
			Entity entity = m_actors[cmp]->getEntity();
			m_actors[cmp]->setEntity(INVALID_ENTITY);
			m_actors[cmp]->setPhysxActor(nullptr);
			m_dynamic_actors.eraseItem(m_actors[cmp]);
			m_universe.destroyComponent(entity, type, this, cmp);
		}
		else
		{
			ASSERT(false);
		}
	}


	ComponentIndex createHeightfield(Entity entity)
	{
		Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)();
		m_terrains.push(terrain);
		terrain->m_heightmap = nullptr;
		terrain->m_scene = this;
		terrain->m_actor = nullptr;
		terrain->m_entity = entity;
		m_universe.addComponent(
			entity, HEIGHTFIELD_HASH, this, m_terrains.size() - 1);
		return m_terrains.size() - 1;
	}


	ComponentIndex createController(Entity entity)
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
		PhysicsSceneImpl::Controller& c = m_controllers.pushEmpty();
		c.m_controller =
			m_controller_manager->createController(cDesc);
		c.m_entity = entity;
		c.m_is_free = false;
		c.m_frame_change.set(0, 0, 0);
		c.m_radius = cDesc.radius;
		c.m_height = cDesc.height;

		m_universe.addComponent(entity, CONTROLLER_HASH, this, m_controllers.size() - 1);
		return m_controllers.size() - 1;
	}


	ComponentIndex createBoxRigidActor(Entity entity)
	{
		RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this);
		m_actors.push(actor);
		actor->setEntity(entity);

		physx::PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		physx::PxTransform transform;
		Matrix mtx = m_universe.getMatrix(entity);
		matrix2Transform(mtx, transform);

		physx::PxRigidStatic* physx_actor =
			PxCreateStatic(*m_system->getPhysics(), transform, geom, *m_default_material);
		actor->setPhysxActor(physx_actor);

		m_universe.addComponent(entity, BOX_ACTOR_HASH, this, m_actors.size() - 1);
		return m_actors.size() - 1;
	}


	ComponentIndex createMeshRigidActor(Entity entity)
	{
		RigidActor* actor = LUMIX_NEW(m_allocator, RigidActor)(*this);
		m_actors.push(actor);
		actor->setEntity(entity);

		m_universe.addComponent(
			entity, MESH_ACTOR_HASH, this, m_actors.size() - 1);
		return m_actors.size() - 1;
	}


	const char* getHeightmap(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->m_heightmap
				   ? m_terrains[cmp]->m_heightmap->getPath().c_str()
				   : "";
	}


	float getHeightmapXZScale(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->m_xz_scale;
	}


	void setHeightmapXZScale(ComponentIndex cmp, float scale) override
	{
		if (scale != m_terrains[cmp]->m_xz_scale)
		{
			m_terrains[cmp]->m_xz_scale = scale;
			if (m_terrains[cmp]->m_heightmap &&
				m_terrains[cmp]->m_heightmap->isReady())
			{
				heightmapLoaded(m_terrains[cmp]);
			}
		}
	}


	float getHeightmapYScale(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->m_y_scale;
	}


	void setHeightmapYScale(ComponentIndex cmp, float scale) override
	{
		if (scale != m_terrains[cmp]->m_y_scale)
		{
			m_terrains[cmp]->m_y_scale = scale;
			if (m_terrains[cmp]->m_heightmap && m_terrains[cmp]->m_heightmap->isReady())
			{
				heightmapLoaded(m_terrains[cmp]);
			}
		}
	}


	void setHeightmap(ComponentIndex cmp, const char* str) override
	{
		auto& resource_manager = m_engine->getResourceManager();
		if (m_terrains[cmp]->m_heightmap)
		{
			resource_manager.get(ResourceManager::TEXTURE)->unload(*m_terrains[cmp]->m_heightmap);
			m_terrains[cmp]
				->m_heightmap->getObserverCb()
				.unbind<Terrain, &Terrain::heightmapLoaded>(m_terrains[cmp]);
		}
		auto* texture_manager = resource_manager.get(ResourceManager::TEXTURE);
		m_terrains[cmp]->m_heightmap = static_cast<Texture*>(texture_manager->load(Path(str)));
		m_terrains[cmp]->m_heightmap->onLoaded<Terrain, &Terrain::heightmapLoaded>(m_terrains[cmp]);
		m_terrains[cmp]->m_heightmap->addDataReference();
	}


	const char* getShapeSource(ComponentIndex cmp) override
	{
		return m_actors[cmp]->getResource() ? m_actors[cmp]->getResource()->getPath().c_str() : "";
	}


	void setShapeSource(ComponentIndex cmp, const char* str) override
	{
		ASSERT(m_actors[cmp]);
		bool is_dynamic = isDynamic(cmp);
		if (m_actors[cmp]->getResource() &&
			m_actors[cmp]->getResource()->getPath() == str &&
			(!m_actors[cmp]->getPhysxActor() ||
			 is_dynamic == !m_actors[cmp]->getPhysxActor()->isRigidStatic()))
		{
			return;
		}

		ResourceManagerBase* manager = m_engine->getResourceManager().get(ResourceManager::PHYSICS);
		PhysicsGeometry* geom_res = static_cast<PhysicsGeometry*>(manager->load(Lumix::Path(str)));

		m_actors[cmp]->setPhysxActor(nullptr);
		m_actors[cmp]->setResource(geom_res);
	}


	void setControllerPosition(int index, const Vec3& pos)
	{
		physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
		m_controllers[index].m_controller->setPosition(p);
	}


	static Vec3 toVec3(const physx::PxVec3& v) { return Vec3(v.x, v.y, v.z); }


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
			auto* actor = m_actors[i.cmp];
			if (!actor->isDynamic())
			{
				g_log_warning.log("physics") << "Trying to apply force to static object";
				return;
			}

			auto* physx_actor = static_cast<physx::PxRigidDynamic*>(actor->getPhysxActor());
			if (!physx_actor) return;
			physx::PxVec3 f(i.force.x, i.force.y, i.force.z);
			physx_actor->addForce(f);
		}
		m_queued_forces.clear();
	}


	void update(float time_delta) override
	{
		if (!m_is_game_running) return;
		
		applyQueuedForces();

		time_delta = Math::minValue(0.01f, time_delta);
		simulateScene(time_delta);
		fetchResults();
		updateDynamicActors();
		updateControllers(time_delta);
	}


	ComponentIndex getActorComponent(Entity entity) override
	{
		for (int i = 0; i < m_actors.size(); ++i)
		{
			if (m_actors[i]->getEntity() == entity) return i;
		}
		return -1;
	}


	void registerLuaAPI()
	{
		auto* scene = m_universe_context.getScene(crc32("lua_script"));
		if (!scene) return;

		m_script_scene = static_cast<LuaScriptScene*>(scene);
		m_script_scene->registerFunction("Physics",
			"moveController",
			LuaWrapper::wrap<decltype(&LuaAPI::moveController), LuaAPI::moveController>);
		m_script_scene->registerFunction("Physics",
			"applyForceToActor",
			LuaWrapper::wrap<decltype(&LuaAPI::applyForceToActor), LuaAPI::applyForceToActor>);
		m_script_scene->registerFunction("Physics",
			"getActorComponent",
			LuaWrapper::wrap<decltype(&LuaAPI::getActorComponent), LuaAPI::getActorComponent>);
		m_script_scene->registerFunction("Physics",
			"putToSleep",
			LuaWrapper::wrap<decltype(&LuaAPI::putToSleep), LuaAPI::putToSleep>);
		m_script_scene->registerFunction("Physics",
			"getActorSpeed",
			LuaWrapper::wrap<decltype(&LuaAPI::getActorSpeed), LuaAPI::getActorSpeed>);
	}


	void startGame() override { m_is_game_running = true; }


	void stopGame() override { m_is_game_running = false; }


	float getControllerRadius(ComponentIndex cmp) override
	{
		return m_controllers[cmp].m_radius;
	}


	float getControllerHeight(ComponentIndex cmp) override
	{
		return m_controllers[cmp].m_height;
	}


	ComponentIndex getController(Entity entity) override
	{
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			if (m_controllers[i].m_entity == entity)
			{
				return i;
			}
		}
		return INVALID_COMPONENT;
	}


	void moveController(ComponentIndex cmp, const Vec3& v, float) override
	{
		m_controllers[cmp].m_frame_change += v;
	}


	bool raycast(const Vec3& origin,
		const Vec3& dir,
		float distance,
		RaycastHit& result) override
	{
		physx::PxVec3 physx_origin(origin.x, origin.y, origin.z);
		physx::PxVec3 unit_dir(dir.x, dir.y, dir.z);
		physx::PxReal max_distance = distance;
		physx::PxRaycastHit hit;

		const physx::PxSceneQueryFlags outputFlags = physx::PxSceneQueryFlag::eDISTANCE |
													 physx::PxSceneQueryFlag::eIMPACT |
													 physx::PxSceneQueryFlag::eNORMAL;

		bool status =
			m_scene->raycastSingle(physx_origin, unit_dir, max_distance, outputFlags, hit);
		result.normal.x = hit.normal.x;
		result.normal.y = hit.normal.y;
		result.normal.z = hit.normal.z;
		result.position.x = hit.position.x;
		result.position.y = hit.position.y;
		result.position.z = hit.position.z;
		result.entity = -1;
		if (hit.shape)
		{
			physx::PxRigidActor* actor = hit.shape->getActor();
			if (actor && actor->userData) result.entity = (int)actor->userData;
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
				m_dynamic_actors[i]->getPhysxActor()->setGlobalPose(trans,
																	false);
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


	void heightmapLoaded(Terrain* terrain)
	{
		PROFILE_FUNCTION();
		Array<physx::PxHeightFieldSample> heights(m_allocator);

		int width = terrain->m_heightmap->getWidth();
		int height = terrain->m_heightmap->getHeight();
		heights.resize(width * height);
		int bytes_per_pixel = terrain->m_heightmap->getBytesPerPixel();
		if (bytes_per_pixel == 2)
		{
			PROFILE_BLOCK("copyData");
			const uint16* LUMIX_RESTRICT data =
				(const uint16*)terrain->m_heightmap->getData();
			for (int j = 0; j < height; ++j)
			{
				int idx = j * width;
				for (int i = 0; i < width; ++i)
				{
					int idx2 = j + i * height;
					heights[idx].height = data[idx2];
					heights[idx].materialIndex0 = heights[idx].materialIndex1 =
						0;
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
					heights[idx].height = data[idx2 * bytes_per_pixel];
					heights[idx].materialIndex0 = heights[idx].materialIndex1 =
						0;
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

			physx::PxHeightField* heightfield =
				m_system->getPhysics()->createHeightField(hfDesc);
			float height_scale =
				bytes_per_pixel == 2 ? 1 / (256 * 256.0f - 1) : 1 / 255.0f;
			physx::PxHeightFieldGeometry hfGeom(heightfield,
												physx::PxMeshGeometryFlags(),
												height_scale *
													terrain->m_y_scale,
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
			Matrix mtx = m_universe.getMatrix(terrain->m_entity);
			matrix2Transform(mtx, transform);

			physx::PxRigidActor* actor;
			actor = PxCreateStatic(*m_system->getPhysics(),
								   transform,
								   hfGeom,
								   *m_default_material);
			if (actor)
			{
				actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION,
									width <= 1024);
				actor->userData = (void*)terrain->m_entity;
				m_scene->addActor(*actor);
				terrain->m_actor = actor;
			}
			else
			{
				g_log_error.log("PhysX")
					<< "Could not create PhysX heightfield "
					<< terrain->m_heightmap->getPath().c_str();
			}
		}
	}


	bool isDynamic(ComponentIndex cmp) override
	{
		RigidActor* actor = m_actors[cmp];
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


	Vec3 getHalfExtents(ComponentIndex cmp) override
	{
		Vec3 size;
		physx::PxRigidActor* actor = m_actors[cmp]->getPhysxActor();
		physx::PxShape* shapes;
		if (actor->getNbShapes() == 1 &&
			m_actors[cmp]->getPhysxActor()->getShapes(&shapes, 1))
		{
			physx::PxVec3& half = shapes->getGeometry().box().halfExtents;
			size.x = half.x;
			size.y = half.y;
			size.z = half.z;
		}
		return size;
	}


	void setHalfExtents(ComponentIndex cmp, const Vec3& size) override
	{
		physx::PxRigidActor* actor = m_actors[cmp]->getPhysxActor();
		physx::PxShape* shapes;
		if (actor->getNbShapes() == 1 &&
			m_actors[cmp]->getPhysxActor()->getShapes(&shapes, 1))
		{
			physx::PxBoxGeometry box;
			bool is_box = shapes->getBoxGeometry(box);
			ASSERT(is_box);
			physx::PxVec3& half = box.halfExtents;
			half.x = Math::maxValue(0.01f, size.x);
			half.y = Math::maxValue(0.01f, size.y);
			half.z = Math::maxValue(0.01f, size.z);
			shapes->setGeometry(box);
		}
	}


	void setIsDynamic(ComponentIndex cmp, bool new_value) override
	{
		RigidActor* actor = m_actors[cmp];
		int dynamic_index = m_dynamic_actors.indexOf(actor);
		bool is_dynamic = dynamic_index != -1;
		if (is_dynamic != new_value)
		{
			m_actors[cmp]->setDynamic(new_value);
			if (new_value)
			{
				m_dynamic_actors.push(actor);
			}
			else
			{
				m_dynamic_actors.eraseItemFast(actor);
			}
			physx::PxShape* shapes;
			if (m_actors[cmp]->getPhysxActor()->getNbShapes() == 1 &&
				m_actors[cmp]->getPhysxActor()->getShapes(&shapes, 1, 0))
			{
				physx::PxGeometryHolder geom = shapes->getGeometry();

				physx::PxTransform transform;
				matrix2Transform(
					m_universe.getMatrix(m_actors[cmp]->getEntity()),
					transform);

				physx::PxRigidActor* actor;
				if (new_value)
				{
					actor = PxCreateDynamic(*m_system->getPhysics(),
											transform,
											geom.any(),
											*m_default_material,
											1.0f);
				}
				else
				{
					actor = PxCreateStatic(*m_system->getPhysics(),
										   transform,
										   geom.any(),
										   *m_default_material);
				}
				ASSERT(actor);
				actor->userData = (void*)m_actors[cmp]->getEntity();
				actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
				m_actors[cmp]->setPhysxActor(actor);
			}
		}
	}


	void serializeActor(OutputBlob& serializer, int idx)
	{
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


	void deserializeActor(InputBlob& serializer, int idx)
	{
		ActorType type;
		serializer.read((int32&)type);

		ResourceManagerBase* manager =
			m_engine->getResourceManager().get(ResourceManager::PHYSICS);

		switch (type)
		{
			case BOX:
			{
				physx::PxBoxGeometry box_geom;
				physx::PxTransform transform;
				Matrix mtx = m_universe.getMatrix(m_actors[idx]->getEntity());
				matrix2Transform(mtx, transform);
				serializer.read(box_geom.halfExtents.x);
				serializer.read(box_geom.halfExtents.y);
				serializer.read(box_geom.halfExtents.z);
				physx::PxRigidActor* actor;
				if (isDynamic(idx))
				{
					actor = PxCreateDynamic(*m_system->getPhysics(),
											transform,
											box_geom,
											*m_default_material,
											1.0f);
				}
				else
				{
					actor = PxCreateStatic(*m_system->getPhysics(),
										   transform,
										   box_geom,
										   *m_default_material);
				}
				m_actors[idx]->setPhysxActor(actor);
				m_universe.addComponent(
					m_actors[idx]->getEntity(), BOX_ACTOR_HASH, this, idx);
			}
			break;
			case TRIMESH:
			case CONVEX:
			{
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, sizeof(tmp));
				m_actors[idx]->setResource(static_cast<PhysicsGeometry*>(
					manager->load(Lumix::Path(tmp))));
				m_universe.addComponent(
					m_actors[idx]->getEntity(), MESH_ACTOR_HASH, this, idx);
			}
			break;
			default:
				ASSERT(false);
				break;
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32)m_actors.size());
		for (int i = 0; i < m_actors.size(); ++i)
		{
			serializer.write(isDynamic(i));
			serializer.write(m_actors[i]->getEntity());
			if (m_actors[i]->getEntity() != -1)
			{
				serializeActor(serializer, i);
			}
		}
		serializer.write((int32)m_controllers.size());
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			serializer.write(m_controllers[i].m_entity);
			serializer.write(m_controllers[i].m_is_free);
		}
		serializer.write((int32)m_terrains.size());
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i])
			{
				serializer.write(true);
				serializer.write(m_terrains[i]->m_entity);
				serializer.writeString(
					m_terrains[i]->m_heightmap
						? m_terrains[i]->m_heightmap->getPath().c_str()
						: "");
				serializer.write(m_terrains[i]->m_xz_scale);
				serializer.write(m_terrains[i]->m_y_scale);
			}
			else
			{
				serializer.write(false);
			}
		}
	}


	void deserializeActors(InputBlob& serializer)
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

			if (m_actors[i]->getEntity() != -1)
			{
				deserializeActor(serializer, i);
			}
		}
	}


	void deserializeControllers(InputBlob& serializer)
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
			int32 index;
			bool is_free;
			serializer.read(index);
			serializer.read(is_free);
			Entity e(index);

			Controller& c = m_controllers.pushEmpty();
			c.m_is_free = is_free;
			c.m_frame_change.set(0, 0, 0);

			if (!is_free)
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
				Vec3 position = m_universe.getPosition(e);
				cDesc.position.set(position.x, position.y - cDesc.height * 0.5f, position.z);
				c.m_controller =
					m_controller_manager->createController(*m_system->getPhysics(), m_scene, cDesc);
				c.m_entity = e;
				m_universe.addComponent(e, CONTROLLER_HASH, this, i);
			}
		}
	}


	void deserializeTerrains(InputBlob& serializer)
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
					m_terrains[i] = LUMIX_NEW(m_allocator, Terrain);
				}
				m_terrains[i]->m_scene = this;
				serializer.read(m_terrains[i]->m_entity);
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				serializer.read(m_terrains[i]->m_xz_scale);
				serializer.read(m_terrains[i]->m_y_scale);

				if (m_terrains[i]->m_heightmap == nullptr ||
					compareString(tmp, m_terrains[i]->m_heightmap->getPath().c_str()) != 0)
				{
					setHeightmap(i, tmp);
				}
				m_universe.addComponent(m_terrains[i]->m_entity, HEIGHTFIELD_HASH, this, i);
			}
		}
	}


	void deserialize(InputBlob& serializer, int) override
	{
		deserializeActors(serializer);
		deserializeControllers(serializer);
		deserializeTerrains(serializer);
	}


	PhysicsSystem& getSystem() const override { return *m_system; }


	float getActorSpeed(ComponentIndex cmp) override
	{
		auto* actor = m_actors[cmp];
		if (!actor->isDynamic())
		{
			g_log_warning.log("physics") << "Trying to get speed of static object";
			return 0;
		}

		auto* physx_actor = static_cast<physx::PxRigidDynamic*>(actor->getPhysxActor());
		if (!physx_actor) return 0;
		return physx_actor->getLinearVelocity().magnitude();
	}


	void putToSleep(ComponentIndex cmp) override
	{
		auto* actor = m_actors[cmp];
		if (!actor->isDynamic())
		{
			g_log_warning.log("physics") << "Trying to put static object to sleep";
			return;
		}

		auto* physx_actor = static_cast<physx::PxRigidDynamic*>(actor->getPhysxActor());
		if (!physx_actor) return;
		physx_actor->putToSleep();
	}


	void applyForceToActor(ComponentIndex cmp, const Vec3& force) override
	{
		auto& i = m_queued_forces.pushEmpty();
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

		pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;

		//if ((filterData0.word0 & filterData1.word1) && (filterData1.word0 & filterData0.word1))
		pairFlags |= physx::PxPairFlag::eNOTIFY_TOUCH_FOUND;
		return physx::PxFilterFlag::eDEFAULT;
	}


	struct QueuedForce
	{
		ComponentIndex cmp;
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
	};

	IAllocator& m_allocator;

	Universe& m_universe;
	UniverseContext& m_universe_context;
	Engine* m_engine;
	ContactCallback m_contact_callback;
	physx::PxScene* m_scene;
	LuaScriptScene* m_script_scene;
	PhysicsSystem* m_system;
	physx::PxControllerManager* m_controller_manager;
	physx::PxMaterial* m_default_material;
	Array<RigidActor*> m_actors;
	Array<RigidActor*> m_dynamic_actors;
	bool m_is_game_running;

	Array<QueuedForce> m_queued_forces;
	Array<Controller> m_controllers;
	Array<Terrain*> m_terrains;
};


PhysicsScene* PhysicsScene::create(PhysicsSystem& system,
	UniverseContext& context,
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
			g_log_error.log("physics") << "PxDefaultCpuDispatcherCreate failed!";
		}
		sceneDesc.cpuDispatcher = cpu_dispatcher;
	}
	if (!sceneDesc.filterShader)
	{
		sceneDesc.filterShader = &physx::PxDefaultSimulationFilterShader;
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
		Matrix mtx = m_scene.getUniverse().getMatrix(m_entity);
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
			g_log_error.log("PhysX") << "Could not create PhysX mesh "
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
		actor->userData = (void*)m_entity;
	}
}


void PhysicsSceneImpl::RigidActor::setResource(PhysicsGeometry* resource)
{
	if (m_resource)
	{
		m_resource->getObserverCb().unbind<RigidActor, &RigidActor::onStateChanged>(this);
		m_resource->getResourceManager().get(ResourceManager::PHYSICS)->unload(*m_resource);
	}
	m_resource = resource;
	if (resource)
	{
		m_resource->onLoaded<RigidActor, &RigidActor::onStateChanged>(this);
	}
}


Terrain::Terrain()
{
	m_heightmap = nullptr;
	m_xz_scale = 1.0f;
	m_y_scale = 1.0f;
	m_actor = nullptr;
}


Terrain::~Terrain()
{
	if (m_heightmap)
	{
		m_heightmap->getResourceManager()
			.get(ResourceManager::TEXTURE)
			->unload(*m_heightmap);
		m_heightmap->getObserverCb().unbind<Terrain, &Terrain::heightmapLoaded>(
			this);
	}
}


void Terrain::heightmapLoaded(Resource::State, Resource::State new_state)
{
	if (new_state == Resource::State::READY)
	{
		m_scene->heightmapLoaded(this);
	}
}


} // !namespace Lumix
