#include "world.h"
#include "engine/engine.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/math.h"
#include "core/sort.h"
#include "engine/plugin.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "core/string.h"


namespace Lumix
{

static constexpr int RESERVED_ENTITIES_COUNT = 1024;

const ComponentUID ComponentUID::INVALID(INVALID_ENTITY, { -1 }, 0);


static constexpr u32 EMPTY_ARCHETYPE = 0;

// archetype is a unique set of component types
struct World::ArchetypeManager {
	struct Archetype {
		Archetype(IAllocator& allocator)
			: types(allocator)
		{}

		RuntimeHash32 hash;
		Array<ComponentType> types;
	};

	using ArchetypeHandle = u32;
	
	ArchetypeManager(IAllocator& allocator)
		: m_archetypes(allocator)
		, m_allocator(allocator)
	{
		m_archetypes.reserve(1024);
		m_archetypes.emplace(m_allocator); // 0-th archetype is reserved for invalid archetype
	}

	const Archetype& get(ArchetypeHandle handle) {
		return m_archetypes[handle];
	}

	bool hasComponent(ArchetypeHandle archetype, ComponentType type) {
		const Archetype& a = m_archetypes[archetype];
		for (ComponentType t : a.types) {
			if (t == type) return true;
		}
		return false;
	}

	ArchetypeHandle get(Span<ComponentType> types) {
		RollingHasher hasher;

		// we sort to get the same hash for different order
		sort(types.begin(), types.end(), [](const ComponentType& a, const ComponentType& b) { return a.index < b.index; });

		hasher.begin();
		for (ComponentType type : types) {
			hasher.update(&type, sizeof(type));
		}
		const RuntimeHash32 hash = hasher.end();

		for (u32 i = 0, c = m_archetypes.size(); i < c; ++i) {
			if (m_archetypes[i].hash == hash) {
				Archetype& a = m_archetypes[i];
				if (a.types.size() != types.length()) continue;
				bool equal = true;
				for (u32 j = 0; j < types.length(); ++j) {
					if (a.types[j] != types[j]) {
						equal = false;
						break;
					}
				}
				if (equal) return i;
			}
		}

		Archetype& a = m_archetypes.emplace(m_allocator);
		a.hash = hash;
		a.types.resize(types.length());
		memcpy(a.types.begin(), types.begin(), types.length() * sizeof(ComponentType));
		return m_archetypes.size() - 1;
	}

	IAllocator& m_allocator;
	Array<Archetype> m_archetypes;
};

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

World::~World() {
	// release modules first, since they can access world
	m_modules.clear();
}

World::World(Engine& engine)
	: m_allocator(engine.getAllocator(), "world")
	, m_engine(engine)
	, m_names(m_allocator)
	, m_entities(m_allocator)
	, m_component_added(m_allocator)
	, m_component_destroyed(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_created(m_allocator)
	, m_first_free_slot(-1)
	, m_modules(m_allocator)
	, m_hierarchy(m_allocator)
	, m_transforms(m_allocator)
	, m_partitions(m_allocator)
{
	m_archetype_manager = UniquePtr<ArchetypeManager>::create(m_allocator, m_allocator);
	m_entities.reserve(RESERVED_ENTITIES_COUNT);
	m_transforms.reserve(RESERVED_ENTITIES_COUNT);
	memset(m_component_type_map, 0, sizeof(m_component_type_map));

	PartitionHandle p = createPartition("");
	setActivePartition(p);

	const Array<ISystem*>& systems = engine.getSystemManager().getSystems();
	for (ISystem* system : systems) {
		system->createModules(*this);
	}

	for (UniquePtr<IModule>& module : m_modules) {
		module->init();
	}
}

World::PartitionHandle World::createPartition(const char* name) {
	ASSERT(sizeof(m_partition_generator) == 2 && m_partition_generator <= 0xffFF); // TODO handle reuse

	Partition& p = m_partitions.emplace();
	p.handle = m_partition_generator;
	++m_partition_generator;
	copyString(p.name, name);
	return p.handle;
}

void World::destroyPartition(PartitionHandle partition) {
	for (EntityData& e : m_entities) {
		if (!e.valid) continue;
		if (e.partition == partition) destroyEntity(EntityRef{i32(&e - m_entities.begin())});
	}
	m_partitions.eraseItems([&](const Partition& p){ return p.handle == partition; });
}

void World::setActivePartition(PartitionHandle partition) {
	m_active_partition = partition;
}

void World::setPartition(EntityRef entity, PartitionHandle partition) {
	m_entities[entity.index].partition = partition;
}

World::Partition& World::getPartition(PartitionHandle partition) {
	for (Partition& p : m_partitions) {
		if (p.handle == partition) return p;
	}
	ASSERT(false);
	return m_partitions[0];
}

World::PartitionHandle World::getPartition(EntityRef entity) {
	return m_entities[entity.index].partition;
}

IModule* World::getModule(ComponentType type) const {
	ComponentTypeEntry* entry = m_component_type_map[type.index].get();
	return entry ? entry->module : nullptr;
}


IModule* World::getModule(const char* name) const
{
	for (auto& module : m_modules)
	{
		if (equalStrings(module->getName(), name))
		{
			return module.get();
		}
	}
	return nullptr;
}


Array<UniquePtr<IModule>>& World::getModules()
{
	return m_modules;
}


void World::addModule(UniquePtr<IModule>&& module)
{
	const RuntimeHash hash(module->getName());
	for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
		if (cmp.module_hash == hash) {
			u32 i = cmp.cmp->component_type.index;
			if (!m_component_type_map[i].get()) m_component_type_map[i].create(m_allocator);
			m_component_type_map[i]->module = module.get();
			m_component_type_map[i]->create = cmp.cmp->creator;
			m_component_type_map[i]->destroy = cmp.cmp->destroyer;
		}
	}

	const char* name = module->getName();
	const i32 idx = m_modules.find([name](const UniquePtr<IModule>& m){ return equalStrings(m->getName(), name); });
	ASSERT(idx == -1);
	m_modules.push(module.move());
}


const DVec3& World::getPosition(EntityRef entity) const
{
	return m_transforms[entity.index].pos;
}


const Quat& World::getRotation(EntityRef entity) const
{
	return m_transforms[entity.index].rot;
}


DelegateList<void(EntityRef)>& World::componentTransformed(ComponentType type) {
	if (!m_component_type_map[type.index].get()) m_component_type_map[type.index].create(m_allocator);
	return m_component_type_map[type.index]->transformed;
}

void World::transformEntity(EntityRef entity, bool update_local)
{
	const ArchetypeManager::Archetype& archetype = m_archetype_manager->get(m_entities[entity.index].archetype);
	for (ComponentType type : archetype.types) {
		m_component_type_map[type.index]->transformed.invoke(entity);
	}
	
	const i32 hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx >= 0) {
		Hierarchy& h = m_hierarchy[hierarchy_idx];
		const Transform my_transform = getTransform(entity);
		if (update_local && h.parent.isValid()) {
			const Transform parent_tr = getTransform((EntityRef)h.parent);
			h.local_transform = Transform::computeLocal(parent_tr, my_transform);
		}

		EntityPtr child = h.first_child;
		while (child.isValid()) {
			const Hierarchy& child_h = m_hierarchy[m_entities[child.index].hierarchy];
			const Transform abs_tr = my_transform.compose(child_h.local_transform);
			Transform& child_data = m_transforms[child.index];
			child_data = abs_tr;
			transformEntity((EntityRef)child, false);

			child = child_h.next_sibling;
		}
	}
}


void World::setRotation(EntityRef entity, const Quat& rot)
{
	m_transforms[entity.index].rot = rot;
	transformEntity(entity, true);
}


void World::setRotation(EntityRef entity, float x, float y, float z, float w)
{
	m_transforms[entity.index].rot.set(x, y, z, w);
	transformEntity(entity, true);
}


bool World::hasEntity(EntityRef entity) const
{
	return entity.index >= 0 && entity.index < m_entities.size() && m_entities[entity.index].valid;
}


void World::setTransformKeepChildren(EntityRef entity, const Transform& transform)
{
	Transform& tmp = m_transforms[entity.index];
	tmp = transform;
	
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	const ArchetypeManager::Archetype& archetype = m_archetype_manager->get(m_entities[entity.index].archetype);
	for (ComponentType type : archetype.types) {
		m_component_type_map[type.index]->transformed.invoke(entity);
	}
	if (hierarchy_idx >= 0)
	{
		Hierarchy& h = m_hierarchy[hierarchy_idx];
		Transform my_transform = getTransform(entity);
		if (h.parent.isValid())
		{
			Transform parent_tr = getTransform((EntityRef)h.parent);
			h.local_transform = Transform::computeLocal(parent_tr, my_transform);
		}

		EntityPtr child = h.first_child;
		while (child.isValid())
		{
			Hierarchy& child_h = m_hierarchy[m_entities[child.index].hierarchy];

			child_h.local_transform = Transform::computeLocal(my_transform, getTransform((EntityRef)child));
			child = child_h.next_sibling;
		}
	}
}


void World::setTransform(EntityRef entity, const Transform& transform)
{
	Transform& tmp = m_transforms[entity.index];
	tmp = transform;
	transformEntity(entity, true);
}


void World::setTransform(EntityRef entity, const RigidTransform& transform)
{
	auto& tmp = m_transforms[entity.index];
	tmp.pos = transform.pos;
	tmp.rot = transform.rot;
	transformEntity(entity, true);
}


void World::setTransform(EntityRef entity, const DVec3& pos, const Quat& rot, const Vec3& scale)
{
	auto& tmp = m_transforms[entity.index];
	tmp.pos = pos;
	tmp.rot = rot;
	tmp.scale = scale;
	transformEntity(entity, true);
}


const Transform& World::getTransform(EntityRef entity) const
{
	return m_transforms[entity.index];
}


Matrix World::getRelativeMatrix(EntityRef entity, const DVec3& base_pos) const
{
	const Transform& transform = m_transforms[entity.index];
	Matrix mtx = transform.rot.toMatrix();
	mtx.setTranslation(Vec3(transform.pos - base_pos));
	mtx.multiply3x3(transform.scale);
	return mtx;
}


void World::setPosition(EntityRef entity, const DVec3& pos)
{
	m_transforms[entity.index].pos = pos;
	transformEntity(entity, true);
}


void World::setEntityName(EntityRef entity, StringView name)
{
	int name_idx = m_entities[entity.index].name;
	if (name_idx < 0)
	{
		if (name.empty()) return;
		m_entities[entity.index].name = m_names.size();
		EntityName name_data;
		name_data.entity = entity;
		copyString(name_data.name, name);
		m_names.push(name_data);
	}
	else
	{
		copyString(m_names[name_idx].name, name);
	}
}


const char* World::getEntityName(EntityRef entity) const
{
	int name_idx = m_entities[entity.index].name;
	if (name_idx < 0) return "";
	return m_names[name_idx].name;
}


EntityPtr World::findByName(EntityPtr parent, const char* name)
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


void World::emplaceEntity(EntityRef entity)
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
		tr.scale = Vec3(-1);
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
	tr.scale = Vec3(1);
	data.name = -1;
	data.hierarchy = -1;
	data.archetype = EMPTY_ARCHETYPE;
	data.valid = true;

	m_entity_created.invoke(entity);
}


EntityRef World::createEntity(const DVec3& position, const Quat& rotation)
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
	tr->scale = Vec3(1);
	data->partition = m_active_partition;
	data->name = -1;
	data->hierarchy = -1;
	data->archetype = EMPTY_ARCHETYPE;
	data->valid = true;
	m_entity_created.invoke(entity);

	return entity;
}

void World::destroyEntity(EntityRef entity) {
	EntityData& entity_data = m_entities[entity.index];
	ASSERT(entity_data.valid);
	// destroy all children recursively
	EntityPtr child = getFirstChild(entity);
	while (child.isValid()) {
		destroyEntity((EntityRef)child);
		child = getFirstChild(entity);
	}

	// remove itself from hierarchy
	setParent(INVALID_ENTITY, entity);

	// destroy components
	const ArchetypeManager::Archetype& archetype = m_archetype_manager->get(entity_data.archetype);
	for (ComponentType type : archetype.types) {
		IModule* module = m_component_type_map[type.index]->module;
		auto destroy_method = m_component_type_map[type.index]->destroy;
		destroy_method(module, entity);
	}

	// clear entity_data
	entity_data.next = m_first_free_slot;
	entity_data.prev = -1;
	entity_data.hierarchy = -1;
	
	entity_data.valid = false;
	if (m_first_free_slot >= 0) {
		m_entities[m_first_free_slot].prev = entity.index;
	}

	if (entity_data.name >= 0) {
		m_entities[m_names.back().entity.index].name = entity_data.name;
		m_names.swapAndPop(entity_data.name);
		entity_data.name = -1;
	}

	m_first_free_slot = entity.index;
	// callback
	m_entity_destroyed.invoke(entity);
}


EntityPtr World::getFirstEntity() const
{
	for (int i = 0; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return EntityPtr{i};
	}
	return INVALID_ENTITY;
}


EntityPtr World::getNextEntity(EntityRef entity) const
{
	for (int i = entity.index + 1; i < m_entities.size(); ++i)
	{
		if (m_entities[i].valid) return EntityPtr{i};
	}
	return INVALID_ENTITY;
}


EntityPtr World::getParent(EntityRef entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].parent;
}


EntityPtr World::getFirstChild(EntityRef entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].first_child;
}


EntityPtr World::getNextSibling(EntityRef entity) const
{
	int idx = m_entities[entity.index].hierarchy;
	if (idx < 0) return INVALID_ENTITY;
	return m_hierarchy[idx].next_sibling;
}


bool World::isDescendant(EntityRef ancestor, EntityRef descendant) const
{
	for(EntityRef e : childrenOf(ancestor)) {
		if (e == descendant) return true;
		if (isDescendant(e, descendant)) return true;
	}

	return false;
}


void World::setParent(EntityPtr new_parent, EntityRef child)
{
	bool would_create_cycle = new_parent.isValid() && isDescendant(child, (EntityRef)new_parent);
	if (would_create_cycle)
	{
		logError("Hierarchy can not contain a cycle.");
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
		m_hierarchy[child_idx].local_transform = Transform::computeLocal(parent_tr, child_tr);
		m_hierarchy[child_idx].next_sibling = m_hierarchy[new_parent_idx].first_child;
		m_hierarchy[new_parent_idx].first_child = child;
	}
	else
	{
		if (child_idx >= 0) collectGarbage(child);
	}
}


void World::updateGlobalTransform(EntityRef entity)
{
	const Hierarchy& h = m_hierarchy[m_entities[entity.index].hierarchy];
	ASSERT(h.parent.isValid());
	Transform parent_tr = getTransform((EntityRef)h.parent);
	
	Transform new_tr = parent_tr.compose(h.local_transform);
	setTransform(entity, new_tr);
}


void World::setLocalPosition(EntityRef entity, const DVec3& pos)
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


void World::setLocalRotation(EntityRef entity, const Quat& rot)
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


void World::setLocalTransform(EntityRef entity, const Transform& transform)
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


Transform World::getLocalTransform(EntityRef entity) const
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		return getTransform(entity);
	}

	return m_hierarchy[hierarchy_idx].local_transform;
}


Vec3 World::getLocalScale(EntityRef entity) const
{
	int hierarchy_idx = m_entities[entity.index].hierarchy;
	if (hierarchy_idx < 0)
	{
		return getScale(entity);
	}

	return m_hierarchy[hierarchy_idx].local_transform.scale;
}

static void serializeModuleList(World& world, OutputMemoryStream& serializer) {
	const Array<UniquePtr<IModule>>& modules = world.getModules();
	serializer.write((i32)modules.size());
	for (UniquePtr<IModule>& module : modules) {
		serializer.writeString(module->getName());
	}
}

static bool hasSerializedModules(World& world, InputMemoryStream& serializer) {
	i32 count;
	serializer.read(count);
	for (int i = 0; i < count; ++i) {
		const char* tmp = serializer.readString();
		if (!world.getModule(tmp)) {
			logError("Missing module ", tmp);
			return false;
		}
	}
	return true;
}

#pragma pack(1)
struct WorldEditorHeaderLegacy {
	enum class Version : u32 {
		CAMERA,
		ENTITY_FOLDERS,
		HASH64,
		NEW_ENTITY_FOLDERS,

		LATEST
	};

	static const u32 MAGIC = 'LUNV';
	u32 magic;
	Version version;
};

struct WorldHeaderLegacy {
	enum class Version : u32 {
		VEC3_SCALE,
		FLAGS,
		LAST
	};
	static const u32 MAGIC = '_LEN';

	u32 magic;
	Version version;
};

struct WorldHeader {
	static const u32 MAGIC = 'LWRL';

	u32 magic = MAGIC;
	WorldVersion version = WorldVersion::LATEST;
};
#pragma pack()

void World::serialize(OutputMemoryStream& serializer, WorldSerializeFlags flags) {
	const bool serialize_partitions = (u32)flags & (u32)WorldSerializeFlags::HAS_PARTITIONS;
	WorldHeader header;
	serializer.write(header);
	serializeModuleList(*this, serializer);
	serializer.write(flags);

	OutputMemoryStream blob(m_allocator);
	blob.write((u32)m_entities.size());

	for (u32 i = 0, c = m_entities.size(); i < c; ++i) {
		if (!m_entities[i].valid) continue;
		const EntityRef e {(i32)i};
		blob.write(e);
		blob.write(m_transforms[i].pos);
		blob.write(m_transforms[i].rot);
		blob.write(m_transforms[i].scale);
		if (serialize_partitions) blob.write(m_entities[i].partition);
	}
	blob.write(INVALID_ENTITY);

	blob.write((u32)m_names.size());
	for (const EntityName& name : m_names) {
		blob.write(name.entity);
		blob.writeString(name.name);
	}

	blob.write((u32)m_hierarchy.size());
	if (!m_hierarchy.empty()) {
		for (const Hierarchy& h : m_hierarchy) {
			blob.write(h.entity);
			blob.write(h.parent);
			blob.write(h.first_child);
			blob.write(h.next_sibling);
			blob.write(h.local_transform.pos);
			blob.write(h.local_transform.rot);
			blob.write(h.local_transform.scale);
		}
	}

	blob.write((i32)m_modules.size());
	for (const UniquePtr<IModule>& module : m_modules) {
		blob.writeString(module->getName());
		blob.write(module->getVersion());
		module->serialize(blob);
	}

	if (serialize_partitions) {
		blob.write((u32)m_partitions.size());
		blob.write(m_partitions.begin(), m_partitions.byte_size());
		blob.write(m_active_partition);
	}

	const u64 offset = serializer.size();
	serializer.write((u32)0);
	serializer.write((u32)0);
	m_engine.compress(blob, serializer);
	u32* sizes = (u32*)(serializer.getMutableData() + offset);
	sizes[0] = (u32)blob.size(); // uncompressed size
	sizes[1] = u32(serializer.size() - offset - sizeof(u32) * 2); // compressed size
}

bool World::deserialize(InputMemoryStream& input, EntityMap& entity_map, WorldVersion& version)
{
	WorldHeader header;
	WorldHeaderLegacy::Version legacy_version = WorldHeaderLegacy::Version::LAST;
	input.read(header);
	if (header.magic == WorldEditorHeaderLegacy::MAGIC || header.magic == 0xffFFffFF) {
		header.magic = WorldHeader::MAGIC;
		// WorldEditorHeaderLegacy::Version matches first values of WorldHeaderVersion, so we can just use header.version as is
		static_assert(sizeof(WorldEditorHeaderLegacy) == sizeof(WorldHeader));
		input.read<u64>(); // hash
		WorldHeaderLegacy legacy_header;
		input.read(legacy_header);
		if (input.hasOverflow() || legacy_header.magic != WorldHeaderLegacy::MAGIC) {
			logError("Wrong or corrupted file");
			return false;
		}
		legacy_version = legacy_header.version;
	}
	else if (header.magic == WorldHeaderLegacy::MAGIC) {
		memcpy(&legacy_version, &header.version, sizeof(header.version));
		header.magic = WorldHeader::MAGIC;
		header.version = WorldVersion::MERGED_HEADERS;
	}

	version = header.version;

	if (input.hasOverflow() || header.magic != WorldHeader::MAGIC) {
		logError("Wrong or corrupted file");
		return false;
	}
	if (header.version > WorldVersion::LATEST) {
		logError("Unsupported version of world");
		return false;
	}
	if (!hasSerializedModules(*this, input)) return false;

	bool deserialize_partitions = false;
	if (legacy_version > WorldHeaderLegacy::Version::FLAGS) {
		WorldSerializeFlags flags;
		input.read(flags);
		deserialize_partitions = (u32)flags & (u32)WorldSerializeFlags::HAS_PARTITIONS;
	}

	InputMemoryStream serializer(input.skip(0), input.remaining());
	OutputMemoryStream uncompressed(m_allocator);
	if (header.version > WorldVersion::COMPRESSED) { 
		u32 uncompressed_size;
		u32 compressed_size;
		input.read(uncompressed_size);
		input.read(compressed_size);
		uncompressed.resize(uncompressed_size);
		m_engine.decompress(Span((const u8*)input.skip(0), compressed_size), Span(uncompressed.getMutableData(), uncompressed.size()));
		serializer = InputMemoryStream(uncompressed);
		input.skip(compressed_size);
	}

	u32 to_reserve;
	serializer.read(to_reserve);
	entity_map.reserve(to_reserve);

	for (EntityPtr e = serializer.read<EntityPtr>(); e.isValid(); e = serializer.read<EntityPtr>()) {
		EntityRef orig = (EntityRef)e;
		const EntityRef new_e = createEntity({0, 0, 0}, {0, 0, 0, 1});
		entity_map.set(orig, new_e);
		Transform& tr = m_transforms[new_e.index];
		serializer.read(tr.pos);
		serializer.read(tr.rot);
		if (legacy_version > WorldHeaderLegacy::Version::VEC3_SCALE) {
			serializer.read(tr.scale);
		}
		else {
			serializer.read(tr.scale.x);
			float padding;
			serializer.read(padding);
			tr.scale.y = tr.scale.z = tr.scale.x;
		}
		if (deserialize_partitions) serializer.read(m_entities[new_e.index].partition);
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
		for (u32 i = 0; i < count; ++i) {
			Hierarchy& h = m_hierarchy[old_count + i];
			serializer.read(h.entity);
			serializer.read(h.parent);
			serializer.read(h.first_child);
			serializer.read(h.next_sibling);
			serializer.read(h.local_transform.pos);
			serializer.read(h.local_transform.rot);
			if (legacy_version > WorldHeaderLegacy::Version::VEC3_SCALE) {
				serializer.read(h.local_transform.scale);
			}
			else {
				serializer.read(h.local_transform.scale.x);
				float padding;
				serializer.read(padding);
				h.local_transform.scale.z = h.local_transform.scale.y = h.local_transform.scale.x;
			}

			h.entity = entity_map.get(h.entity);
			h.first_child = entity_map.get(h.first_child);
			h.next_sibling = entity_map.get(h.next_sibling);
			h.parent = entity_map.get(h.parent);
			m_entities[h.entity.index].hierarchy = i + old_count;
		}
	}

	i32 module_count;
	serializer.read(module_count);
	for (int i = 0; i < module_count; ++i) {
		const char* tmp = serializer.readString();
		IModule* module = getModule(tmp);
		const i32 version = serializer.read<i32>();
		module->deserialize(serializer, entity_map, version);
	}

	if (deserialize_partitions) {
		u32 partitions_count;
		serializer.read(partitions_count);
		m_partitions.resize(partitions_count);
		serializer.read(m_partitions.begin(), m_partitions.byte_size());
		serializer.read(m_active_partition);
	}
	if (serializer.hasOverflow()) {
		logError("End of file encountered while trying to read data");
		return false;
	}

	if (header.version <= WorldVersion::COMPRESSED) { 
		input.skip(serializer.getPosition());
	}
	return true;
}

void World::setScale(EntityRef entity, const Vec3& scale)
{
	m_transforms[entity.index].scale = scale;
	transformEntity(entity, true);
}


const Vec3& World::getScale(EntityRef entity) const
{
	return m_transforms[entity.index].scale;
}


Span<const ComponentType> World::getComponents(EntityRef entity) const
{
	ArchetypeHandle archetype = m_entities[entity.index].archetype;
	if (archetype == EMPTY_ARCHETYPE) return {};

	const ArchetypeManager::Archetype& a = m_archetype_manager->get(archetype);
	return a.types;
}


bool World::hasComponent(EntityRef entity, ComponentType component_type) const
{
	const ArchetypeHandle archetype = m_entities[entity.index].archetype;
	return m_archetype_manager->hasComponent(archetype, component_type);
}


void World::onComponentDestroyed(EntityRef entity, ComponentType component_type, IModule* module) {
	ComponentType tmp[64];
	const ArchetypeHandle archetype = m_entities[entity.index].archetype;
	const ArchetypeManager::Archetype& a = m_archetype_manager->get(archetype);
	ASSERT(a.types.size() <= (i32)lengthOf(tmp));
	u32 count = 0;
	for (ComponentType t : a.types) {
		if (t == component_type) continue;
		tmp[count] = t;
		++count;
	}

	m_entities[entity.index].archetype = m_archetype_manager->get(Span(tmp, count));

	m_component_destroyed.invoke(ComponentUID(entity, component_type, module));
}


void World::createComponent(ComponentType type, EntityRef entity)
{
	IModule* module = m_component_type_map[type.index]->module;
	auto& create_method = m_component_type_map[type.index]->create;
	create_method(module, entity);
}


void World::destroyComponent(EntityRef entity, ComponentType type)
{
	IModule* module = m_component_type_map[type.index]->module;
	auto& destroy_method = m_component_type_map[type.index]->destroy;
	destroy_method(module, entity);
}


void World::onComponentCreated(EntityRef entity, ComponentType component_type, IModule* module)
{
	ComponentType tmp[64];
	const ArchetypeHandle archetype = m_entities[entity.index].archetype;
	const ArchetypeManager::Archetype& a = m_archetype_manager->get(archetype);
	ASSERT(a.types.size() + 1 <= (i32)lengthOf(tmp));
	u32 count = 0;
	for (ComponentType t : a.types) {
		if (t == component_type) continue;
		tmp[count] = t;
		++count;
	}
	tmp[count] = component_type;
	++count;

	m_entities[entity.index].archetype = m_archetype_manager->get(Span(tmp, count));

	ComponentUID cmp(entity, component_type, module);
	m_component_added.invoke(cmp);
}

ChildrenRange World::childrenOf(EntityRef entity) const {
	return ChildrenRange(*this, entity);
}

void ChildrenRange::Iterator::operator ++() {
	if (entity) entity = world->getNextSibling(*entity);
}

bool ChildrenRange::Iterator::operator !=(const Iterator& rhs) {
	return rhs.entity != entity;
}

EntityRef ChildrenRange::Iterator::operator*() {
	return *entity;
}

ChildrenRange::ChildrenRange(const World& world, EntityRef parent)
	: world(world)
	, parent(parent)
{}

ChildrenRange::Iterator ChildrenRange::begin() const {
	Iterator iter;
	iter.world = &world;
	iter.entity = world.getFirstChild(parent);
	return iter;
}

ChildrenRange::Iterator ChildrenRange::end() const {
	Iterator iter;
	iter.world = &world;
	iter.entity = INVALID_ENTITY;
	return iter;
}

} // namespace Lumix
