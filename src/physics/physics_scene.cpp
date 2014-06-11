#include "physics/physics_scene.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <gl/gl.h>
#include <PxPhysicsAPI.h>
#include "cooking/PxCooking.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/iserializer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"
#include "universe/component_event.h"
#include "universe/entity_moved_event.h"
#include "physics/physics_system.h"
#include "physics/physics_system_impl.h"


namespace Lux
{


struct PhysicsSceneImpl
{
	enum ActorType
	{
		BOX
	};

	void handleEvent(Event& event);
	void createConvexGeom(const char* path, physx::PxConvexMeshGeometry& geom);
	void createTriMesh(const char* path, physx::PxTriangleMeshGeometry& geom);

	void setControllerPosition(int index, const Vec3& pos);
	void serializeActor(ISerializer& serializer, int idx);
	void deserializeActor(ISerializer& serializer, int idx);

	struct Controller
	{
		physx::PxController* m_controller;
		Entity m_entity;
	};

	Universe*					m_universe;
	Engine*						m_engine;
	physx::PxScene*				m_scene;
	PhysicsSystem*				m_system;
	physx::PxMaterial*			m_default_material;
	Array<physx::PxRigidActor*>	m_actors;
	Array<string>				m_shape_sources;
	
	Array<bool>					m_is_dynamic;
	Array<Entity>				m_entities;
	Array<int>					m_index_map;
	Array<Controller>			m_controllers;
	PhysicsScene*				m_owner;
};


static const uint32_t BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32_t MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32_t CONTROLLER_HASH = crc32("physical_controller");


struct OutputStream : public physx::PxOutputStream
{
	OutputStream()
	{
		data = LUX_NEW_ARRAY(uint8_t, 4096);
		capacity = 4096;
		size = 0;
	}

	~OutputStream()
	{
		LUX_DELETE_ARRAY(data);
	}


	virtual physx::PxU32 write(const void* src, physx::PxU32 count)
	{
		if(size + (int)count > capacity)
		{
			int new_capacity = Math::max(size + (int)count, capacity + 4096);
			uint8_t* new_data = LUX_NEW_ARRAY(unsigned char, new_capacity);
			memcpy(new_data, data, size);
			LUX_DELETE_ARRAY(data);
			data = new_data;
			capacity = new_capacity;
		}
		memcpy(data + size, src, count);
		size += count;
		return count;
	}

	uint8_t* data;
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
		if(pos + (int)count <= size)
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


PhysicsScene::PhysicsScene()
{
	m_impl = 0;
}

	
bool PhysicsScene::create(PhysicsSystem& system, Universe& universe, Engine& engine)
{
	m_impl = LUX_NEW(PhysicsSceneImpl);
	m_impl->m_owner = this;
	m_impl->m_universe = &universe;
	m_impl->m_universe->getEventManager().addListener(EntityMovedEvent::type).bind<PhysicsSceneImpl, &PhysicsSceneImpl::handleEvent>(m_impl);
	m_impl->m_engine = &engine;
	physx::PxSceneDesc sceneDesc(system.m_impl->m_physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
	if(!sceneDesc.cpuDispatcher) {
		physx::PxDefaultCpuDispatcher* cpu_dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
		if(!cpu_dispatcher)
			printf("PxDefaultCpuDispatcherCreate failed!");
		sceneDesc.cpuDispatcher = cpu_dispatcher;
	} 
	if(!sceneDesc.filterShader)
		sceneDesc.filterShader  = &physx::PxDefaultSimulationFilterShader;

	m_impl->m_scene = system.m_impl->m_physics->createScene(sceneDesc);
	if (!m_impl->m_scene)
		return false;
	m_impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE,     1.0);
	m_impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
	m_impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eACTOR_AXES, 1.0f);
	m_impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
	m_impl->m_scene->setVisualizationParameter(physx::PxVisualizationParameter::eWORLD_AXES, 1.0f);
	m_impl->m_system = &system;
	m_impl->m_default_material = m_impl->m_system->m_impl->m_physics->createMaterial(0.5,0.5,0.5);
	return true;
}


void PhysicsScene::destroy()
{
	m_impl->m_default_material->release();
	m_impl->m_scene->release();
	LUX_DELETE(m_impl);
	m_impl = NULL;
}


void matrix2Transform(const Matrix& mtx, physx::PxTransform& transform)
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


void PhysicsScene::destroyActor(Component cmp)
{
	ASSERT(cmp.type == BOX_ACTOR_HASH);
	int inner_index = m_impl->m_index_map[cmp.index];
	m_impl->m_scene->removeActor(*m_impl->m_actors[inner_index]);
	m_impl->m_actors[inner_index]->release();
	m_impl->m_universe->getEventManager().emitEvent(ComponentEvent(cmp, false));
	m_impl->m_actors.eraseFast(inner_index);
	m_impl->m_shape_sources.eraseFast(inner_index);
	m_impl->m_is_dynamic.eraseFast(inner_index);
	m_impl->m_entities.eraseFast(inner_index);
	for(int i = 0; i < m_impl->m_index_map.size(); ++i)
	{
		if(m_impl->m_index_map[i] == m_impl->m_entities.size())
		{
			m_impl->m_index_map[i] = inner_index;
			break;
		}
	}
	m_impl->m_index_map[cmp.index] = -1;
	m_impl->m_universe->getEventManager().emitEvent(ComponentEvent(cmp, false));
}


Component PhysicsScene::createController(Entity entity)
{
	physx::PxCapsuleControllerDesc cDesc;
	cDesc.material			= m_impl->m_default_material;
	cDesc.height			= 1.8f;
	cDesc.radius			= 0.25f;
	cDesc.slopeLimit		= 0.0f;
	cDesc.contactOffset		= 0.1f;
	cDesc.stepOffset		= 0.02f;
	cDesc.callback			= NULL;
	cDesc.behaviorCallback	= NULL;
	Vec3 position = entity.getPosition();
	cDesc.position.set(position.x, position.y, position.z);
	PhysicsSceneImpl::Controller c;
	c.m_controller = m_impl->m_system->m_impl->m_controller_manager->createController(*m_impl->m_system->m_impl->m_physics, m_impl->m_scene, cDesc);
	c.m_entity = entity;

	m_impl->m_controllers.push(c);
	
	Component cmp(entity, CONTROLLER_HASH, this, m_impl->m_controllers.size() - 1);
	m_impl->m_universe->getEventManager().emitEvent(ComponentEvent(cmp));
	return cmp;
}


Component PhysicsScene::createBoxRigidActor(Entity entity)
{
	int new_index = m_impl->m_entities.size();
	for(int i = 0; i < m_impl->m_index_map.size(); ++i)
	{
		if(m_impl->m_index_map[i] == -1) 
		{
			new_index = i;
			break;
		}
	}
	if(new_index == m_impl->m_entities.size())
	{
		m_impl->m_actors.push(0);
		m_impl->m_shape_sources.push(string(""));
		m_impl->m_is_dynamic.push(false);
		m_impl->m_entities.push(entity);
	}
	else
	{
		m_impl->m_actors[new_index] = 0;
		m_impl->m_shape_sources[new_index] = "";
		m_impl->m_is_dynamic[new_index] = false;
		m_impl->m_entities[new_index] = entity;
	}

	physx::PxBoxGeometry geom;
	geom.halfExtents.x = 1;
	geom.halfExtents.y = 1;
	geom.halfExtents.z = 1;
	physx::PxTransform transform;
	Matrix mtx;
	entity.getMatrix(mtx);
	matrix2Transform(mtx, transform);

	physx::PxRigidStatic* actor = PxCreateStatic(*m_impl->m_system->m_impl->m_physics, transform, geom, *m_impl->m_default_material);
	actor->userData = (void*)entity.index;
	m_impl->m_scene->addActor(*actor);
	m_impl->m_scene->simulate(0.01f);
	m_impl->m_scene->fetchResults(true);
	m_impl->m_actors[new_index] = actor;
	actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);

	Component cmp(entity, BOX_ACTOR_HASH, this, m_impl->m_actors.size() - 1);
	m_impl->m_universe->getEventManager().emitEvent(ComponentEvent(cmp));
	return cmp;
}


Component PhysicsScene::createMeshRigidActor(Entity entity)
{
	int new_index = m_impl->m_entities.size();
	for (int i = 0; i < m_impl->m_index_map.size(); ++i)
	{
		if (m_impl->m_index_map[i] == -1)
		{
			new_index = i;
			break;
		}
	}
	if (new_index == m_impl->m_entities.size())
	{
		m_impl->m_actors.push(NULL);
		m_impl->m_shape_sources.push(string(""));
		m_impl->m_is_dynamic.push(false);
		m_impl->m_entities.push(entity);
	}
	else
	{
		m_impl->m_actors[new_index] = NULL;
		m_impl->m_shape_sources[new_index] = "";
		m_impl->m_is_dynamic[new_index] = false;
		m_impl->m_entities[new_index] = entity;
	}

/*	physx::PxTriangleMeshGeometry geom;
	physx::PxTransform transform;
	Matrix mtx;
	entity.getMatrix(mtx);
	matrix2Transform(mtx, transform);

	physx::PxRigidStatic* actor = PxCreateStatic(*m_impl->m_system->m_impl->m_physics, transform, geom, *m_impl->m_default_material);
	actor->userData = (void*)entity.index;
	m_impl->m_scene->addActor(*actor);
	m_impl->m_scene->simulate(0.01f);
	m_impl->m_scene->fetchResults(true);
	actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
	*/

	Component cmp(entity, MESH_ACTOR_HASH, this, m_impl->m_actors.size() - 1);
	m_impl->m_universe->getEventManager().emitEvent(ComponentEvent(cmp));
	return cmp;
}


void PhysicsScene::getShapeSource(Component cmp, string& str)
{
	str = m_impl->m_shape_sources[cmp.index];
}


void PhysicsScene::setShapeSource(Component cmp, const string& str)
{
	if(m_impl->m_actors[cmp.index] && m_impl->m_shape_sources[cmp.index] == str && m_impl->m_is_dynamic[cmp.index] == !m_impl->m_actors[cmp.index]->isRigidStatic())
	{
		return;
	}

	physx::PxTriangleMeshGeometry geom;
	m_impl->createTriMesh(str.c_str(), geom);
	
	physx::PxTransform transform;
	Matrix mtx;
	cmp.entity.getMatrix(mtx);
	matrix2Transform(mtx, transform);

	if (m_impl->m_actors[cmp.index])
	{
		m_impl->m_scene->removeActor(*m_impl->m_actors[cmp.index]);
		m_impl->m_actors[cmp.index]->release();
		m_impl->m_actors[cmp.index] = NULL;
	}

	physx::PxRigidActor* actor;
	if (m_impl->m_is_dynamic[cmp.index])
	{
		actor = PxCreateDynamic(*m_impl->m_system->m_impl->m_physics, transform, geom, *m_impl->m_default_material, 1.0f);
	}
	else
	{
		actor = PxCreateStatic(*m_impl->m_system->m_impl->m_physics, transform, geom, *m_impl->m_default_material);
	}
	if(actor)
	{
		actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
		actor->userData = (void*)cmp.entity.index;
		m_impl->m_scene->addActor(*actor);
		m_impl->m_actors[cmp.index] = actor;
		m_impl->m_shape_sources[cmp.index] = str;
	}
	else
	{
		g_log_error.log("PhysX", "Could not create PhysX mesh %s", str.c_str());
	}
}


void PhysicsSceneImpl::createTriMesh(const char* path, physx::PxTriangleMeshGeometry& geom)
{
	FILE* fp;
	fopen_s(&fp, path, "rb");
	if(fp)
	{
		Array<Vec3> verts;
		int num_verts, num_indices;
		Array<uint32_t> tris;

		fread(&num_verts, sizeof(num_verts), 1, fp);
		verts.resize(num_verts);
		fread(&verts[0], sizeof(Vec3), num_verts, fp);
		fread(&num_indices, sizeof(num_indices), 1, fp);
		tris.resize(num_indices);
		fread(&tris[0], sizeof(uint32_t), num_indices, fp);
		physx::PxTriangleMeshDesc meshDesc;
		meshDesc.points.count = num_verts;
		meshDesc.points.stride = sizeof(physx::PxVec3);
		meshDesc.points.data = &verts[0];

		meshDesc.triangles.count = num_indices / 3;
		meshDesc.triangles.stride = 3 * sizeof(physx::PxU32);
		meshDesc.triangles.data	= &tris[0];

		for(int i = 0; i < num_indices; ++i)
		{
			ASSERT(tris[i] < (uint32_t)verts.size());
		}

		OutputStream writeBuffer;
		bool status = m_system->m_impl->m_cooking->cookTriangleMesh(meshDesc, writeBuffer);

		InputStream readBuffer(writeBuffer.data, writeBuffer.size);
		geom.triangleMesh = m_system->m_impl->m_physics->createTriangleMesh(readBuffer);
		fclose(fp);
	}
}


void PhysicsSceneImpl::createConvexGeom(const char* path, physx::PxConvexMeshGeometry& geom)
{
	FILE* fp;
	fopen_s(&fp, path, "rb");
	if(fp)
	{
		fseek(fp, 0, SEEK_END);
		long size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		Array<Vec3> vertices;
		vertices.resize(size / sizeof(Vec3));
		fread(&vertices[0], size, 1, fp);
		fclose(fp);
		physx::PxConvexMeshDesc meshDesc;
		meshDesc.points.count = vertices.size();
		meshDesc.points.stride = sizeof(Vec3);
		meshDesc.points.data = &vertices[0];
		meshDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

		OutputStream writeBuffer;
		bool status = m_system->m_impl->m_cooking->cookConvexMesh(meshDesc, writeBuffer);			
		if(!status)
			return;

		InputStream readBuffer(writeBuffer.data, writeBuffer.size);
		physx::PxConvexMesh* mesh = m_system->m_impl->m_physics->createConvexMesh(readBuffer);
		geom.convexMesh = mesh;
	}
}


void PhysicsSceneImpl::setControllerPosition(int index, const Vec3& pos)
{
	physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
	m_controllers[index].m_controller->setPosition(p);
}


void PhysicsScene::render()
{
	m_impl->m_scene->getNbActors(physx::PxActorTypeSelectionFlag::eRIGID_STATIC);
	const physx::PxRenderBuffer& rb = m_impl->m_scene->getRenderBuffer();
	const physx::PxU32 numLines = rb.getNbLines();
	const physx::PxU32 numPoints = rb.getNbPoints();
	const physx::PxU32 numTri = rb.getNbTriangles();
	if(numLines)
	{
		glBegin(GL_LINES);
		const physx::PxDebugLine* PX_RESTRICT lines = rb.getLines();
		for(physx::PxU32 i=0; i<numLines; i++)
		{
			const physx::PxDebugLine& line = lines[i];
			GLubyte bytes[3];
			bytes[0] = (GLubyte)((line.color0 >> 16) & 0xff);
			bytes[1] = (GLubyte)((line.color0 >> 8) & 0xff);
			bytes[2] = (GLubyte)((line.color0) & 0xff);
			glColor3ubv(bytes);
			glVertex3fv((GLfloat*)&line.pos0);
			glVertex3fv((GLfloat*)&line.pos1);
		}
		glEnd();
	}
}


void PhysicsScene::update(float time_delta)
{
	time_delta = 0.01f;
	m_impl->m_scene->simulate(time_delta);
	m_impl->m_scene->fetchResults(true);
	for(int i = 0; i < m_impl->m_is_dynamic.size(); ++i)
	{
		if(m_impl->m_is_dynamic[i])
		{
			physx::PxTransform trans = m_impl->m_actors[i]->getGlobalPose();
			m_impl->m_entities[i].setPosition(trans.p.x, trans.p.y, trans.p.z);
			m_impl->m_entities[i].setRotation(trans.q.x, trans.q.y, trans.q.z, trans.q.w);
		}
	}
	physx::PxVec3 g(0, time_delta * -9.8f, 0);
	for(int i = 0; i < m_impl->m_controllers.size(); ++i)
	{
		const physx::PxExtendedVec3& p = m_impl->m_controllers[i].m_controller->getPosition();
		m_impl->m_controllers[i].m_controller->move(g, 0.0001f, time_delta, physx::PxControllerFilters());
		m_impl->m_controllers[i].m_entity.setPosition((float)p.x, (float)p.y, (float)p.z);
	}
}


void PhysicsScene::moveController(Component cmp, const Vec3& v, float dt)
{
	m_impl->m_controllers[cmp.index].m_controller->move(physx::PxVec3(v.x, v.y, v.z), 0.001f, dt, physx::PxControllerFilters());
}


bool PhysicsScene::raycast(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result)
{
	physx::PxVec3 physx_origin(origin.x, origin.y, origin.z);
	physx::PxVec3 unit_dir(dir.x, dir.y, dir.z);
	physx::PxReal max_distance = distance;
	physx::PxRaycastHit hit;

	const physx::PxSceneQueryFlags outputFlags = physx::PxSceneQueryFlag::eDISTANCE | physx::PxSceneQueryFlag::eIMPACT | physx::PxSceneQueryFlag::eNORMAL;

	bool status = m_impl->m_scene->raycastSingle(physx_origin, unit_dir, max_distance, outputFlags, hit);
	result.normal.x = hit.normal.x;
	result.normal.y = hit.normal.y;
	result.normal.z = hit.normal.z;
	result.position.x = hit.impact.x;
	result.position.y = hit.impact.y;
	result.position.z = hit.impact.z;
	result.entity.index = -1;
	if(hit.shape)
	{
		physx::PxRigidActor& actor = hit.shape->getActor();
		if(actor.userData)
			result.entity.index = (int)actor.userData;
	}
	return status;
}


void PhysicsSceneImpl::handleEvent(Event& event)
{
	if(event.getType() == EntityMovedEvent::type)
	{
		Entity& e = static_cast<EntityMovedEvent&>(event).entity;
		const Entity::ComponentList& cmps = e.getComponents();
		for(int i = 0, c = cmps.size(); i < c; ++i)
		{
			if(cmps[i].type == BOX_ACTOR_HASH)
			{
				Vec3 pos = e.getPosition();
				physx::PxVec3 pvec(pos.x, pos.y, pos.z);
				Quat q;
				e.getMatrix().getRotation(q);
				physx::PxQuat pquat(q.x, q.y, q.z, q.w);
				physx::PxTransform trans(pvec, pquat);
				if(m_actors[cmps[i].index])
				{
					m_actors[cmps[i].index]->setGlobalPose(trans, false);
				}
				break;
			}
			else if(cmps[i].type == CONTROLLER_HASH)
			{
				Vec3 pos = e.getPosition();
				physx::PxExtendedVec3 pvec(pos.x, pos.y, pos.z);
				m_controllers[cmps[i].index].m_controller->setPosition(pvec);
				break;
			}
		}
	}
}


void PhysicsScene::getIsDynamic(Component cmp, bool& is)
{
	is = m_impl->m_is_dynamic[cmp.index];
}


void PhysicsScene::getHalfExtents(Component cmp, Vec3& size)
{
	physx::PxRigidActor* actor = m_impl->m_actors[cmp.index];
	physx::PxShape* shapes;
	if(actor->getNbShapes() == 1 && m_impl->m_actors[cmp.index]->getShapes(&shapes, 1))
	{
		physx::PxVec3& half = shapes->getGeometry().box().halfExtents;
		size.x = half.x;
		size.y = half.y;
		size.z = half.z;
	}
}


void PhysicsScene::setHalfExtents(Component cmp, const Vec3& size)
{
	physx::PxRigidActor* actor = m_impl->m_actors[cmp.index];
	physx::PxShape* shapes;
	if(actor->getNbShapes() == 1 && m_impl->m_actors[cmp.index]->getShapes(&shapes, 1))
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


void PhysicsScene::setIsDynamic(Component cmp, const bool& is)
{
	if(m_impl->m_is_dynamic[cmp.index] != is)
	{
		m_impl->m_is_dynamic[cmp.index] = is;
		physx::PxShape* shapes;
		if(m_impl->m_actors[cmp.index]->getNbShapes() == 1 && m_impl->m_actors[cmp.index]->getShapes(&shapes, 1, 0))
		{
			physx::PxGeometryHolder geom = shapes->getGeometry();
			
			physx::PxTransform transform;
			matrix2Transform(cmp.entity.getMatrix(), transform);

			physx::PxRigidActor* actor;
			if(is)
			{
				actor = PxCreateDynamic(*m_impl->m_system->m_impl->m_physics, transform, geom.any(), *m_impl->m_default_material, 1.0f);
			}
			else
			{
				actor = PxCreateStatic(*m_impl->m_system->m_impl->m_physics, transform, geom.any(), *m_impl->m_default_material);
			}
			m_impl->m_scene->removeActor(*m_impl->m_actors[cmp.index]);
			m_impl->m_actors[cmp.index]->release();
			m_impl->m_scene->addActor(*actor);
			m_impl->m_actors[cmp.index] = actor;
		}
	}
}


void PhysicsSceneImpl::serializeActor(ISerializer& serializer, int idx)
{
	physx::PxShape* shapes;
	if(m_actors[idx]->getNbShapes() == 1 && m_actors[idx]->getShapes(&shapes, 1))
	{
		physx::PxBoxGeometry geom;
		if(shapes->getBoxGeometry(geom))
		{
			serializer.serialize("type", (int32_t)BOX);
			serializer.serialize("x", geom.halfExtents.x);
			serializer.serialize("y", geom.halfExtents.y);
			serializer.serialize("z", geom.halfExtents.z);
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


void PhysicsSceneImpl::deserializeActor(ISerializer& serializer, int idx)
{
	ActorType type;
	serializer.deserialize("type", (int32_t&)type);
	physx::PxTransform transform;
	Matrix mtx;
	m_entities[idx].getMatrix(mtx);
	matrix2Transform(mtx, transform);

	physx::PxGeometry* geom;
	physx::PxBoxGeometry box_geom;
	switch(type)
	{
		case BOX:
			{
				serializer.deserialize("x", box_geom.halfExtents.x);
				serializer.deserialize("y", box_geom.halfExtents.y);
				serializer.deserialize("z", box_geom.halfExtents.z);
				geom = &box_geom;
			}
			break;
		default:
			ASSERT(false);
			break;
	}

	physx::PxRigidActor* actor;
	if(m_is_dynamic[idx])
	{
		actor = PxCreateDynamic(*m_system->m_impl->m_physics, transform, *geom, *m_default_material, 1.0f);
	}
	else
	{
		actor = PxCreateStatic(*m_system->m_impl->m_physics, transform, *geom, *m_default_material);
	}
	actor->userData = (void*)m_entities[idx].index;
	m_scene->addActor(*actor);
	m_actors[idx] = actor;
	actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);

	m_universe->addComponent(m_entities[idx], BOX_ACTOR_HASH, m_owner, idx);
}


void PhysicsScene::serialize(ISerializer& serializer)
{
	serializer.serialize("count", m_impl->m_shape_sources.size());
	serializer.beginArray("actors");
	for(int i = 0; i < m_impl->m_shape_sources.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->m_shape_sources[i]);
		serializer.serializeArrayItem(m_impl->m_is_dynamic[i]);
		serializer.serializeArrayItem(m_impl->m_entities[i].index);
		m_impl->serializeActor(serializer, i);
	}
	serializer.endArray();
	serializer.serialize("count", m_impl->m_controllers.size());
	serializer.beginArray("controllers");
	for(int i = 0; i < m_impl->m_controllers.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->m_controllers[i].m_entity.index);
	}
	serializer.endArray();
}


void PhysicsScene::deserialize(ISerializer& serializer)
{
	int count;
	serializer.deserialize("count", count);
	m_impl->m_shape_sources.resize(count);
	m_impl->m_is_dynamic.resize(count);
	m_impl->m_entities.resize(count);
	m_impl->m_actors.resize(count);
	serializer.deserializeArrayBegin("actors");
	for(int i = 0; i < m_impl->m_shape_sources.size(); ++i)
	{
		serializer.deserializeArrayItem(m_impl->m_shape_sources[i]);
		serializer.deserializeArrayItem(m_impl->m_is_dynamic[i]);
		serializer.deserializeArrayItem(m_impl->m_entities[i].index);
		m_impl->m_entities[i].universe = m_impl->m_universe;
		m_impl->deserializeActor(serializer, i);
	}
	serializer.deserializeArrayEnd();
	serializer.deserialize("count", count);
	m_impl->m_controllers.clear();
	serializer.deserializeArrayBegin("controllers");
	for(int i = 0; i < count; ++i)
	{
		int index;
		serializer.deserializeArrayItem(index);
		Entity e(m_impl->m_universe, index);
		createController(e);
		m_impl->setControllerPosition(i, e.getPosition());
		m_impl->m_universe->addComponent(e, CONTROLLER_HASH, this, i);
	}		
	serializer.deserializeArrayEnd();
}


PhysicsSystem& PhysicsScene::getSystem() const
{
	return *m_impl->m_system;
}


} // !namespace Lux
