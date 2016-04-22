#include "entity_groups.h"
#include "engine/core/blob.h"
#include "engine/core/string.h"
#include "universe/universe.h"


namespace Lumix
{
EntityGroups::EntityGroups(IAllocator& allocator)
	: m_groups(allocator)
	, m_group_infos(allocator)
	, m_entity_to_group_map(allocator)
	, m_allocator(allocator)
	, m_universe(nullptr)
{
	m_groups.emplace(allocator);
	auto& info = m_group_infos.emplace();
	copyString(info.name, "default");
}


int EntityGroups::getGroup(const char* name) const
{
	for (int i = 0, c = m_group_infos.size(); i < c; ++i)
	{
		if (compareString(m_group_infos[i].name, name) == 0) return i;
	}
	return -1;
}


void EntityGroups::freezeGroup(int idx, bool freeze)
{
	m_group_infos[idx].frozen = freeze;
}


bool EntityGroups::isGroupFrozen(int idx) const
{
	return m_group_infos[idx].frozen;
}


void EntityGroups::allEntitiesToDefault()
{
	ASSERT(m_groups.size() == 1);
	ASSERT(m_groups[0].empty());
	ASSERT(m_universe);

	for (int i = 0, c = m_universe->getEntityCount(); i < c; ++i)
	{
		Entity entity = m_universe->getEntityFromDenseIdx(i);
		m_groups[0].push(entity);
		m_entity_to_group_map[entity] = 0;
	}
}


void EntityGroups::deleteGroup(int idx)
{
	if (m_groups.size() == 1) return;
	int default_idx = idx == 0 ? 1 : 0;
	for (auto e : m_groups[idx])
	{
		m_groups[default_idx].push(e);
	}
	m_groups.eraseFast(idx);
	m_group_infos.eraseFast(idx);
}


void EntityGroups::createGroup(const char* name)
{
	if (name[0] == 0) return;
	for (auto& i : m_group_infos)
	{
		if (compareString(i.name, name) == 0) return;
	}

	m_groups.emplace(m_allocator);
	auto& group_info = m_group_infos.emplace();
	copyString(group_info.name, name);
	group_info.frozen = false;
}


void EntityGroups::setUniverse(Universe* universe)
{
	if (m_universe)
	{
		m_universe->entityCreated().unbind<EntityGroups, &EntityGroups::onEntityCreated>(this);
		m_universe->entityDestroyed().unbind<EntityGroups, &EntityGroups::onEntityDestroyed>(this);
	}

	m_universe = universe;
	m_group_infos.clear();
	m_groups.clear();
	m_groups.emplace(m_allocator);
	auto& info = m_group_infos.emplace();
	copyString(info.name, "default");

	if (m_universe)
	{
		m_universe->entityCreated().bind<EntityGroups, &EntityGroups::onEntityCreated>(this);
		m_universe->entityDestroyed().bind<EntityGroups, &EntityGroups::onEntityDestroyed>(this);
	}
}


int EntityGroups::getGroupEntitiesCount(int idx) const
{
	return m_groups[idx].size();
}


void EntityGroups::onEntityCreated(Entity entity)
{
	m_groups[0].push(entity);
	if (entity >= m_entity_to_group_map.size()) m_entity_to_group_map.resize(entity + 1);
	m_entity_to_group_map[entity] = 0;
}


void EntityGroups::onEntityDestroyed(Entity entity)
{
	removeFromGroup(entity);
	m_entity_to_group_map[entity] = -1;
}


void EntityGroups::setGroup(Entity entity, int group)
{
	removeFromGroup(entity);
	m_groups[group].push(entity);
	m_entity_to_group_map[entity] = group;
}


void EntityGroups::removeFromGroup(Entity entity)
{
	m_groups[m_entity_to_group_map[entity]].eraseItemFast(entity);
	m_entity_to_group_map[entity] = -1;
}


void EntityGroups::serialize(OutputBlob& blob)
{
	ASSERT(sizeof(m_group_infos[0].name) == 20);

	blob.write(m_group_infos.size());
	for(auto& group : m_group_infos)
	{
		blob.write(&group.name, sizeof(group.name));
	}

	for(auto& i : m_groups)
	{
		blob.write(i.size());
		if(!i.empty()) blob.write(&i[0], i.size() * sizeof(i[0]));
	}
}


void EntityGroups::deserialize(InputBlob& blob)
{
	int count;
	blob.read(count);
	m_group_infos.resize(count);
	for (int i = 0; i < count; ++i)
	{
		blob.read(m_group_infos[i].name, sizeof(m_group_infos[i].name));
		m_group_infos[i].frozen = false;
	}

	m_groups.clear();
	int entity_count = 0;
	for(int i = 0; i < count; ++i)
	{
		int group_size;
		auto& group = m_groups.emplace(m_allocator);
		blob.read(group_size);
		entity_count += group_size;
		group.resize(group_size);
		if(group_size > 0) blob.read(&group[0], group_size * sizeof(group[0]));
	}
	m_entity_to_group_map.resize(entity_count);
	for (int i = 0; i < m_groups.size(); ++i)
	{
		for (auto e : m_groups[i])
		{
			if (e >= m_entity_to_group_map.size()) m_entity_to_group_map.resize(e + 1);
			m_entity_to_group_map[e] = i;
		}
	}
}


const Entity* EntityGroups::getGroupEntities(int idx) const
{
	return &m_groups[idx][0];
}


int EntityGroups::getGroupCount() const
{
	return m_groups.size();
}


const char* EntityGroups::getGroupName(int idx) const
{
	return m_group_infos[idx].name;
}


void EntityGroups::setGroupName(int idx, const char* name)
{
	copyString(m_group_infos[idx].name, name);
}
} // namespace Lumix
