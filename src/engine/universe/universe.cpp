#include "universe.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/iplugin.h"
#include "engine/log.h"
#include "engine/matrix.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/serializer.h"
#include "engine/universe/component.h"


namespace Lumix
{


static const int RESERVED_ENTITIES_COUNT = 5000;


Universe::~Universe() = default;


Universe::Universe(IAllocator& allocator)
	: m_allocator(allocator)
	, m_names(m_allocator)
	, m_entities(m_allocator)
	, m_component_added(m_allocator)
	, m_component_destroyed(m_allocator)
	, m_entity_created(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_moved(m_allocator)
	, m_first_free_slot(-1)
	, m_scenes(m_allocator)
	, m_hierarchy(m_allocator)
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


void Universe::transformEntity(Entity entity, bool update_local)
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	entityTransformed().invoke(entity);
	if (hierarchy_idx >= 0)
	{
		Hierarchy& h = m_hierarchy[hierarchy_idx];
		Transform my_transform = getTransform(entity);
		if (update_local && h.parent.isValid())
		{
			Transform parent_tr = getTransform(h.parent);
			h.local_transform = (parent_tr.inverted() * my_transform);
		}

		Entity child = h.first_child;
		while (child.isValid())
		{
			Hierarchy& child_h = m_hierarchy[m_entities[child.index].hierarchy];
			Transform abs_tr = my_transform * child_h.local_transform;
			EntityData& child_data = m_entities[child.index];
			child_data.position = abs_tr.pos;
			child_data.rotation = abs_tr.rot;
			child_data.scale = abs_tr.scale;
			transformEntity(child, false);

			child = child_h.next_sibling;
		}
	}
}


void Universe::setRotation(Entity entity, const Quat& rot)
{
	m_entities[entity.index].rotation = rot;
	transformEntity(entity, true);
}


void Universe::setRotation(Entity entity, float x, float y, float z, float w)
{
	m_entities[entity.index].rotation.set(x, y, z, w);
	transformEntity(entity, true);
}


bool Universe::hasEntity(Entity entity) const
{
	return entity.index >= 0 && entity.index < m_entities.size() && m_entities[entity.index].valid;
}


void Universe::setMatrix(Entity entity, const Matrix& mtx)
{
	EntityData& out = m_entities[entity.index];
	mtx.decompose(out.position, out.rotation, out.scale);
	transformEntity(entity, true);
}


Matrix Universe::getPositionAndRotation(Entity entity) const
{
	auto& transform = m_entities[entity.index];
	Matrix mtx = transform.rotation.toMatrix();
	mtx.setTranslation(transform.position);
	return mtx;
}


void Universe::setTransformKeepChildren(Entity entity, const Transform& transform)
{
	auto& tmp = m_entities[entity.index];
	tmp.position = transform.pos;
	tmp.rotation = transform.rot;
	tmp.scale = transform.scale;
	
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	entityTransformed().invoke(entity);
	if (hierarchy_idx >= 0)
	{
		Hierarchy& h = m_hierarchy[hierarchy_idx];
		Transform my_transform = getTransform(entity);
		if (h.parent.isValid())
		{
			Transform parent_tr = getTransform(h.parent);
			h.local_transform = parent_tr.inverted() * my_transform;
		}

		Entity child = h.first_child;
		while (child.isValid())
		{
			Hierarchy& child_h = m_hierarchy[m_entities[child.index].hierarchy];

			child_h.local_transform = my_transform.inverted() * getTransform(child);
			child = child_h.next_sibling;
		}
	}

}


void Universe::setTransform(Entity entity, const Transform& transform)
{
	auto& tmp = m_entities[entity.index];
	tmp.position = transform.pos;
	tmp.rotation = transform.rot;
	tmp.scale = transform.scale;
	transformEntity(entity, true);
}


void Universe::setTransform(Entity entity, const RigidTransform& transform)
{
	auto& tmp = m_entities[entity.index];
	tmp.position = transform.pos;
	tmp.rotation = transform.rot;
	transformEntity(entity, true);
}


void Universe::setTransform(Entity entity, const Vec3& pos, const Quat& rot, float scale)
{
	auto& tmp = m_entities[entity.index];
	tmp.position = pos;
	tmp.rotation = rot;
	tmp.scale = scale;
	transformEntity(entity, true);
}


Transform Universe::getTransform(Entity entity) const
{
	auto& transform = m_entities[entity.index];
	return {transform.position, transform.rotation, transform.scale};
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
	transformEntity(entity, true);
}


void Universe::setPosition(Entity entity, const Vec3& pos)
{
	auto& transform = m_entities[entity.index];
	transform.position = pos;
	transformEntity(entity, true);
}


void Universe::setEntityName(Entity entity, const char* name)
{
	int name_idx = m_entities[entity.index].name;
	if (name_idx < 0)
	{
		if (name[0] == '\0') return;
		m_entities[entity.index].name = m_names.size();
		EntityName& name_data = m_names.emplace();
		name_data.entity = entity;
		copyString(name_data.name, name);
	}
	else
	{
		copyString(m_names[name_idx].name, name);
	}
}


const char* Universe::getEntityName(Entity entity) const
{
	int name_idx = m_entities[entity.index].name;
	if (name_idx < 0) return "";
	return m_names[name_idx].name;
}


Entity Universe::getEntityByName(const char* name)
{
	for (const EntityName& name_data : m_names)
	{
		if (equalStrings(name_data.name, name)) return name_data.entity;
	}
	return INVALID_ENTITY;
}


void Universe::emplaceEntity(Entity entity)
{
	while (m_entities.size() <= entity.index)
	{
		EntityData& data = m_entities.emplace();
		data.valid = false;
		data.prev = -1;
		data.name = -1;
		data.hierarchy = -1;
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
	data.name = -1;
	data.hierarchy = -1;
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
	data->name = -1;
	data->hierarchy = -1;
	data->components = 0;
	data->valid = true;
	m_entity_created.invoke(entity);

	return entity;
}


void Universe::destroyEntity(Entity entity)
{
	if (!entity.isValid()) return;
	
	EntityData& entity_data = m_entities[entity.index];
	ASSERT(entity_data.valid);
	for (Entity first_child = getFirstChild(entity); first_child.isValid(); first_child = getFirstChild(entity))
	{
		destroyEntity(first_child);
	}
	setParent(INVALID_ENTITY, entity);
	

	u64 mask = entity_data.components;
	for (int i = 0; i < ComponentType::MAX_TYPES_COUNT; ++i)
	{
		if ((mask & ((u64)1 << i)) != 0)
		{
			ComponentType type = {i};
			auto original_mask = mask;
			IScene* scene = m_component_type_map[i].scene;
			auto destroy_method = m_component_type_map[i].destroy;
			(scene->*destroy_method)(entity);
			mask = entity_data.components;
			ASSERT(original_mask != mask);
		}
	}

	entity_data.next = m_first_free_slot;
	entity_data.prev = -1;
	entity_data.hierarchy = -1;
	
	entity_data.valid = false;
	if (m_first_free_slot >= 0)
	{
		m_entities[m_first_free_slot].prev = entity.index;
	}

	if (entity_data.name >= 0)
	{
		m_entities[m_names.back().entity.index].name = entity_data.name;
		m_names.eraseFast(entity_data.name);
		entity_data.name = -1;
	}

	m_first_free_slot = entity.index;
	m_entity_destroyed.invoke(entity);
}


Entity Universe::getFirstEntity() const
{
	for (int i = 0; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return {i};
	}
	return INVALID_ENTITY;
}


Entity Universe::getNextEntity(Entity entity) const
{
	for (int i = entity.index + 1; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return {i};
	}
	return INVALID_ENTITY;
}


Entity Universe::getParent(Entity entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].parent;
}


Entity Universe::getFirstChild(Entity entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].first_child;
}


Entity Universe::getNextSibling(Entity entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].next_sibling;
}


bool Universe::isDescendant(Entity ancestor, Entity descendant) const
{
	if (!ancestor.isValid()) return false;
	for(Entity e = getFirstChild(ancestor); e.isValid(); e = getNextSibling(e))
	{
		if (e == descendant) return true;
		if (isDescendant(e, descendant)) return true;
	}

	return false;
}


void Universe::setParent(Entity new_parent, Entity child)
{
	bool would_create_cycle = isDescendant(child, new_parent);
	if (would_create_cycle)
	{
		g_log_error.log("Engine") << "Hierarchy can not contains a cycle.";
		return;
	}

	auto collectGarbage = [this](Entity entity) {
		Hierarchy& h = m_hierarchy[m_entities[entity.index].hierarchy];
		if (h.parent.isValid()) return;
		if (h.first_child.isValid()) return;

		const Hierarchy& last = m_hierarchy.back();
		m_entities[last.entity.index].hierarchy = m_entities[entity.index].hierarchy;
		m_entities[entity.index].hierarchy = -1;
		h = last;
		m_hierarchy.pop();
	};

	int child_idx = m_entities[child.index].hierarchy;
	
	if (child_idx >= 0)
	{
		Entity old_parent = m_hierarchy[child_idx].parent;

		if (old_parent.isValid())
		{
			Hierarchy& old_parent_h = m_hierarchy[m_entities[old_parent.index].hierarchy];
			Entity* x = &old_parent_h.first_child;
			while (x->isValid())
			{
				if (*x == child)
				{
					*x = getNextSibling(child);
					break;
				}
				x = &m_hierarchy[m_entities[x->index].hierarchy].next_sibling;
			}
			m_hierarchy[child_idx].parent = INVALID_ENTITY;
			m_hierarchy[child_idx].next_sibling = INVALID_ENTITY;
			collectGarbage(old_parent);
			child_idx = m_entities[child.index].hierarchy;
		}
	}
	else if(new_parent.isValid())
	{
		child_idx = m_hierarchy.size();
		m_entities[child.index].hierarchy = child_idx;
		Hierarchy& h = m_hierarchy.emplace();
		h.entity = child;
		h.parent = INVALID_ENTITY;
		h.first_child = INVALID_ENTITY;
		h.next_sibling = INVALID_ENTITY;
	}

	if (new_parent.isValid())
	{
		int new_parent_idx = m_entities[new_parent.index].hierarchy;
		if (new_parent_idx < 0)
		{
			new_parent_idx = m_hierarchy.size();
			m_entities[new_parent.index].hierarchy = new_parent_idx;
			Hierarchy& h = m_hierarchy.emplace();
			h.entity = new_parent;
			h.parent = INVALID_ENTITY;
			h.first_child = INVALID_ENTITY;
			h.next_sibling = INVALID_ENTITY;
		}

		m_hierarchy[child_idx].parent = new_parent;
		Transform parent_tr = getTransform(new_parent);
		Transform child_tr = getTransform(child);
		m_hierarchy[child_idx].local_transform = parent_tr.inverted() * child_tr;
		m_hierarchy[child_idx].next_sibling = m_hierarchy[new_parent_idx].first_child;
		m_hierarchy[new_parent_idx].first_child = child;
	}
	else
	{
		if (child_idx >= 0) collectGarbage(child);
	}
}


void Universe::updateGlobalTransform(Entity entity)
{
	const Hierarchy& h = m_hierarchy[m_entities[entity.index].hierarchy];
	Transform parent_tr = getTransform(h.parent);
	
	Transform new_tr = parent_tr * h.local_transform;
	setTransform(entity, new_tr);
}


void Universe::setLocalPosition(Entity entity, const Vec3& pos)
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		setPosition(entity, pos);
		return;
	}

	m_hierarchy[hierarchy_idx].local_transform.pos = pos;
	updateGlobalTransform(entity);
}


void Universe::setLocalRotation(Entity entity, const Quat& rot)
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		setRotation(entity, rot);
		return;
	}
	m_hierarchy[hierarchy_idx].local_transform.rot = rot;
	updateGlobalTransform(entity);
}


Transform Universe::computeLocalTransform(Entity parent, const Transform& global_transform) const
{
	Transform parent_tr = getTransform(parent);
	return parent_tr.inverted() * global_transform;
}


void Universe::setLocalTransform(Entity entity, const Transform& transform)
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		setTransform(entity, transform);
		return;
	}

	Hierarchy& h = m_hierarchy[hierarchy_idx];
	h.local_transform = transform;
	updateGlobalTransform(entity);
}


Transform Universe::getLocalTransform(Entity entity) const
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		return getTransform(entity);
	}

	return m_hierarchy[hierarchy_idx].local_transform;
}


float Universe::getLocalScale(Entity entity) const
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		return getScale(entity);
	}

	return m_hierarchy[hierarchy_idx].local_transform.scale;
}



void Universe::serializeComponent(ISerializer& serializer, ComponentType type, Entity entity)
{
	auto* scene = m_component_type_map[type.index].scene;
	auto& method = m_component_type_map[type.index].serialize;
	(scene->*method)(serializer, entity);
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
	serializer.write((i32)m_names.size());
	for (const EntityName& name : m_names)
	{
		serializer.write(name.entity);
		serializer.writeString(name.name);
	}
	serializer.write(m_first_free_slot);

	serializer.write(m_hierarchy.size());
	if(!m_hierarchy.empty()) serializer.write(&m_hierarchy[0], sizeof(m_hierarchy[0]) * m_hierarchy.size());
}


void Universe::deserialize(InputBlob& serializer)
{
	i32 count;
	serializer.read(count);
	m_entities.resize(count);

	if (count > 0) serializer.read(&m_entities[0], sizeof(m_entities[0]) * m_entities.size());

	serializer.read(count);
	for (int i = 0; i < count; ++i)
	{
		EntityName& name = m_names.emplace();
		serializer.read(name.entity);
		serializer.readString(name.name, lengthOf(name.name));
		m_entities[name.entity.index].name = m_names.size() - 1;
	}

	serializer.read(m_first_free_slot);

	serializer.read(count);
	m_hierarchy.resize(count);
	if (count > 0) serializer.read(&m_hierarchy[0], sizeof(m_hierarchy[0]) * m_hierarchy.size());
}


struct PrefabEntityGUIDMap : public ILoadEntityGUIDMap
{
	explicit PrefabEntityGUIDMap(const Array<Entity>& _entities)
		: entities(_entities)
	{
	}


	Entity get(EntityGUID guid) override
	{
		if (guid.value >= entities.size()) return INVALID_ENTITY;
		return entities[(int)guid.value];
	}


	const Array<Entity>& entities;
};


Entity Universe::instantiatePrefab(const PrefabResource& prefab,
	const Vec3& pos,
	const Quat& rot,
	float scale)
{
	InputBlob blob(prefab.blob.getData(), prefab.blob.getPos());
	Array<Entity> entities(m_allocator);
	PrefabEntityGUIDMap entity_map(entities);
	TextDeserializer deserializer(blob, entity_map);
	u32 version;
	deserializer.read(&version);
	if (version > (int)PrefabVersion::LAST)
	{
		g_log_error.log("Engine") << "Prefab " << prefab.getPath() << " has unsupported version.";
		return INVALID_ENTITY;
	}
	int count;
	deserializer.read(&count);
	int entity_idx = 0;
	entities.reserve(count);
	for (int i = 0; i < count; ++i)
	{
		entities.push(createEntity({0, 0, 0}, {0, 0, 0, 1}));
	}
	while (blob.getPosition() < blob.getSize() && entity_idx < count)
	{
		u64 prefab;
		deserializer.read(&prefab);
		Entity entity = entities[entity_idx];
		setTransform(entity, {pos, rot, scale});
		if (version > (int)PrefabVersion::WITH_HIERARCHY)
		{
			Entity parent;

			deserializer.read(&parent);
			if (parent.isValid())
			{
				RigidTransform local_tr;
				deserializer.read(&local_tr);
				float scale;
				deserializer.read(&scale);
				setParent(parent, entity);
				setLocalTransform(entity, {local_tr.pos, local_tr.rot, scale});
			}
		}
		u32 cmp_type_hash;
		deserializer.read(&cmp_type_hash);
		while (cmp_type_hash != 0)
		{
			ComponentType cmp_type = Reflection::getComponentTypeFromHash(cmp_type_hash);
			int scene_version;
			deserializer.read(&scene_version);
			deserializeComponent(deserializer, entity, cmp_type, scene_version);
			deserializer.read(&cmp_type_hash);
		}
		++entity_idx;
	}
	return entities[0];
}


void Universe::setScale(Entity entity, float scale)
{
	auto& transform = m_entities[entity.index];
	transform.scale = scale;
	transformEntity(entity, true);
}


float Universe::getScale(Entity entity) const
{
	auto& transform = m_entities[entity.index];
	return transform.scale;
}


ComponentUID Universe::getFirstComponent(Entity entity) const
{
	u64 mask = m_entities[entity.index].components;
	for (int i = 0; i < ComponentType::MAX_TYPES_COUNT; ++i)
	{
		if ((mask & (u64(1) << i)) != 0)
		{
			IScene* scene = m_component_type_map[i].scene;
			return ComponentUID(entity, {i}, scene);
		}
	}
	return ComponentUID::INVALID;
}


ComponentUID Universe::getNextComponent(const ComponentUID& cmp) const
{
	u64 mask = m_entities[cmp.entity.index].components;
	for (int i = cmp.type.index + 1; i < ComponentType::MAX_TYPES_COUNT; ++i)
	{
		if ((mask & (u64(1) << i)) != 0)
		{
			IScene* scene = m_component_type_map[i].scene;
			return ComponentUID(cmp.entity, {i}, scene);
		}
	}
	return ComponentUID::INVALID;
}


ComponentUID Universe::getComponent(Entity entity, ComponentType component_type) const
{
	u64 mask = m_entities[entity.index].components;
	if ((mask & (u64(1) << component_type.index)) == 0) return ComponentUID::INVALID;
	IScene* scene = m_component_type_map[component_type.index].scene;
	return ComponentUID(entity, component_type, scene);
}


bool Universe::hasComponent(Entity entity, ComponentType component_type) const
{
	u64 mask = m_entities[entity.index].components;
	return (mask & (u64(1) << component_type.index)) != 0;
}


void Universe::onComponentDestroyed(Entity entity, ComponentType component_type, IScene* scene)
{
	auto mask = m_entities[entity.index].components;
	auto old_mask = mask;
	mask &= ~((u64)1 << component_type.index);
	ASSERT(old_mask != mask);
	m_entities[entity.index].components = mask;
	m_component_destroyed.invoke(ComponentUID(entity, component_type, scene));
}


void Universe::createComponent(ComponentType type, Entity entity)
{
	IScene* scene = m_component_type_map[type.index].scene;
	auto& create_method = m_component_type_map[type.index].create;
	(scene->*create_method)(entity);
}


void Universe::destroyComponent(Entity entity, ComponentType type)
{
	IScene* scene = m_component_type_map[type.index].scene;
	auto& destroy_method = m_component_type_map[type.index].destroy;
	(scene->*destroy_method)(entity);
}


void Universe::onComponentCreated(Entity entity, ComponentType component_type, IScene* scene)
{
	ComponentUID cmp(entity, component_type, scene);
	m_entities[entity.index].components |= (u64)1 << component_type.index;
	m_component_added.invoke(cmp);
}


} // namespace Lumix
