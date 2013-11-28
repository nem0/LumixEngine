#include "physics_scene.h"
#include "physics_system.h"
#include <PxPhysicsAPI.h>
#include "cooking/PxCooking.h"
#include "common/PxIO.h"
#include <cassert>
#include <cstdio>
#include "core/crc32.h"
#include "core/matrix.h"
#include "core/json_object.h"
#include "core/event_manager.h"
#include "universe/entity_moved_event.h"
#include "universe/component_event.h"
#include "physics/physics_system.h"
#include "core/iserializer.h"
#include "physics_system_impl.h"


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
		physx::PxController* controller;
		Entity entity;
	};

	Universe*						universe;
	physx::PxScene*					scene;
	PhysicsSystem*					system;
	physx::PxMaterial*				default_material;
	vector<physx::PxRigidActor*>	actors;
	vector<string>					shape_sources;
	vector<bool>					is_dynamic;
	vector<Entity>					entities;
	vector<int>						index_map;
		
	vector<Controller> controllers;
	PhysicsScene* owner;
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
	m_impl->owner = this;
	m_impl->universe = &universe;
	m_impl->universe->getEventManager()->registerListener(EntityMovedEvent::type, m_impl, &PhysicsSceneImpl::handleEvent);
	physx::PxSceneDesc sceneDesc(system.m_impl->physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
	if(!sceneDesc.cpuDispatcher) {
		physx::PxDefaultCpuDispatcher* cpu_dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
		if(!cpu_dispatcher)
			printf("PxDefaultCpuDispatcherCreate failed!");
		sceneDesc.cpuDispatcher = cpu_dispatcher;
	} 
	if(!sceneDesc.filterShader)
		sceneDesc.filterShader  = &physx::PxDefaultSimulationFilterShader;

	m_impl->scene = system.m_impl->physics->createScene(sceneDesc);
	if (!m_impl->scene)
		return false;
	m_impl->scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE,     1.0);
	m_impl->scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
	m_impl->system = &system;
	m_impl->default_material = m_impl->system->m_impl->physics->createMaterial(0.5,0.5,0.5);
	return true;
}


void PhysicsScene::destroy()
{
	m_impl->default_material->release();
	m_impl->scene->release();
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
	assert(cmp.type == physical_type);
	int inner_index = m_impl->index_map[cmp.index];
	m_impl->scene->removeActor(*m_impl->actors[inner_index]);
	m_impl->actors[inner_index]->release();
	m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
	m_impl->actors.eraseFast(inner_index);
	m_impl->shape_sources.eraseFast(inner_index);
	m_impl->is_dynamic.eraseFast(inner_index);
	m_impl->entities.eraseFast(inner_index);
	for(int i = 0; i < m_impl->index_map.size(); ++i)
	{
		if(m_impl->index_map[i] == m_impl->entities.size())
		{
			m_impl->index_map[i] = inner_index;
			break;
		}
	}
	m_impl->index_map[cmp.index] = -1;
	m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
}


Component PhysicsScene::createController(Entity entity)
{
	physx::PxCapsuleControllerDesc cDesc;
	cDesc.material			= m_impl->default_material;
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
	c.controller = m_impl->system->m_impl->controller_manager->createController(*m_impl->system->m_impl->physics, m_impl->scene, cDesc);
	c.entity = entity;

	m_impl->controllers.push_back(c);
	
	Component cmp(entity, controller_type, this, m_impl->controllers.size() - 1);
	m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


Component PhysicsScene::createActor(Entity entity)
{
	int new_index = m_impl->entities.size();
	for(int i = 0; i < m_impl->index_map.size(); ++i)
	{
		if(m_impl->index_map[i] == -1) 
		{
			new_index = i;
			break;
		}
	}
	if(new_index == m_impl->entities.size())
	{
		m_impl->actors.push_back(0);
		m_impl->shape_sources.push_back("");
		m_impl->is_dynamic.push_back(false);
		m_impl->entities.push_back(entity);
	}
	else
	{
		m_impl->actors[new_index] = 0;
		m_impl->shape_sources[new_index] = "";
		m_impl->is_dynamic[new_index] = false;
		m_impl->entities[new_index] = entity;
	}
	Component cmp(entity, physical_type, this, m_impl->actors.size() - 1);
	m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


void PhysicsScene::getShapeSource(Component cmp, string& str)
{
	str = m_impl->shape_sources[cmp.index];
}


void PhysicsScene::setShapeSource(Component cmp, const string& str)
{
	if(m_impl->actors[cmp.index] && m_impl->shape_sources[cmp.index] == str && m_impl->is_dynamic[cmp.index] == !m_impl->actors[cmp.index]->isRigidStatic())
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
				assert(false); // unsupported type
			}
		}
		delete[] buffer;

		if(m_impl->actors[cmp.index])
		{
			m_impl->scene->removeActor(*m_impl->actors[cmp.index]);
			m_impl->actors[cmp.index]->release();
			m_impl->actors[cmp.index] = 0;
		}

		if(geom)
		{
			physx::PxRigidActor* actor;
			if(m_impl->is_dynamic[cmp.index])
			{
				actor = PxCreateDynamic(*m_impl->system->m_impl->physics, transform, *geom, *m_impl->default_material, 1.0f);
			}
			else
			{
				actor = PxCreateStatic(*m_impl->system->m_impl->physics, transform, *geom, *m_impl->default_material);
			}
			actor->userData = (void*)cmp.entity.index;
			m_impl->scene->addActor(*actor);
			m_impl->scene->simulate(0.01f);
			m_impl->scene->fetchResults(true);
			m_impl->actors[cmp.index] = actor;
			actor->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
			m_impl->shape_sources[cmp.index] = str;
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
		bool status = system->m_impl->cooking->cookTriangleMesh(meshDesc, writeBuffer);

		InputStream readBuffer(writeBuffer.data, writeBuffer.size);
		geom.triangleMesh = system->m_impl->physics->createTriangleMesh(readBuffer);
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
		bool status = system->m_impl->cooking->cookConvexMesh(meshDesc, writeBuffer);			
		if(!status)
			return;

		InputStream readBuffer(writeBuffer.data, writeBuffer.size);
		physx::PxConvexMesh* mesh = system->m_impl->physics->createConvexMesh(readBuffer);
		geom.convexMesh = mesh;
	}
}


void PhysicsSceneImpl::postDeserialize()
{
	actors.resize(shape_sources.size());
	for(int i = 0; i < shape_sources.size(); ++i)
	{
		entities[i].universe = universe;
		owner->setIsDynamic(Component(entities[i], physical_type, this, i), is_dynamic[i]);
		Component cmp(entities[i], physical_type, owner, i);
		universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	}
	index_map.resize(shape_sources.size());
	for(int i = 0; i < shape_sources.size(); ++i)
	{
		index_map[i] = i;
	}
}


void PhysicsSceneImpl::setControllerPosition(int index, const Vec3& pos)
{
	physx::PxExtendedVec3 p(pos.x, pos.y, pos.z);
	controllers[index].controller->setPosition(p);
}


void PhysicsScene::update(float time_delta)
{
	time_delta = 0.01f;
	m_impl->scene->simulate(time_delta);
	m_impl->scene->fetchResults(true);
	for(int i = 0; i < m_impl->is_dynamic.size(); ++i)
	{
		if(m_impl->is_dynamic[i])
		{
			physx::PxTransform trans = m_impl->actors[i]->getGlobalPose();
			m_impl->entities[i].setPosition(trans.p.x, trans.p.y, trans.p.z);
			m_impl->entities[i].setRotation(trans.q.x, trans.q.y, trans.q.z, trans.q.w);
		}
	}
	physx::PxVec3 g(0, time_delta * -9.8f, 0);
	for(int i = 0; i < m_impl->controllers.size(); ++i)
	{
		const physx::PxExtendedVec3& p = m_impl->controllers[i].controller->getPosition();
		m_impl->controllers[i].controller->move(g, 0.0001f, time_delta, physx::PxControllerFilters());
		m_impl->controllers[i].entity.setPosition((float)p.x, (float)p.y, (float)p.z);
	}

}


void PhysicsScene::moveController(Component cmp, const Vec3& v, float dt)
{
	m_impl->controllers[cmp.index].controller->move(physx::PxVec3(v.x, v.y, v.z), 0.001f, dt, physx::PxControllerFilters());
}


bool PhysicsScene::raycast(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result)
{
	physx::PxVec3 physx_origin(origin.x, origin.y, origin.z);
	physx::PxVec3 unit_dir(dir.x, dir.y, dir.z);
	physx::PxReal max_distance = distance;
	physx::PxRaycastHit hit;

	const physx::PxSceneQueryFlags outputFlags = physx::PxSceneQueryFlag::eDISTANCE | physx::PxSceneQueryFlag::eIMPACT | physx::PxSceneQueryFlag::eNORMAL;

	bool status = m_impl->scene->raycastSingle(physx_origin, unit_dir, max_distance, outputFlags, hit);
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
				if(actors[cmps[i].index])
				{
					actors[cmps[i].index]->setGlobalPose(trans, false);
				}
				break;
			}
			else if(cmps[i].type == controller_type)
			{
				Vec3 pos = e.getPosition();
				physx::PxExtendedVec3 pvec(pos.x, pos.y, pos.z);
				controllers[cmps[i].index].controller->setPosition(pvec);
				break;
			}
		}
	}
}


void PhysicsScene::getIsDynamic(Component cmp, bool& is)
{
	is = m_impl->is_dynamic[cmp.index];
}


void PhysicsScene::setIsDynamic(Component cmp, const bool& is)
{
	m_impl->is_dynamic[cmp.index] = is;
	setShapeSource(cmp, m_impl->shape_sources[cmp.index]);
}


void PhysicsScene::serialize(ISerializer& serializer)
{
	serializer.serialize("count", m_impl->shape_sources.size());
	serializer.beginArray("actors");
	for(int i = 0; i < m_impl->shape_sources.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->shape_sources[i]);
		serializer.serializeArrayItem(m_impl->is_dynamic[i]);
		serializer.serializeArrayItem(m_impl->entities[i].index);
	}
	serializer.endArray();
	serializer.serialize("count", m_impl->controllers.size());
	serializer.beginArray("controllers");
	for(int i = 0; i < m_impl->controllers.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->controllers[i].entity.index);
	}
	serializer.endArray();
}


void PhysicsScene::deserialize(ISerializer& serializer)
{
	int count;
	serializer.deserialize("count", count);
	m_impl->shape_sources.resize(count);
	m_impl->is_dynamic.resize(count);
	m_impl->entities.resize(count);
	serializer.deserializeArrayBegin("actors");
	for(int i = 0; i < m_impl->shape_sources.size(); ++i)
	{
		serializer.deserializeArrayItem(m_impl->shape_sources[i]);
		serializer.deserializeArrayItem(m_impl->is_dynamic[i]);
		serializer.deserializeArrayItem(m_impl->entities[i].index);
	}
	serializer.deserialize("count", count);
	m_impl->controllers.clear();
	serializer.deserializeArrayBegin("controllers");
	for(int i = 0; i < count; ++i)
	{
		int index;
		serializer.deserializeArrayItem(index);
		Entity e(m_impl->universe, index);
		createController(e);
		m_impl->setControllerPosition(i, e.getPosition());
	}		
	serializer.deserializeArrayEnd();
	m_impl->postDeserialize();
}


PhysicsSystem& PhysicsScene::getSystem() const
{
	return *m_impl->system;
}


physx::PxScene* PhysicsScene::getRawScene()
{
	return m_impl->scene;
}


} // !namespace Lux
