#include "physics/physics_scene.h"
#include "cooking/PxCooking.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "physics/physics_system.h"
#include "physics/physics_geometry_manager.h"
#include "universe/universe.h"
#include <PxPhysicsAPI.h>


namespace Lumix
{


static const uint32_t BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32_t MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32_t CONTROLLER_HASH = crc32("physical_controller");
static const uint32_t HEIGHTFIELD_HASH = crc32("physical_heightfield");


struct OutputStream : public physx::PxOutputStream
{
	OutputStream(IAllocator& allocator)
		: allocator(allocator)
	{
		data = (uint8_t*)allocator.allocate(sizeof(uint8_t) * 4096);
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
			uint8_t* new_data =
				(uint8_t*)allocator.allocate(sizeof(uint8_t) * new_capacity);
			memcpy(new_data, data, size);
			allocator.deallocate(data);
			data = new_data;
			capacity = new_capacity;
		}
		memcpy(data + size, src, count);
		size += count;
		return count;
	}

	uint8_t* data;
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
	enum ActorType
	{
		BOX,
		TRIMESH,
		CONVEX
	};


	class RigidActor
	{
	public:
		RigidActor(PhysicsSceneImpl& scene, IAllocator& allocator)
			: m_resource(nullptr)
			, m_physx_actor(nullptr)
			, m_scene(scene)
		{
		}

		void setResource(PhysicsGeometry* resource);
		void setEntity(Entity entity) { m_entity = entity; }
		Entity getEntity() { return m_entity; }
		void setPhysxActor(physx::PxRigidActor* actor);
		physx::PxRigidActor* getPhysxActor() const { return m_physx_actor; }
		PhysicsGeometry* getResource() const { return m_resource; }

	private:
		void onStateChanged(Resource::State old_state,
							Resource::State new_state);

	private:
		physx::PxRigidActor* m_physx_actor;
		PhysicsGeometry* m_resource;
		Entity m_entity;
		PhysicsSceneImpl& m_scene;
	};


	PhysicsSceneImpl(Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_controllers(m_allocator)
		, m_actors(m_allocator)
		, m_terrains(m_allocator)
		, m_dynamic_actors(m_allocator)
		, m_universe(universe)
		, m_is_game_running(false)
	{
	}


	~PhysicsSceneImpl()
	{
		for (int i = 0; i < m_actors.size(); ++i)
		{
			m_allocator.deleteObject(m_actors[i]);
		}
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			m_allocator.deleteObject(m_terrains[i]);
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


	virtual Universe& getUniverse() override { return m_universe; }


	virtual bool ownComponentType(uint32_t type) const override
	{
		return type == BOX_ACTOR_HASH || type == MESH_ACTOR_HASH ||
			   type == HEIGHTFIELD_HASH || type == CONTROLLER_HASH;
	}


	virtual IPlugin& getPlugin() const override { return *m_system; }


	virtual ComponentIndex createComponent(uint32_t component_type,
										   Entity entity) override
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


	virtual void destroyComponent(ComponentIndex cmp, uint32_t type) override
	{
		if (type == HEIGHTFIELD_HASH)
		{
			Entity entity = m_terrains[cmp]->m_entity;
			m_allocator.deleteObject(m_terrains[cmp]);
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
		Terrain* terrain = m_allocator.newObject<Terrain>();
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
		c.m_controller = m_system->getControllerManager()->createController(
			*m_system->getPhysics(), m_scene, cDesc);
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
		RigidActor* actor =
			m_allocator.newObject<RigidActor>(*this, m_allocator);
		m_actors.push(actor);
		actor->setEntity(entity);

		physx::PxBoxGeometry geom;
		geom.halfExtents.x = 1;
		geom.halfExtents.y = 1;
		geom.halfExtents.z = 1;
		physx::PxTransform transform;
		Matrix mtx = m_universe.getMatrix(entity);
		matrix2Transform(mtx, transform);

		physx::PxRigidStatic* physx_actor = PxCreateStatic(
			*m_system->getPhysics(), transform, geom, *m_default_material);
		physx_actor->userData = (void*)entity;
		m_scene->addActor(*physx_actor);
		actor->setPhysxActor(physx_actor);
		physx_actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);

		m_universe.addComponent(
			entity, BOX_ACTOR_HASH, this, m_actors.size() - 1);
		return m_actors.size() - 1;
	}


	ComponentIndex createMeshRigidActor(Entity entity)
	{
		RigidActor* actor =
			m_allocator.newObject<RigidActor>(*this, m_allocator);
		m_actors.push(actor);
		actor->setEntity(entity);

		m_universe.addComponent(
			entity, MESH_ACTOR_HASH, this, m_actors.size() - 1);
		return m_actors.size() - 1;
	}


	virtual const char* getHeightmap(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->m_heightmap
				   ? m_terrains[cmp]->m_heightmap->getPath().c_str()
				   : "";
	}


	virtual float getHeightmapXZScale(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->m_xz_scale;
	}


	virtual void setHeightmapXZScale(ComponentIndex cmp, float scale) override
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


	virtual float getHeightmapYScale(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->m_y_scale;
	}


	virtual void setHeightmapYScale(ComponentIndex cmp, float scale) override
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


	virtual void setHeightmap(ComponentIndex cmp, const char* str) override
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


	virtual const char* getShapeSource(ComponentIndex cmp) override
	{
		return m_actors[cmp]->getResource() ? m_actors[cmp]->getResource()->getPath().c_str() : "";
	}


	virtual void setShapeSource(ComponentIndex cmp, const char* str) override
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

		ResourceManagerBase* manager =
			m_engine->getResourceManager().get(ResourceManager::PHYSICS);
		PhysicsGeometry* geom_res = static_cast<PhysicsGeometry*>(
			manager->load(Lumix::Path(str)));

		m_actors[cmp]->setResource(geom_res);

		if (m_actors[cmp]->getPhysxActor())
		{
			m_scene->removeActor(*m_actors[cmp]->getPhysxActor());
			m_actors[cmp]->setPhysxActor(nullptr);
		}
	}


	void setControllerPosition(int index, const Vec3& pos)
	{
		physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
		m_controllers[index].m_controller->setPosition(p);
	}


	static Vec3 toVec3(const physx::PxVec3& v) { return Vec3(v.x, v.y, v.z); }


	virtual void render(RenderScene& render_scene) override
	{
		m_scene->getNbActors(physx::PxActorTypeSelectionFlag::eRIGID_STATIC);
		const physx::PxRenderBuffer& rb = m_scene->getRenderBuffer();
		const physx::PxU32 num_lines = rb.getNbLines();
		const physx::PxU32 num_points = rb.getNbPoints();
		const physx::PxU32 num_tri = rb.getNbTriangles();
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


	virtual void update(float time_delta) override
	{
		if (!m_is_game_running) return;
		
		time_delta = Math::minValue(0.01f, time_delta);
		simulateScene(time_delta);
		fetchResults();
		updateDynamicActors();
		updateControllers(time_delta);
	}


	virtual void startGame() override
	{
		m_is_game_running = true;
	}


	virtual void stopGame() override
	{
		m_is_game_running = false;
	}
	

	virtual float getControllerRadius(ComponentIndex cmp) override
	{
		return m_controllers[cmp].m_radius;
	}


	virtual float getControllerHeight(ComponentIndex cmp) override
	{
		return m_controllers[cmp].m_height;
	}


	virtual ComponentIndex getController(Entity entity) override
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


	virtual void
	moveController(ComponentIndex cmp, const Vec3& v, float dt) override
	{
		m_controllers[cmp].m_frame_change += v;
	}


	virtual bool raycast(const Vec3& origin,
						 const Vec3& dir,
						 float distance,
						 RaycastHit& result) override
	{
		physx::PxVec3 physx_origin(origin.x, origin.y, origin.z);
		physx::PxVec3 unit_dir(dir.x, dir.y, dir.z);
		physx::PxReal max_distance = distance;
		physx::PxRaycastHit hit;

		const physx::PxSceneQueryFlags outputFlags =
			physx::PxSceneQueryFlag::eDISTANCE |
			physx::PxSceneQueryFlag::eIMPACT | physx::PxSceneQueryFlag::eNORMAL;

		bool status = m_scene->raycastSingle(
			physx_origin, unit_dir, max_distance, outputFlags, hit);
		result.normal.x = hit.normal.x;
		result.normal.y = hit.normal.y;
		result.normal.z = hit.normal.z;
		result.position.x = hit.impact.x;
		result.position.y = hit.impact.y;
		result.position.z = hit.impact.z;
		result.entity = -1;
		if (hit.shape)
		{
			physx::PxRigidActor& actor = hit.shape->getActor();
			if (actor.userData)
				result.entity = (int)actor.userData;
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
			const uint16_t* LUMIX_RESTRICT data =
				(const uint16_t*)terrain->m_heightmap->getData();
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
			const uint8_t* data = terrain->m_heightmap->getData();
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


	virtual bool isDynamic(ComponentIndex cmp) override
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


	virtual Vec3 getHalfExtents(ComponentIndex cmp) override
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


	virtual void setHalfExtents(ComponentIndex cmp, const Vec3& size) override
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
			half.x = size.x;
			half.y = size.y;
			half.z = size.z;
			shapes->setGeometry(box);
		}
	}


	virtual void setIsDynamic(ComponentIndex cmp, bool new_value) override
	{
		int dynamic_index = -1;
		RigidActor* actor = m_actors[cmp];
		for (int i = 0, c = m_dynamic_actors.size(); i < c; ++i)
		{
			if (m_dynamic_actors[i] == actor)
			{
				dynamic_index = i;
				break;
			}
		}
		bool is_dynamic = dynamic_index != -1;
		if (is_dynamic != new_value)
		{
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
				serializer.write((int32_t)BOX);
				serializer.write(geom.halfExtents.x);
				serializer.write(geom.halfExtents.y);
				serializer.write(geom.halfExtents.z);
			}
			else if (shapes->getConvexMeshGeometry(convex_geom))
			{
				serializer.write((int32_t)CONVEX);
				serializer.writeString(
					m_actors[idx]->getResource()
						? m_actors[idx]->getResource()->getPath().c_str()
						: "");
			}
			else if (shapes->getTriangleMeshGeometry(trimesh_geom))
			{
				serializer.write((int32_t)TRIMESH);
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
		serializer.read((int32_t&)type);

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
				actor->userData = (void*)m_actors[idx]->getEntity();
				m_scene->addActor(*actor);
				m_actors[idx]->setPhysxActor(actor);
				actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
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


	virtual void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32_t)m_actors.size());
		for (int i = 0; i < m_actors.size(); ++i)
		{
			serializer.write(isDynamic(i));
			serializer.write(m_actors[i]->getEntity());
			if (m_actors[i]->getEntity() != -1)
			{
				serializeActor(serializer, i);
			}
		}
		serializer.write((int32_t)m_controllers.size());
		for (int i = 0; i < m_controllers.size(); ++i)
		{
			serializer.write(m_controllers[i].m_entity);
			serializer.write(m_controllers[i].m_is_free);
		}
		serializer.write((int32_t)m_terrains.size());
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
		int32_t count;
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
			RigidActor* actor =
				m_allocator.newObject<RigidActor>(*this, m_allocator);
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
		int32_t count;
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
			int32_t index;
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
					m_system->getControllerManager()->createController(
						*m_system->getPhysics(), m_scene, cDesc);
				c.m_entity = e;
				m_universe.addComponent(e, CONTROLLER_HASH, this, i);
			}
		}
	}


	void deserializeTerrains(InputBlob& serializer)
	{
		int32_t count;
		serializer.read(count);
		for (int i = count; i < m_terrains.size(); ++i)
		{
			m_allocator.deleteObject(m_terrains[i]);
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
					m_terrains[i] = m_allocator.newObject<Terrain>();
				}
				m_terrains[i]->m_scene = this;
				serializer.read(m_terrains[i]->m_entity);
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				serializer.read(m_terrains[i]->m_xz_scale);
				serializer.read(m_terrains[i]->m_y_scale);

				if (m_terrains[i]->m_heightmap == nullptr ||
					strcmp(tmp,
						   m_terrains[i]->m_heightmap->getPath().c_str()) != 0)
				{
					setHeightmap(i, tmp);
				}
				m_universe.addComponent(
					m_terrains[i]->m_entity, HEIGHTFIELD_HASH, this, i);
			}
		}
	}


	virtual void deserialize(InputBlob& serializer) override
	{
		deserializeActors(serializer);
		deserializeControllers(serializer);
		deserializeTerrains(serializer);
	}


	virtual PhysicsSystem& getSystem() const override { return *m_system; }


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
	Engine* m_engine;
	physx::PxScene* m_scene;
	PhysicsSystem* m_system;
	physx::PxMaterial* m_default_material;
	Array<RigidActor*> m_actors;
	Array<RigidActor*> m_dynamic_actors;
	bool m_is_game_running;

	Array<Controller> m_controllers;
	Array<Terrain*> m_terrains;
};


PhysicsScene* PhysicsScene::create(PhysicsSystem& system,
								   Universe& universe,
								   Engine& engine,
								   IAllocator& allocator)
{
	PhysicsSceneImpl* impl =
		allocator.newObject<PhysicsSceneImpl>(universe, allocator);
	impl->m_universe.entityTransformed()
		.bind<PhysicsSceneImpl, &PhysicsSceneImpl::onEntityMoved>(impl);
	impl->m_engine = &engine;
	physx::PxSceneDesc sceneDesc(system.getPhysics()->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
	if (!sceneDesc.cpuDispatcher)
	{
		physx::PxDefaultCpuDispatcher* cpu_dispatcher =
			physx::PxDefaultCpuDispatcherCreate(1);
		if (!cpu_dispatcher)
		{
			g_log_error.log("physics")
				<< "PxDefaultCpuDispatcherCreate failed!";
		}
		sceneDesc.cpuDispatcher = cpu_dispatcher;
	}
	if (!sceneDesc.filterShader)
	{
		sceneDesc.filterShader = &physx::PxDefaultSimulationFilterShader;
	}

	impl->m_scene = system.getPhysics()->createScene(sceneDesc);
	if (!impl->m_scene)
	{
		allocator.deleteObject(impl);
		return nullptr;
	}

	impl->m_system = &system;
	impl->m_default_material =
		impl->m_system->getPhysics()->createMaterial(0.5, 0.5, 0.5);
	return impl;
}


void PhysicsScene::destroy(PhysicsScene* scene)
{
	PhysicsSceneImpl* impl = static_cast<PhysicsSceneImpl*>(scene);
	impl->m_default_material->release();
	impl->m_scene->release();
	impl->m_allocator.deleteObject(scene);
}


void PhysicsSceneImpl::RigidActor::onStateChanged(Resource::State old_state,
												  Resource::State new_state)
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
			actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
			actor->userData = (void*)m_entity;
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
