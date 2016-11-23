#include "universe.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/matrix.h"
#include "engine/property_register.h"
#include <cstdint>


namespace Lumix
{


static const int RESERVED_ENTITIES_COUNT = 5000;


Universe::~Universe()
{
}


Universe::Universe(IAllocator& allocator)
	: m_allocator(allocator)
	, m_name_to_id_map(m_allocator)
	, m_id_to_name_map(m_allocator)
	, m_transformations(m_allocator)
	, m_components(m_allocator)
	, m_component_added(m_allocator)
	, m_component_destroyed(m_allocator)
	, m_entity_created(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_moved(m_allocator)
	, m_entity_map(m_allocator)
	, m_first_free_slot(-1)
	, m_scenes(m_allocator)
{
	m_transformations.reserve(RESERVED_ENTITIES_COUNT);
	m_components.reserve(RESERVED_ENTITIES_COUNT);
	m_entity_map.reserve(RESERVED_ENTITIES_COUNT);
	for (int i = 0; i < lengthOf(m_component_type_scene_map); ++i)
	{
		m_component_type_scene_map[i] = 0;
	}
}


IScene* Universe::getScene(ComponentType type) const
{
	return m_component_type_scene_map[type.index];
}


IScene* Universe::getScene(u32 hash) const
{
	for (auto* scene : m_scenes)
	{
		if (crc32(scene->getPlugin().getName()) == hash)
		{
			return scene;
		}
	}
	return nullptr;
}


Array<IScene*>& Universe::getScenes()
{
	return m_scenes;
}


void Universe::addScene(IScene* scene)
{
	m_scenes.push(scene);
}


const Vec3& Universe::getPosition(Entity entity) const
{
	return m_transformations[m_entity_map[entity.index]].position;
}


const Quat& Universe::getRotation(Entity entity) const
{
	return m_transformations[m_entity_map[entity.index]].rotation;
}


void Universe::setRotation(Entity entity, const Quat& rot)
{
	m_transformations[m_entity_map[entity.index]].rotation = rot;
	entityTransformed().invoke(entity);
}


void Universe::setRotation(Entity entity, float x, float y, float z, float w)
{
	m_transformations[m_entity_map[entity.index]].rotation.set(x, y, z, w);
	entityTransformed().invoke(entity);
}


bool Universe::hasEntity(Entity entity) const
{
	return entity.index >= 0 && entity.index < m_entity_map.size() && m_entity_map[entity.index] >= 0;
}


void Universe::setMatrix(Entity entity, const Matrix& mtx)
{
	Transformation& out = m_transformations[m_entity_map[entity.index]];
	mtx.decompose(out.position, out.rotation, out.scale);
	entityTransformed().invoke(entity);
}


Matrix Universe::getPositionAndRotation(Entity entity) const
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	Matrix mtx = transform.rotation.toMatrix();
	mtx.setTranslation(transform.position);
	return mtx;
}


void Universe::setTransform(Entity entity, const Transform& transform)
{
	auto& tmp = m_transformations[m_entity_map[entity.index]];
	tmp.position = transform.pos;
	tmp.rotation = transform.rot;
	entityTransformed().invoke(entity);
}


Transform Universe::getTransform(Entity entity) const
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	return Transform(transform.position, transform.rotation);
}


Matrix Universe::getMatrix(Entity entity) const
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	Matrix mtx = transform.rotation.toMatrix();
	mtx.setTranslation(transform.position);
	mtx.multiply3x3(transform.scale);
	return mtx;
}


void Universe::setPosition(Entity entity, float x, float y, float z)
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	transform.position.set(x, y, z);
	entityTransformed().invoke(entity);
}


void Universe::setPosition(Entity entity, const Vec3& pos)
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	transform.position = pos;
	entityTransformed().invoke(entity);
}


void Universe::setEntityName(Entity entity, const char* name)
{
	int name_index = m_id_to_name_map.find(entity.index);
	if (name_index >= 0)
	{
		u32 hash = crc32(m_id_to_name_map.at(name_index).c_str());
		m_name_to_id_map.erase(hash);
		m_id_to_name_map.eraseAt(name_index);
	}

	if (name && name[0] != '\0')
	{
		m_name_to_id_map.insert(crc32(name), entity.index);
		m_id_to_name_map.insert(entity.index, string(name, getAllocator()));
	}
}


const char* Universe::getEntityName(Entity entity) const
{
	int name_index = m_id_to_name_map.find(entity.index);
	return name_index < 0 ? "" : m_id_to_name_map.at(name_index).c_str();
}


void Universe::createEntity(Entity entity)
{
	ASSERT(isValid(entity));
	int id = m_first_free_slot;
	int prev_id = -1;
	while (id >= 0 && id != entity.index)
	{
		prev_id = id;
		id = -m_entity_map[id];
	}

	ASSERT(id == entity.index);
	if (prev_id == -1)
	{
		m_first_free_slot = -m_entity_map[entity.index];
	}
	else
	{
		m_entity_map[prev_id] = m_entity_map[entity.index];
	}
	m_entity_map[entity.index] = m_transformations.size();

	Transformation& trans = m_transformations.emplace();
	trans.position.set(0, 0, 0);
	trans.rotation.set(0, 0, 0, 1);
	trans.scale = 1;
	trans.entity = entity;
	m_components.emplace(0);

	m_entity_created.invoke(entity);
}


Entity Universe::createEntity(const Vec3& position, const Quat& rotation)
{
	int global_id = 0;
	if (m_first_free_slot >= 0)
	{
		global_id = m_first_free_slot;
		m_first_free_slot = -m_entity_map[m_first_free_slot];
		m_entity_map[global_id] = m_transformations.size();
	}
	else
	{
		global_id = m_entity_map.size();
		m_entity_map.push(m_transformations.size());
	}

	Transformation& trans = m_transformations.emplace();
	trans.position = position;
	trans.rotation = rotation;
	trans.scale = 1;
	trans.entity.index = global_id;
	m_components.emplace(0);
	m_entity_created.invoke({global_id});

	return {global_id};
}


void Universe::destroyEntity(Entity entity)
{
	if (!isValid(entity) || m_entity_map[entity.index] < 0) return;

	u64 mask = m_components[m_entity_map[entity.index]];
	for (int i = 0; i < MAX_COMPONENTS_TYPES_COUNT; ++i)
	{
		if ((mask & ((u64)1 << i)) != 0)
		{
			ComponentType type = {i};
			auto original_mask = mask;
			IScene* scene = m_component_type_scene_map[i];
			scene->destroyComponent(scene->getComponent(entity, type), type);
			mask = m_components[m_entity_map[entity.index]];
			ASSERT(original_mask != mask);
		}
	}

	Entity last_item_id = m_transformations.back().entity;
	m_entity_map[last_item_id.index] = m_entity_map[entity.index];
	m_transformations.eraseFast(m_entity_map[entity.index]);
	m_components.eraseFast(m_entity_map[entity.index]);
	m_entity_map[entity.index] = m_first_free_slot >= 0 ? -m_first_free_slot : INT32_MIN;

	int name_index = m_id_to_name_map.find(entity.index);
	if (name_index >= 0)
	{
		u32 name_hash = crc32(m_id_to_name_map.at(name_index).c_str());
		m_name_to_id_map.erase(name_hash);
		m_id_to_name_map.eraseAt(name_index);
	}

	m_first_free_slot = entity.index;
	m_entity_destroyed.invoke(entity);
}


int Universe::getDenseIdx(Entity entity)
{
	return !isValid(entity) ? -1 : m_entity_map[entity.index];
}


Entity Universe::getEntityFromDenseIdx(int idx)
{
	return m_transformations[idx].entity;
}


Entity Universe::getFirstEntity()
{
	for (int i = 0; i < m_entity_map.size(); ++i)
	{
		if (m_entity_map[i] >= 0)
		{
			return {i};
		}
	}
	return INVALID_ENTITY;
}


Entity Universe::getNextEntity(Entity entity)
{
	for (int i = entity.index + 1; i < m_entity_map.size(); ++i)
	{
		if (m_entity_map[i] >= 0)
		{
			return {i};
		}
	}
	return INVALID_ENTITY;
}


void Universe::serialize(OutputBlob& serializer)
{
	serializer.write((i32)m_transformations.size());
	serializer.write(&m_transformations[0], sizeof(m_transformations[0]) * m_transformations.size());
	serializer.write((i32)m_id_to_name_map.size());
	for (int i = 0, c = m_id_to_name_map.size(); i < c; ++i)
	{
		serializer.write(m_id_to_name_map.getKey(i));
		serializer.writeString(m_id_to_name_map.at(i).c_str());
	}

	serializer.write(m_first_free_slot);
	serializer.write((i32)m_entity_map.size());
	if (!m_entity_map.empty())
	{
		serializer.write(&m_entity_map[0], sizeof(m_entity_map[0]) * m_entity_map.size());
	}
}


void Universe::deserialize(InputBlob& serializer)
{
	i32 count;
	serializer.read(count);
	m_transformations.resize(count);
	for (int i = 0, c = m_components.size(); i < c; ++i) m_components[i] = 0;
	m_components.resize(count);

	serializer.read(&m_transformations[0], sizeof(m_transformations[0]) * m_transformations.size());

	serializer.read(count);
	m_id_to_name_map.clear();
	m_name_to_id_map.clear();
	for (int i = 0; i < count; ++i)
	{
		u32 key;
		char name[50];
		serializer.read(key);
		serializer.readString(name, sizeof(name));
		m_id_to_name_map.insert(key, string(name, m_allocator));
		m_name_to_id_map.insert(crc32(name), key);
	}

	serializer.read(m_first_free_slot);
	serializer.read(count);
	m_entity_map.resize(count);
	if (!m_entity_map.empty())
	{
		serializer.read(&m_entity_map[0], sizeof(m_entity_map[0]) * count);
	}
}


void Universe::setScale(Entity entity, float scale)
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	transform.scale = scale;
	entityTransformed().invoke(entity);
}


float Universe::getScale(Entity entity)
{
	auto& transform = m_transformations[m_entity_map[entity.index]];
	return transform.scale;
}


void Universe::registerComponentTypeScene(ComponentType type, IScene* scene)
{
	ASSERT(!m_component_type_scene_map[type.index]);
	m_component_type_scene_map[type.index] = scene;
}


ComponentUID Universe::getFirstComponent(Entity entity) const
{
	u64 mask = m_components[m_entity_map[entity.index]];
	for (int i = 0; i < MAX_COMPONENTS_TYPES_COUNT; ++i)
	{
		if ((mask & (u64(1) << i)) != 0)
		{
			IScene* scene = m_component_type_scene_map[i];
			return ComponentUID(entity, {i}, scene, scene->getComponent(entity, {i}));
		}
	}
	return ComponentUID::INVALID;
}


ComponentUID Universe::getNextComponent(const ComponentUID& cmp) const
{
	u64 mask = m_components[m_entity_map[cmp.entity.index]];
	for (int i = cmp.type.index + 1; i < MAX_COMPONENTS_TYPES_COUNT; ++i)
	{
		if ((mask & (u64(1) << i)) != 0)
		{
			IScene* scene = m_component_type_scene_map[i];
			return ComponentUID(cmp.entity, {i}, scene, scene->getComponent(cmp.entity, {i}));
		}
	}
	return ComponentUID::INVALID;
}


ComponentUID Universe::getComponent(Entity entity, ComponentType component_type) const
{
	u64 mask = m_components[m_entity_map[entity.index]];
	if ((mask & (u64(1) << component_type.index)) == 0) return ComponentUID::INVALID;
	IScene* scene = m_component_type_scene_map[component_type.index];
	return ComponentUID(entity, component_type, scene, scene->getComponent(entity, component_type));
}


bool Universe::hasComponent(Entity entity, ComponentType component_type) const
{
	u64 mask = m_components[m_entity_map[entity.index]];
	return (mask & (u64(1) << component_type.index)) != 0;
}


void Universe::destroyComponent(Entity entity, ComponentType component_type, IScene* scene, ComponentHandle index)
{
	auto mask = m_components[m_entity_map[entity.index]];
	auto old_mask = mask;
	mask &= ~((u64)1 << component_type.index);
	auto x = PropertyRegister::getComponentTypeID(component_type.index);
	ASSERT(old_mask != mask);
	m_components[m_entity_map[entity.index]] = mask;
	m_component_destroyed.invoke(ComponentUID(entity, component_type, scene, index));
}


void Universe::addComponent(Entity entity, ComponentType component_type, IScene* scene, ComponentHandle index)
{
	ComponentUID cmp(entity, component_type, scene, index);
	m_components[m_entity_map[entity.index]] |= (u64)1 << component_type.index;
	m_component_added.invoke(cmp);
}


bool Universe::nameExists(const char* name) const
{
	return m_name_to_id_map.find(crc32(name)) != -1;
}


} // namespace Lumix
