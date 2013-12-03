#include "physics/physics_scene.h"
#include <cstdio>
#include <PxPhysicsAPI.h>
#include "cooking/PxCooking.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/iserializer.h"
#include "core/json_object.h"
#include "core/matrix.h"
#include "universe/component_event.h"
#include "universe/entity_moved_event.h"
#include "physics/physics_system.h"
#include "physics/physics_system_impl.h"


namespace Lux
{


struct PhysicsSceneImpl
{
	static void handleEvent(void* data, Event& event);
	void handleEvent(Event& event);
	void createConvexGeom(const char* path, physx::PxConvexMeshGeometry& geom);
	void createTriMesh(const char* path, physx::PxTriangleMeshGeometry& geom);
	void postDeserialize();
	void setControllerPosition(int index, const Vec3& pos);

	struct Controller
	{
		physx::PxController* m_controller;
		Entity m_entity;
	};

	Universe*						m_universe;
	physx::PxScene*					m_scene;
	PhysicsSystem*					m_system;
	physx::PxMaterial*				m_default_material;
	vector<physx::PxRigidActor*>	m_actors;
	vector<string>					m_shape_sources;
	vector<bool>					m_is_dynamic;
	vector<Entity>					m_entities;
	vector<int>						m_index_map;
	vector<Controller>				m_controllers;
	PhysicsScene*					m_owner;
};


static const uint32_t physical_type = crc32("physical");
static const uint32_t controller_type = crc32("physical_controller");


struct OutputStream : public physx::PxOutputStream
{
	OutputStream()
	{
		data = new unsigned char[4096];
		capacity = 4096;
		size = 0;
	}

	~OutputStream()
	{
		delete[] data;
	}


	virtual physx::PxU32 write(const void* src, physx::PxU32 count)
	{
		if(size + (int)count > capacity)
		{
			unsigned char* new_data = new unsigned char[capacity + 4096];
			memcpy(new_data, data, size);
			delete[] data;
			data = new_data;
			capacity += 4096;
		}
		memcpy(data + size, src, count);
		size += count;
		return count;
	}

	unsigned char* data;
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


bool PhysicsScene::create(PhysicsSystem& system, Universe& universe)
{
	m_impl = new PhysicsSceneImpl;
	m_impl->m_owner = this;
	m_impl->m_universe = &universe;
	m_impl->m_universe->getEventManager()->registerListener(EntityMovedEvent::type, m_impl, &PhysicsSceneImpl::handleEvent);
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
	m_impl->m_system = &system;
	m_impl->m_default_material = m_impl->m_system->m_impl->m_physics->createMaterial(0.5,0.5,0.5);
	return true;
}


void PhysicsScene::destroy()
{
	m_impl->m_default_material->release();
	m_impl->m_scene->release();
	delete m_impl;
	m_impl = 0;
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
	ASSERT(cmp.type == physical_type);
	int inner_index = m_impl->m_index_map[cmp.index];
	m_impl->m_scene->removeActor(*m_impl->m_actors[inner_index]);
	m_impl->m_actors[inner_index]->release();
	m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
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
	m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
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

	m_impl->m_controllers.push_back(c);
	
	Component cmp(entity, controller_type, this, m_impl->m_controllers.size() - 1);
	m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


Component PhysicsScene::createActor(Entity entity)
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
		m_impl->m_actors.push_back(0);
		m_impl->m_shape_sources.push_back("");
		m_impl->m_is_dynamic.push_back(false);
		m_impl->m_entities.push_back(entity);
	}
	else
	{
		m_impl->m_actors[new_index] = 0;
		m_impl->m_shape_sources[new_index] = "";
		m_impl->m_is_dynamic[new_index] = false;
		m_impl->m_entities[new_index] = entity;
	}
	Component cmp(entity, physical_type, this, m_impl->m_actors.size() - 1);
	m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
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
	FILE* fp;
	fopen_s(&fp, str.c_str(), "r");
	if(fp)
	{
		physx::PxTransform transform;
		Matrix mtx;
		cmp.entity.getMatrix(mtx);
		matrix2Transform(mtx, transform);
		physx::PxGeometry* geom = 0;
		physx::PxBoxGeometry box_geom;
		physx::PxSphereGeometry sphere_geom;
		physx::PxConvexMeshGeometry convex_geom;
		physx::PxTriangleMeshGeometry trimesh_geom;

		fseek(fp, 0, SEEK_END);
		long size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char* buffer = new char[size];
		fread(buffer, size, 1, fp);
		jsmn_parser parser;
		jsmn_init(&parser);
		jsmntok_t tokens[255];
		jsmn_parse(&parser, buffer, tokens, 255);
		JsonObject root(0, buffer, tokens);
		JsonObject js_shape = root.getProperty("shape");
		char tmp[255];
		if(js_shape.toString(tmp, 255))
		{
			if(strcmp(tmp, "sphere") == 0)
			{
				root.getProperty("radius").toString(tmp, 255);
				sscanf_s(tmp, "%f", &sphere_geom.radius);
				geom = &sphere_geom;
			}
			else if(strcmp(tmp, "box") == 0)
			{
				root.getProperty("x").toString(tmp, 255);
				sscanf_s(tmp, "%f", &box_geom.halfExtents.x);
				root.getProperty("y").toString(tmp, 255);
				sscanf_s(tmp, "%f", &box_geom.halfExtents.y);
				root.getProperty("z").toString(tmp, 255);
				sscanf_s(tmp, "%f", &box_geom.halfExtents.z);
				geom = &box_geom;
			}
			else if(strcmp(tmp, "convex") == 0)
			{
				if(root.getProperty("src").toString(tmp, 255))
				{
					m_impl->createConvexGeom(tmp, convex_geom);
				}
				geom = &convex_geom;
			}
			else if(strcmp(tmp, "trimesh") == 0)
			{
				if(root.getProperty("src").toString(tmp, 255))
				{
					m_impl->createTriMesh(tmp, trimesh_geom);
				}
				geom = &trimesh_geom;
			}
			else
			{
				ASSERT(false); // unsupported type
			}
		}
		delete[] buffer;

		if(m_impl->m_actors[cmp.index])
		{
			m_impl->m_scene->removeActor(*m_impl->m_actors[cmp.index]);
			m_impl->m_actors[cmp.index]->release();
			m_impl->m_actors[cmp.index] = 0;
		}

		if(geom)
		{
			physx::PxRigidActor* actor;
			if(m_impl->m_is_dynamic[cmp.index])
			{
				actor = PxCreateDynamic(*m_impl->m_system->m_impl->m_physics, transform, *geom, *m_impl->m_default_material, 1.0f);
			}
			else
			{
				actor = PxCreateStatic(*m_impl->m_system->m_impl->m_physics, transform, *geom, *m_impl->m_default_material);
			}
			actor->userData = (void*)cmp.entity.index;
			m_impl->m_scene->addActor(*actor);
			m_impl->m_scene->simulate(0.01f);
			m_impl->m_scene->fetchResults(true);
			m_impl->m_actors[cmp.index] = actor;
			actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
			m_impl->m_shape_sources[cmp.index] = str;
		}
	}
}


void PhysicsSceneImpl::createTriMesh(const char* path, physx::PxTriangleMeshGeometry& geom)
{
	FILE* fp;
	fopen_s(&fp, path, "rb");
	if(fp)
	{
		vector<Vec3> verts;
		int num_verts, num_indices;
		vector<uint32_t> tris;

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
		meshDesc.triangles.stride = 3*sizeof(physx::PxU32);
		meshDesc.triangles.data	= &tris[0];

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
		vector<Vec3> vertices;
		vertices.reserve(size / sizeof(Vec3));
		vertices.set_size(size / sizeof(Vec3));
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


void PhysicsSceneImpl::postDeserialize()
{
	m_actors.resize(m_shape_sources.size());
	for(int i = 0; i < m_shape_sources.size(); ++i)
	{
		m_entities[i].universe = m_universe;
		m_owner->setIsDynamic(Component(m_entities[i], physical_type, this, i), m_is_dynamic[i]);
		Component cmp(m_entities[i], physical_type, m_owner, i);
		m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	}
	m_index_map.resize(m_shape_sources.size());
	for(int i = 0; i < m_shape_sources.size(); ++i)
	{
		m_index_map[i] = i;
	}
}


void PhysicsSceneImpl::setControllerPosition(int index, const Vec3& pos)
{
	physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
	m_controllers[index].m_controller->setPosition(p);
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


void PhysicsSceneImpl::handleEvent(void* data, Event& event)
{
	static_cast<PhysicsSceneImpl*>(data)->handleEvent(event);
}


void PhysicsSceneImpl::handleEvent(Event& event)
{
	if(event.getType() == EntityMovedEvent::type)
	{
		Entity& e = static_cast<EntityMovedEvent&>(event).entity;
		const vector<Component>& cmps = e.getComponents();
		for(int i = 0, c = cmps.size(); i < c; ++i)
		{
			if(cmps[i].type == physical_type)
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
			else if(cmps[i].type == controller_type)
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


void PhysicsScene::setIsDynamic(Component cmp, const bool& is)
{
	m_impl->m_is_dynamic[cmp.index] = is;
	setShapeSource(cmp, m_impl->m_shape_sources[cmp.index]);
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
	serializer.deserializeArrayBegin("actors");
	for(int i = 0; i < m_impl->m_shape_sources.size(); ++i)
	{
		serializer.deserializeArrayItem(m_impl->m_shape_sources[i]);
		serializer.deserializeArrayItem(m_impl->m_is_dynamic[i]);
		serializer.deserializeArrayItem(m_impl->m_entities[i].index);
	}
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
	}		
	serializer.deserializeArrayEnd();
	m_impl->postDeserialize();
}


PhysicsSystem& PhysicsScene::getSystem() const
{
	return *m_impl->m_system;
}


} // !namespace Lux
