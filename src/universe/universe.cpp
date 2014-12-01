#include "universe.h"
#include "core/crc32.h"
#include "core/matrix.h"
#include "core/json_serializer.h"


namespace Lumix
{

	
static const int RESERVED_ENTITIES = 5000;



Universe::~Universe()
{
}


Universe::Universe(IAllocator& allocator)
	: m_allocator(allocator)
	, m_name_to_id_map(m_allocator)
	, m_id_to_name_map(m_allocator)
	, m_positions(m_allocator)
	, m_rotations(m_allocator)
	, m_component_created(m_allocator)
	, m_component_destroyed(m_allocator)
	, m_entity_created(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_moved(m_allocator)
	, m_free_slots(m_allocator)
{
	m_positions.reserve(RESERVED_ENTITIES);
	m_rotations.reserve(RESERVED_ENTITIES);
}


Entity Universe::createEntity()
{
	if(m_free_slots.empty())
	{
		m_positions.push(Vec3(0, 0, 0));
		m_rotations.push(Quat(0, 0, 0, 1));
		Entity e(this, m_positions.size() - 1);
		m_entity_created.invoke(e);
		return e;
	}
	else
	{
		Entity e(this, m_free_slots.back());
		m_free_slots.pop();
		m_positions[e.index].set(0, 0, 0);
		m_rotations[e.index].set(0, 0, 0, 1);
		m_entity_created.invoke(e);
		return e;
	}
}


void Universe::destroyEntity(Entity& entity)
{
	if(entity.isValid())
	{
		m_free_slots.push(entity.index);
		m_entity_destroyed.invoke(entity);
	}
}


Entity Universe::getFirstEntity()
{
	for(int i = 0; i < m_positions.size(); ++i)
	{
		bool found = false;
		for(int j = 0, cj = m_free_slots.size(); j < cj; ++j)
		{
			if(m_free_slots[j] == i)
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			return Entity(this, i);
		}
	}
	return Entity::INVALID;
}


Entity Universe::getNextEntity(Entity entity)
{
	for(int i = entity.index + 1; i < m_positions.size(); ++i)
	{
		bool found = false;
		for(int j = 0, cj = m_free_slots.size(); j < cj; ++j)
		{
			if(m_free_slots[j] == i)
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			return Entity(this, i);
		}
	}
	return Entity::INVALID;
}


void Universe::serialize(ISerializer& serializer)
{
	serializer.serialize("count", m_positions.size());
	serializer.beginArray("positions");
	for(int i = 0; i < m_positions.size(); ++i)
	{
		serializer.serializeArrayItem(m_positions[i].x);
		serializer.serializeArrayItem(m_positions[i].y);
		serializer.serializeArrayItem(m_positions[i].z);
	}
	serializer.endArray();
	serializer.beginArray("rotations");
	for(int i = 0; i < m_rotations.size(); ++i)
	{
		serializer.serializeArrayItem(m_rotations[i].x);
		serializer.serializeArrayItem(m_rotations[i].y);
		serializer.serializeArrayItem(m_rotations[i].z);
		serializer.serializeArrayItem(m_rotations[i].w);
	}
	serializer.endArray();

	serializer.serialize("name_count", m_id_to_name_map.size());
	serializer.beginArray("names");
	for (int i = 0, c = m_id_to_name_map.size(); i < c; ++i)
	{
		serializer.serializeArrayItem(m_id_to_name_map.getKey(i));
		serializer.serializeArrayItem(m_id_to_name_map.get(i).c_str());
	}
	serializer.endArray();

	serializer.serialize("free_slot_count", m_free_slots.size());
	serializer.beginArray("free_slots");
	for(int i = 0; i < m_free_slots.size(); ++i)
	{
		serializer.serializeArrayItem(m_free_slots[i]);
	}
	serializer.endArray();
}


void Universe::deserialize(ISerializer& serializer)
{
	int count;
	serializer.deserialize("count", count, 0);
	m_positions.resize(count);
	m_rotations.resize(count);

	serializer.deserializeArrayBegin("positions");
	for(int i = 0; i < count; ++i)
	{
		serializer.deserializeArrayItem(m_positions[i].x, 0);
		serializer.deserializeArrayItem(m_positions[i].y, 0);
		serializer.deserializeArrayItem(m_positions[i].z, 0);
	}
	serializer.deserializeArrayEnd();

	serializer.deserializeArrayBegin("rotations");
	for(int i = 0; i < count; ++i)
	{
		serializer.deserializeArrayItem(m_rotations[i].x, 0);
		serializer.deserializeArrayItem(m_rotations[i].y, 0);
		serializer.deserializeArrayItem(m_rotations[i].z, 0);
		serializer.deserializeArrayItem(m_rotations[i].w, 1);
	}
	serializer.deserializeArrayEnd();

	serializer.deserialize("name_count", count, 0);
	serializer.deserializeArrayBegin("names");
	m_id_to_name_map.clear();
	m_name_to_id_map.clear();
	for (int i = 0; i < count; ++i)
	{
		uint32_t key;
		static const int MAX_NAME_LENGTH = 50;
		char name[MAX_NAME_LENGTH];
		serializer.deserializeArrayItem(key, 0);
		serializer.deserializeArrayItem(name, MAX_NAME_LENGTH, "");
		m_id_to_name_map.insert(key, string(name, m_allocator));
		m_name_to_id_map.insert(crc32(name), key);
	}
	serializer.deserializeArrayEnd();

	serializer.deserialize("free_slot_count", count, 0);
	m_free_slots.resize(count);
	serializer.deserializeArrayBegin("free_slots");
	for(int i = 0; i < count; ++i)
	{
		serializer.deserializeArrayItem(m_free_slots[i], 0);
	}
	serializer.deserializeArrayEnd();
}


void Universe::destroyComponent(const Component& cmp)
{
	m_component_destroyed.invoke(cmp);
}


Component Universe::addComponent(const Entity& entity, uint32_t component_type, IScene* scene, int index)
{
	Component cmp(entity, component_type, scene, index);
	if (m_component_added.isValid())
	{
		m_component_added.invoke(cmp);
	}
	return cmp;
}


bool Universe::nameExists(const char* name) const
{
	return m_name_to_id_map.find(crc32(name)) != -1;
}


} // !namespace Lumix
