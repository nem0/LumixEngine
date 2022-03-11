#include "universe.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/plugin.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/string.h"


namespace Lumix
{

static constexpr int RESERVED_ENTITIES_COUNT = 1024;

const ComponentUID ComponentUID::INVALID(INVALID_ENTITY, { -1 }, 0);

EntityMap::EntityMap(IAllocator& allocator) 
	: m_map(allocator)
{}

void EntityMap::reserve(u32 count) {
	m_map.reserve(count);
}

EntityPtr EntityMap::get(EntityPtr e) const {
	return e.isValid() && e.index < m_map.size() ? m_map[e.index] : INVALID_ENTITY;
}

EntityRef EntityMap::get(EntityRef e) const {
	return (EntityRef)m_map[e.index];
}

void EntityMap::set(EntityRef src, EntityRef dst) {
	while (m_map.size() <= src.index) {
		m_map.push(INVALID_ENTITY);
	}
	m_map[src.index] = dst;
}


Universe::~Universe() = default;


Universe::Universe(Engine& engine, IAllocator& allocator)
	: m_allocator(allocator)
	, m_engine(engine)
	, m_names(m_allocator)
	, m_entities(m_allocator)
	, m_component_added(m_allocator)
	, m_component_destroyed(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_moved(m_allocator)
	, m_entity_created(m_allocator)
	, m_first_free_slot(-1)
	, m_scenes(m_allocator)
	, m_hierarchy(m_allocator)
	, m_transforms(m_allocator)
	, m_name("")
{
	m_entities.reserve(RESERVED_ENTITIES_COUNT);
	m_transforms.reserve(RESERVED_ENTITIES_COUNT);
	memset(m_component_type_map, 0, sizeof(m_component_type_map));
}


IScene* Universe::getScene(ComponentType type) const {
	return m_component_type_map[type.index].scene;
}


IScene* Universe::getScene(const char* name) const
{
	for (auto& scene : m_scenes)
	{
		if (equalStrings(scene->getPlugin().getName(), name))
		{
			return scene.get();
		}
	}
	return nullptr;
}


Array<UniquePtr<IScene>>& Universe::getScenes()
{
	return m_scenes;
}


void Universe::addScene(UniquePtr<IScene>&& scene)
{
	const u32 hash = crc32(scene->getPlugin().getName());
	for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
		if (cmp.scene == hash) {
			m_component_type_map[cmp.cmp->component_type.index].scene = scene.get();
			m_component_type_map[cmp.cmp->component_type.index].create = cmp.cmp->creator;
			m_component_type_map[cmp.cmp->component_type.index].destroy = cmp.cmp->destroyer;
		}
	}

	m_scenes.push(scene.move());
}


const DVec3& Universe::getPosition(EntityRef entity) const
{
	return m_transforms[entity.index].pos;
}


const Quat& Universe::getRotation(EntityRef entity) const
{
	return m_transforms[entity.index].rot;
}


void Universe::transformEntity(EntityRef entity, bool update_local)
{
	const int hierarchy_idx = m_entities[entity.index].hierarchy;
	m_entity_moved.invoke(entity);
	if (hierarchy_idx >= 0) {
		Hierarchy& h = m_hierarchy[hierarchy_idx];
		const Transform my_transform = getTransform(entity);
		if (update_local && h.parent.isValid()) {
			const Transform parent_tr = getTransform((EntityRef)h.parent);
			h.local_transform = (parent_tr.inverted() * my_transform);
		}

		EntityPtr child = h.first_child;
		while (child.isValid())
		{
			const Hierarchy& child_h = m_hierarchy[m_entities[child.index].hierarchy];
			const Transform abs_tr = my_transform * child_h.local_transform;
			Transform& child_data = m_transforms[child.index];
			child_data = abs_tr;
			transformEntity((EntityRef)child, false);

			child = child_h.next_sibling;
		}
	}
}


void Universe::setRotation(EntityRef entity, const Quat& rot)
{
	m_transforms[entity.index].rot = rot;
	transformEntity(entity, true);
}


void Universe::setRotation(EntityRef entity, float x, float y, float z, float w)
{
	m_transforms[entity.index].rot.set(x, y, z, w);
	transformEntity(entity, true);
}


bool Universe::hasEntity(EntityRef entity) const
{
	return entity.index >= 0 && entity.index < m_entities.size() && m_entities[entity.index].valid;
}


void Universe::setTransformKeepChildren(EntityRef entity, const Transform& transform)
{
	Transform& tmp = m_transforms[entity.index];
	tmp = transform;
	
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	entityTransformed().invoke(entity);
	if (hierarchy_idx >= 0)
	{
		Hierarchy& h = m_hierarchy[hierarchy_idx];
		Transform my_transform = getTransform(entity);
		if (h.parent.isValid())
		{
			Transform parent_tr = getTransform((EntityRef)h.parent);
			h.local_transform = parent_tr.inverted() * my_transform;
		}

		EntityPtr child = h.first_child;
		while (child.isValid())
		{
			Hierarchy& child_h = m_hierarchy[m_entities[child.index].hierarchy];

			child_h.local_transform = my_transform.inverted() * getTransform((EntityRef)child);
			child = child_h.next_sibling;
		}
	}
}


void Universe::setTransform(EntityRef entity, const Transform& transform)
{
	Transform& tmp = m_transforms[entity.index];
	tmp = transform;
	transformEntity(entity, true);
}


void Universe::setTransform(EntityRef entity, const RigidTransform& transform)
{
	auto& tmp = m_transforms[entity.index];
	tmp.pos = transform.pos;
	tmp.rot = transform.rot;
	transformEntity(entity, true);
}


void Universe::setTransform(EntityRef entity, const DVec3& pos, const Quat& rot, float scale)
{
	auto& tmp = m_transforms[entity.index];
	tmp.pos = pos;
	tmp.rot = rot;
	tmp.scale = scale;
	transformEntity(entity, true);
}


const Transform& Universe::getTransform(EntityRef entity) const
{
	return m_transforms[entity.index];
}


Matrix Universe::getRelativeMatrix(EntityRef entity, const DVec3& base_pos) const
{
	const Transform& transform = m_transforms[entity.index];
	Matrix mtx = transform.rot.toMatrix();
	mtx.setTranslation(Vec3(transform.pos - base_pos));
	mtx.multiply3x3(transform.scale);
	return mtx;
}


void Universe::setPosition(EntityRef entity, const DVec3& pos)
{
	m_transforms[entity.index].pos = pos;
	transformEntity(entity, true);
}


void Universe::setEntityName(EntityRef entity, const char* name)
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


const char* Universe::getEntityName(EntityRef entity) const
{
	int name_idx = m_entities[entity.index].name;
	if (name_idx < 0) return "";
	return m_names[name_idx].name;
}


EntityPtr Universe::findByName(EntityPtr parent, const char* name)
{
	if (parent.isValid()) {
		int h_idx = m_entities[parent.index].hierarchy;
		if (h_idx < 0) return INVALID_ENTITY;

		EntityPtr e = m_hierarchy[h_idx].first_child;
		while (e.isValid()) {
			const EntityData& data = m_entities[e.index];
			int name_idx = data.name;
			if (name_idx >= 0) {
				if (equalStrings(m_names[name_idx].name, name)) return e;
			}
			e = m_hierarchy[data.hierarchy].next_sibling;
		}
	}
	else
	{
		for (int i = 0, c = m_names.size(); i < c; ++i) {
			if (equalStrings(m_names[i].name, name)) {
				const EntityData& data = m_entities[m_names[i].entity.index];
				if (data.hierarchy < 0) return m_names[i].entity;
				if (!m_hierarchy[data.hierarchy].parent.isValid()) return m_names[i].entity;
			}
		}
	}

	return INVALID_ENTITY;
}


void Universe::emplaceEntity(EntityRef entity)
{
	while (m_entities.size() <= entity.index)
	{
		EntityData& data = m_entities.emplace();
		Transform& tr = m_transforms.emplace();
		data.valid = false;
		data.prev = -1;
		data.name = -1;
		data.hierarchy = -1;
		data.next = m_first_free_slot;
		tr.scale = -1;
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
	Transform& tr = m_transforms[entity.index];
	tr.pos = DVec3(0, 0, 0);
	tr.rot.set(0, 0, 0, 1);
	tr.scale = 1;
	data.name = -1;
	data.hierarchy = -1;
	data.components = 0;
	data.valid = true;

	m_entity_created.invoke(entity);
}


EntityRef Universe::createEntity(const DVec3& position, const Quat& rotation)
{
	EntityData* data;
	EntityRef entity;
	Transform* tr;
	if (m_first_free_slot >= 0)
	{
		data = &m_entities[m_first_free_slot];
		tr = &m_transforms[m_first_free_slot];
		entity.index = m_first_free_slot;
		if (data->next >= 0) m_entities[data->next].prev = -1;
		m_first_free_slot = data->next;
	}
	else
	{
		entity.index = m_entities.size();
		data = &m_entities.emplace();
		tr = &m_transforms.emplace();
	}
	tr->pos = position;
	tr->rot = rotation;
	tr->scale = 1;
	data->name = -1;
	data->hierarchy = -1;
	data->components = 0;
	data->valid = true;
	m_entity_created.invoke(entity);

	return entity;
}


void Universe::destroyEntity(EntityRef entity)
{
	EntityData& entity_data = m_entities[entity.index];
	ASSERT(entity_data.valid);
	for (EntityPtr first_child = getFirstChild(entity); first_child.isValid(); first_child = getFirstChild(entity))
	{
		setParent(INVALID_ENTITY, (EntityRef)first_child);
	}
	setParent(INVALID_ENTITY, entity);
	

	u64 mask = entity_data.components;
	for (int i = 0; i < ComponentType::MAX_TYPES_COUNT; ++i)
	{
		if ((mask & ((u64)1 << i)) != 0)
		{
			auto original_mask = mask;
			IScene* scene = m_component_type_map[i].scene;
			auto destroy_method = m_component_type_map[i].destroy;
			destroy_method(scene, entity);
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
		m_names.swapAndPop(entity_data.name);
		entity_data.name = -1;
	}

	m_first_free_slot = entity.index;
	m_entity_destroyed.invoke(entity);
}


EntityPtr Universe::getFirstEntity() const
{
	for (int i = 0; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return EntityPtr{i};
	}
	return INVALID_ENTITY;
}


EntityPtr Universe::getNextEntity(EntityRef entity) const
{
	for (int i = entity.index + 1; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return EntityPtr{i};
	}
	return INVALID_ENTITY;
}


EntityPtr Universe::getParent(EntityRef entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].parent;
}


EntityPtr Universe::getFirstChild(EntityRef entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].first_child;
}


EntityPtr Universe::getNextSibling(EntityRef entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].next_sibling;
}


bool Universe::isDescendant(EntityRef ancestor, EntityRef descendant) const
{
	for(EntityPtr e = getFirstChild(ancestor); e.isValid(); e = getNextSibling((EntityRef)e))
	{
		if (e == descendant) return true;
		if (isDescendant((EntityRef)e, descendant)) return true;
	}

	return false;
}


void Universe::setParent(EntityPtr new_parent, EntityRef child)
{
	bool would_create_cycle = new_parent.isValid() && isDescendant(child, (EntityRef)new_parent);
	if (would_create_cycle)
	{
		logError("Hierarchy can not contains a cycle.");
		return;
	}

	auto collectGarbage = [this](EntityRef entity) {
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
		EntityPtr old_parent = m_hierarchy[child_idx].parent;

		if (old_parent.isValid())
		{
			Hierarchy& old_parent_h = m_hierarchy[m_entities[old_parent.index].hierarchy];
			EntityPtr* x = &old_parent_h.first_child;
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
			collectGarbage((EntityRef)old_parent);
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
			h.entity = (EntityRef)new_parent;
			h.parent = INVALID_ENTITY;
			h.first_child = INVALID_ENTITY;
			h.next_sibling = INVALID_ENTITY;
		}

		m_hierarchy[child_idx].parent = new_parent;
		Transform parent_tr = getTransform((EntityRef)new_parent);
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


void Universe::updateGlobalTransform(EntityRef entity)
{
	const Hierarchy& h = m_hierarchy[m_entities[entity.index].hierarchy];
	ASSERT(h.parent.isValid());
	Transform parent_tr = getTransform((EntityRef)h.parent);
	
	Transform new_tr = parent_tr * h.local_transform;
	setTransform(entity, new_tr);
}


void Universe::setLocalPosition(EntityRef entity, const DVec3& pos)
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


void Universe::setLocalRotation(EntityRef entity, const Quat& rot)
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


void Universe::setLocalTransform(EntityRef entity, const Transform& transform)
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


Transform Universe::getLocalTransform(EntityRef entity) const
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		return getTransform(entity);
	}

	return m_hierarchy[hierarchy_idx].local_transform;
}


float Universe::getLocalScale(EntityRef entity) const
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		return getScale(entity);
	}

	return m_hierarchy[hierarchy_idx].local_transform.scale;
}


void Universe::serialize(OutputMemoryStream& serializer)
{
	serializer.write((u32)m_entities.size());

	for (u32 i = 0, c = m_entities.size(); i < c; ++i) {
		if (!m_entities[i].valid) continue;
		const EntityRef e = {(i32)i};
		serializer.write(e);
		serializer.write(m_transforms[i]);
	}
	serializer.write(INVALID_ENTITY);

	serializer.write((u32)m_names.size());
	for (const EntityName& name : m_names) {
		serializer.write(name.entity);
		serializer.writeString(name.name);
	}

	serializer.write((u32)m_hierarchy.size());
	if (!m_hierarchy.empty()) serializer.write(&m_hierarchy[0], m_hierarchy.byte_size());
}

void Universe::setName(const char* name) { 
	copyString(m_name, name);
}

void Universe::deserialize(InputMemoryStream& serializer, EntityMap& entity_map)
{
	u32 to_reserve;
	serializer.read(to_reserve);
	entity_map.reserve(to_reserve);

	for (EntityPtr e = serializer.read<EntityPtr>(); e.isValid(); e = serializer.read<EntityPtr>()) {
		EntityRef orig = (EntityRef)e;
		const EntityRef new_e = createEntity({0, 0, 0}, {0, 0, 0, 1});
		entity_map.set(orig, new_e);
		serializer.read(m_transforms[new_e.index]);
	}

	u32 count;
	serializer.read(count);
	for (u32 i = 0; i < count; ++i) {
		EntityName& name = m_names.emplace();
		serializer.read(name.entity);
		name.entity = entity_map.get(name.entity);
		copyString(name.name, serializer.readString());
		m_entities[name.entity.index].name = m_names.size() - 1;
	}

	serializer.read(count);
	const u32 old_count = m_hierarchy.size();
	m_hierarchy.resize(count + old_count);
	if (count > 0) {
		serializer.read(&m_hierarchy[old_count], sizeof(m_hierarchy[0]) * count);

		for (u32 i = old_count; i < count + old_count; ++i) {
			m_hierarchy[i].entity = entity_map.get(m_hierarchy[i].entity);
			m_hierarchy[i].first_child = entity_map.get(m_hierarchy[i].first_child);
			m_hierarchy[i].next_sibling = entity_map.get(m_hierarchy[i].next_sibling);
			m_hierarchy[i].parent = entity_map.get(m_hierarchy[i].parent);
			m_entities[m_hierarchy[i].entity.index].hierarchy = i;
		}
	}
}


void Universe::setScale(EntityRef entity, float scale)
{
	m_transforms[entity.index].scale = scale;
	transformEntity(entity, true);
}


float Universe::getScale(EntityRef entity) const
{
	return m_transforms[entity.index].scale;
}


ComponentUID Universe::getFirstComponent(EntityRef entity) const
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


ComponentUID Universe::getComponent(EntityRef entity, ComponentType component_type) const
{
	u64 mask = m_entities[entity.index].components;
	if ((mask & (u64(1) << component_type.index)) == 0) return ComponentUID::INVALID;
	IScene* scene = m_component_type_map[component_type.index].scene;
	return ComponentUID(entity, component_type, scene);
}


u64 Universe::getComponentsMask(EntityRef entity) const
{
	return m_entities[entity.index].components;
}


bool Universe::hasComponent(EntityRef entity, ComponentType component_type) const
{
	u64 mask = m_entities[entity.index].components;
	return (mask & (u64(1) << component_type.index)) != 0;
}


void Universe::onComponentDestroyed(EntityRef entity, ComponentType component_type, IScene* scene)
{
	auto mask = m_entities[entity.index].components;
	auto old_mask = mask;
	mask &= ~((u64)1 << component_type.index);
	ASSERT(old_mask != mask);
	m_entities[entity.index].components = mask;
	m_component_destroyed.invoke(ComponentUID(entity, component_type, scene));
}


void Universe::createComponent(ComponentType type, EntityRef entity)
{
	IScene* scene = m_component_type_map[type.index].scene;
	auto& create_method = m_component_type_map[type.index].create;
	create_method(scene, entity);
}


void Universe::destroyComponent(EntityRef entity, ComponentType type)
{
	IScene* scene = m_component_type_map[type.index].scene;
	auto& destroy_method = m_component_type_map[type.index].destroy;
	destroy_method(scene, entity);
}


void Universe::onComponentCreated(EntityRef entity, ComponentType component_type, IScene* scene)
{
	ComponentUID cmp(entity, component_type, scene);
	m_entities[entity.index].components |= (u64)1 << component_type.index;
	m_component_added.invoke(cmp);
}


} // namespace Lumix
