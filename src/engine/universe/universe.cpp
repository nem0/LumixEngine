#include "universe.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/matrix.h"
#include "engine/prefab.h"
#include "engine/property_register.h"
#include "engine/serializer.h"
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
	, m_entities(m_allocator)
	, m_component_added(m_allocator)
	, m_component_destroyed(m_allocator)
	, m_entity_created(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_moved(m_allocator)
	, m_first_free_slot(-1)
	, m_scenes(m_allocator)
{
	m_entities.reserve(RESERVED_ENTITIES_COUNT);
}


IScene* Universe::getScene(ComponentType type) const
{
	return m_component_type_map[type.index].scene;
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
	return m_entities[entity.index].position;
}


const Quat& Universe::getRotation(Entity entity) const
{
	return m_entities[entity.index].rotation;
}


void Universe::setRotation(Entity entity, const Quat& rot)
{
	m_entities[entity.index].rotation = rot;
	entityTransformed().invoke(entity);
}


void Universe::setRotation(Entity entity, float x, float y, float z, float w)
{
	m_entities[entity.index].rotation.set(x, y, z, w);
	entityTransformed().invoke(entity);
}


bool Universe::hasEntity(Entity entity) const
{
	return entity.index >= 0 && entity.index < m_entities.size() && m_entities[entity.index].valid;
}


void Universe::setMatrix(Entity entity, const Matrix& mtx)
{
	EntityData& out = m_entities[entity.index];
	mtx.decompose(out.position, out.rotation, out.scale);
	entityTransformed().invoke(entity);
}


Matrix Universe::getPositionAndRotation(Entity entity) const
{
	auto& transform = m_entities[entity.index];
	Matrix mtx = transform.rotation.toMatrix();
	mtx.setTranslation(transform.position);
	return mtx;
}


void Universe::setTransform(Entity entity, const Transform& transform)
{
	auto& tmp = m_entities[entity.index];
	tmp.position = transform.pos;
	tmp.rotation = transform.rot;
	entityTransformed().invoke(entity);
}


Transform Universe::getTransform(Entity entity) const
{
	auto& transform = m_entities[entity.index];
	return Transform(transform.position, transform.rotation);
}


Matrix Universe::getMatrix(Entity entity) const
{
	auto& transform = m_entities[entity.index];
	Matrix mtx = transform.rotation.toMatrix();
	mtx.setTranslation(transform.position);
	mtx.multiply3x3(transform.scale);
	return mtx;
}


void Universe::setPosition(Entity entity, float x, float y, float z)
{
	auto& transform = m_entities[entity.index];
	transform.position.set(x, y, z);
	entityTransformed().invoke(entity);
}


void Universe::setPosition(Entity entity, const Vec3& pos)
{
	auto& transform = m_entities[entity.index];
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
	while (m_entities.size() <= entity.index)
	{
		EntityData& data = m_entities.emplace();
		data.valid = false;
		data.prev = -1;
		data.next = m_first_free_slot;
		data.scale = -1;
		if (m_first_free_slot >= 0)
		{
			m_entities[m_first_free_slot].prev = m_entities.size() - 1;
		}
		m_first_free_slot = m_entities.size() - 1;
	}
	if (m_first_free_slot == entity.index)
	{
		m_first_free_slot = m_entities[entity.index].next;
	}
	if (m_entities[entity.index].prev >= 0)
	{
		m_entities[m_entities[entity.index].prev].next = m_entities[entity.index].next;
	}
	if (m_entities[entity.index].next >= 0)
	{
		m_entities[m_entities[entity.index].next].prev= m_entities[entity.index].prev;
	}
	EntityData& data = m_entities[entity.index];
	data.position.set(0, 0, 0);
	data.rotation.set(0, 0, 0, 1);
	data.scale = 1;
	data.components = 0;
	data.valid = true;
	m_entity_created.invoke(entity);
}


Entity Universe::createEntity(const Vec3& position, const Quat& rotation)
{
	EntityData* data;
	Entity entity;
	if (m_first_free_slot >= 0)
	{
		data = &m_entities[m_first_free_slot];
		entity.index = m_first_free_slot;
		if (data->next >= 0) m_entities[data->next].prev = -1;
		m_first_free_slot = data->next;
	}
	else
	{
		entity.index = m_entities.size();
		data = &m_entities.emplace();
	}
	data->position = position;
	data->rotation = rotation;
	data->scale = 1;
	data->components = 0;
	data->valid = true;
	m_entity_created.invoke(entity);

	return entity;
}


void Universe::destroyEntity(Entity entity)
{
	if (!isValid(entity) || entity.index < 0) return;

	u64 mask = m_entities[entity.index].components;
	for (int i = 0; i < MAX_COMPONENTS_TYPES_COUNT; ++i)
	{
		if ((mask & ((u64)1 << i)) != 0)
		{
			ComponentType type = {i};
			auto original_mask = mask;
			IScene* scene = m_component_type_map[i].scene;
			scene->destroyComponent(scene->getComponent(entity, type), type);
			mask = m_entities[entity.index].components;
			ASSERT(original_mask != mask);
		}
	}

	m_entities[entity.index].next = m_first_free_slot;
	m_entities[entity.index].prev = -1;
	m_entities[entity.index].valid = false;
	if (m_first_free_slot >= 0)
	{
		m_entities[m_first_free_slot].prev = entity.index;
	}

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


Entity Universe::getFirstEntity()
{
	for (int i = 0; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return {i};
	}
	return INVALID_ENTITY;
}


Entity Universe::getNextEntity(Entity entity)
{
	for (int i = entity.index + 1; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return {i};
	}
	return INVALID_ENTITY;
}


void Universe::serializeComponent(ISerializer& serializer, ComponentType type, ComponentHandle cmp)
{
	auto* scene = m_component_type_map[type.index].scene;
	auto& method = m_component_type_map[type.index].serialize;
	(scene->*method)(serializer, cmp);
}


void Universe::deserializeComponent(IDeserializer& serializer, Entity entity, ComponentType type, int scene_version)
{
	auto* scene = m_component_type_map[type.index].scene;
	auto& method = m_component_type_map[type.index].deserialize;
	(scene->*method)(serializer, entity, scene_version);
}


void Universe::serialize(OutputBlob& serializer)
{
	serializer.write((i32)m_entities.size());
	serializer.write(&m_entities[0], sizeof(m_entities[0]) * m_entities.size());
	serializer.write((i32)m_id_to_name_map.size());
	for (int i = 0, c = m_id_to_name_map.size(); i < c; ++i)
	{
		serializer.write(m_id_to_name_map.getKey(i));
		serializer.writeString(m_id_to_name_map.at(i).c_str());
	}
	serializer.write(m_first_free_slot);
}


void Universe::deserialize(InputBlob& serializer)
{
	i32 count;
	serializer.read(count);
	m_entities.resize(count);
	for (auto& i : m_entities) i.components = 0;

	serializer.read(&m_entities[0], sizeof(m_entities[0]) * m_entities.size());

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
}


struct PrefabEntityGUIDMap : public IEntityGUIDMap
{
	Entity get(EntityGUID guid) override { return{ (int)guid.value }; }


	EntityGUID get(Entity entity) override { return{ (u64)entity.index }; }


	void insert(EntityGUID guid, Entity entity) {}



};


void Universe::instantiatePrefab(const PrefabResource& prefab,
	const Vec3& pos,
	const Quat& rot,
	float scale,
	Array<Entity>& entities)
{
	InputBlob blob(prefab.blob.getData(), prefab.blob.getPos());
	PrefabEntityGUIDMap entity_map;
	TextDeserializer deserializer(blob, entity_map);
	while (blob.getPosition() < blob.getSize())
	{
		u64 prefab;
		deserializer.read(&prefab);
		Entity entity = createEntity(pos, rot);
		entities.push(entity);
		setScale(entity, scale);
		u32 cmp_type_hash;
		deserializer.read(&cmp_type_hash);
		while (cmp_type_hash != 0)
		{
			ComponentType cmp_type = PropertyRegister::getComponentTypeFromHash(cmp_type_hash);
			int scene_version;
			deserializer.read(&scene_version);
			deserializeComponent(deserializer, entity, cmp_type, scene_version);
			deserializer.read(&cmp_type_hash);
		}
	}
}


void Universe::setScale(Entity entity, float scale)
{
	auto& transform = m_entities[entity.index];
	transform.scale = scale;
	entityTransformed().invoke(entity);
}


float Universe::getScale(Entity entity)
{
	auto& transform = m_entities[entity.index];
	return transform.scale;
}


ComponentUID Universe::getFirstComponent(Entity entity) const
{
	u64 mask = m_entities[entity.index].components;
	for (int i = 0; i < MAX_COMPONENTS_TYPES_COUNT; ++i)
	{
		if ((mask & (u64(1) << i)) != 0)
		{
			IScene* scene = m_component_type_map[i].scene;
			return ComponentUID(entity, {i}, scene, scene->getComponent(entity, {i}));
		}
	}
	return ComponentUID::INVALID;
}


ComponentUID Universe::getNextComponent(const ComponentUID& cmp) const
{
	u64 mask = m_entities[cmp.entity.index].components;
	for (int i = cmp.type.index + 1; i < MAX_COMPONENTS_TYPES_COUNT; ++i)
	{
		if ((mask & (u64(1) << i)) != 0)
		{
			IScene* scene = m_component_type_map[i].scene;
			return ComponentUID(cmp.entity, {i}, scene, scene->getComponent(cmp.entity, {i}));
		}
	}
	return ComponentUID::INVALID;
}


ComponentUID Universe::getComponent(Entity entity, ComponentType component_type) const
{
	u64 mask = m_entities[entity.index].components;
	if ((mask & (u64(1) << component_type.index)) == 0) return ComponentUID::INVALID;
	IScene* scene = m_component_type_map[component_type.index].scene;
	return ComponentUID(entity, component_type, scene, scene->getComponent(entity, component_type));
}


bool Universe::hasComponent(Entity entity, ComponentType component_type) const
{
	u64 mask = m_entities[entity.index].components;
	return (mask & (u64(1) << component_type.index)) != 0;
}


void Universe::destroyComponent(Entity entity, ComponentType component_type, IScene* scene, ComponentHandle index)
{
	auto mask = m_entities[entity.index].components;
	auto old_mask = mask;
	mask &= ~((u64)1 << component_type.index);
	auto x = PropertyRegister::getComponentTypeID(component_type.index);
	ASSERT(old_mask != mask);
	m_entities[entity.index].components = mask;
	m_component_destroyed.invoke(ComponentUID(entity, component_type, scene, index));
}


void Universe::addComponent(Entity entity, ComponentType component_type, IScene* scene, ComponentHandle index)
{
	ComponentUID cmp(entity, component_type, scene, index);
	m_entities[entity.index].components |= (u64)1 << component_type.index;
	m_component_added.invoke(cmp);
}


bool Universe::nameExists(const char* name) const
{
	return m_name_to_id_map.find(crc32(name)) != -1;
}


} // namespace Lumix
