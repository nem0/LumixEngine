#include "entity_groups.h"
#include "core/blob.h"
#include "core/string.h"
#include "universe/universe.h"


namespace Lumix
{
EntityGroups::EntityGroups(IAllocator& allocator)
	: m_groups(allocator)
	, m_group_names(allocator)
	, m_allocator(allocator)
	, m_universe(nullptr)
{
	m_groups.emplace(allocator);
	auto& name = m_group_names.pushEmpty();
	copyString(name.name, "default");
}


int EntityGroups::getGroup(const char* name) const
{
	for(int i = 0, c = m_group_names.size(); i < c; ++i)
	{
		if(compareString(m_group_names[i].name, name) == 0) return i;
	}
	return -1;
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
	m_group_names.eraseFast(idx);
}


void EntityGroups::createGroup(const char* name)
{
	if (name[0] == 0) return;
	for (auto& i : m_group_names)
	{
		if (compareString(i.name, name) == 0) return;
	}

	m_groups.emplace(m_allocator);
	auto& group_name = m_group_names.pushEmpty();
	copyString(group_name.name, name);
}


void EntityGroups::setUniverse(Universe* universe)
{
	if (m_universe)
	{
		m_universe->entityCreated().unbind<EntityGroups, &EntityGroups::onEntityCreated>(this);
		m_universe->entityDestroyed().unbind<EntityGroups, &EntityGroups::onEntityDestroyed>(this);
	}

	m_universe = universe;
	m_group_names.clear();
	m_groups.clear();
	m_groups.emplace(m_allocator);
	auto& name = m_group_names.pushEmpty();
	copyString(name.name, "default");

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
}


void EntityGroups::onEntityDestroyed(Entity entity)
{
	removeFromGroup(entity);
}


void EntityGroups::setGroup(Entity entity, int group)
{
	removeFromGroup(entity);
	m_groups[group].push(entity);
}


void EntityGroups::removeFromGroup(Entity entity)
{
	for (auto& g : m_groups)
	{
		for (int i = 0, c = g.size(); i < c; ++i)
		{
			if (g[i] == entity)
			{
				g.eraseFast(i);
				return;
			}
		}
	}
}


void EntityGroups::serialize(OutputBlob& blob)
{
	ASSERT(sizeof(m_group_names[0]) == 20);

	blob.write(m_group_names.size());
	blob.write(&m_group_names[0], m_group_names.size() * sizeof(m_group_names[0]));

	for(auto& i : m_groups)
	{
		blob.write(i.size());
		blob.write(&i[0], i.size() * sizeof(i[0]));
	}
}


void EntityGroups::deserialize(InputBlob& blob)
{
	int count;
	blob.read(count);
	m_group_names.resize(count);
	blob.read(&m_group_names[0], count * sizeof(m_group_names[0]));

	m_groups.clear();
	for(int i = 0; i < count; ++i)
	{
		int group_size;
		auto& group = m_groups.emplace(m_allocator);
		blob.read(group_size);
		group.resize(group_size);
		blob.read(&group[0], group_size * sizeof(group[0]));
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
	return m_group_names[idx].name;
}


void EntityGroups::setGroupName(int idx, const char* name)
{
	copyString(m_group_names[idx].name, name);
}
} // namespace Lumix
