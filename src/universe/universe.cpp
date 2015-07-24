#include "universe.h"
#include "core/blob.h"
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
	, m_component_destroyed(m_allocator)
	, m_entity_created(m_allocator)
	, m_entity_destroyed(m_allocator)
	, m_entity_moved(m_allocator)
	, m_free_slots(m_allocator)
{
	m_positions.reserve(RESERVED_ENTITIES);
	m_rotations.reserve(RESERVED_ENTITIES);
}


void Universe::setRotation(int entity_index, const Quat& rot)
{
	m_rotations[entity_index] = rot;
	entityMoved().invoke(Entity(entity_index));
}


void Universe::setRotation(int entity_index, float x, float y, float z, float w)
{
	m_rotations[entity_index].set(x, y, z, w);
	entityMoved().invoke(Entity(entity_index));
}


bool Universe::hasEntity(Entity entity) const
{
	for (int i = 0; i < m_free_slots.size(); ++i)
	{
		if (m_free_slots[i] == entity)
			return false;
	}
	return entity != -1;

}


void Universe::setMatrix(int entity_index, const Matrix& mtx)
{
	Quat rot;
	mtx.getRotation(rot);
	m_positions[entity_index] = mtx.getTranslation();
	m_rotations[entity_index] = rot;
	entityMoved().invoke(Entity(entity_index));
}


Matrix Universe::getMatrix(int entity_index) const
{
	Matrix mtx;
	m_rotations[entity_index].toMatrix(mtx);
	mtx.setTranslation(m_positions[entity_index]);
	return mtx;
}


void Universe::setPosition(int entity_index, float x, float y, float z)
{
	m_positions[entity_index].set(x, y, z);
	entityMoved().invoke(Entity(entity_index));
}


void Universe::setPosition(int entity_index, const Vec3& pos)
{
	m_positions[entity_index] = pos;
	entityMoved().invoke(Entity(entity_index));
}


void Universe::setEntityName(Entity entity, const char* name)
{
	int name_index = m_id_to_name_map.find(entity);
	if (name_index >= 0)
	{
		uint32_t hash = crc32(m_id_to_name_map.at(name_index).c_str());
		m_name_to_id_map.erase(hash);
		m_id_to_name_map.eraseAt(name_index);
	}

	if (name && name[0] != '\0')
	{
		m_name_to_id_map.insert(crc32(name), entity);
		m_id_to_name_map.insert(entity, string(name, getAllocator()));
	}
}


const char* Universe::getEntityName(Entity entity) const
{
	int name_index = m_id_to_name_map.find(entity);
	return name_index < 0 ? "" : m_id_to_name_map.at(name_index).c_str();
}


void Universe::createEntity(Entity entity)
{
	ASSERT(entity >= 0);
	for (int i = 0; i < m_free_slots.size(); ++i)
	{
		if (m_free_slots[i] == entity)
		{
			m_free_slots.eraseFast(i);
			m_positions[entity].set(0, 0, 0);
			m_rotations[entity].set(0, 0, 0, 1);
			m_entity_created.invoke(entity);
			return;
		}
	}
	ASSERT(false);
}


Entity Universe::createEntity()
{
	if (m_free_slots.empty())
	{
		m_positions.push(Vec3(0, 0, 0));
		m_rotations.push(Quat(0, 0, 0, 1));
		Entity e(m_positions.size() - 1);
		m_entity_created.invoke(e);
		return e;
	}
	else
	{
		Entity e(m_free_slots.back());
		m_free_slots.pop();
		m_positions[e].set(0, 0, 0);
		m_rotations[e].set(0, 0, 0, 1);
		m_entity_created.invoke(e);
		return e;
	}
}


void Universe::destroyEntity(Entity entity)
{
	if (entity >= 0)
	{
		m_free_slots.push(entity);
		m_entity_destroyed.invoke(entity);
	}
}


Entity Universe::getFirstEntity()
{
	for (int i = 0; i < m_positions.size(); ++i)
	{
		bool found = false;
		for (int j = 0, cj = m_free_slots.size(); j < cj; ++j)
		{
			if (m_free_slots[j] == i)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return Entity(i);
		}
	}
	return INVALID_ENTITY;
}


Entity Universe::getNextEntity(Entity entity)
{
	for (int i = entity + 1; i < m_positions.size(); ++i)
	{
		bool found = false;
		for (int j = 0, cj = m_free_slots.size(); j < cj; ++j)
		{
			if (m_free_slots[j] == i)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return Entity(i);
		}
	}
	return INVALID_ENTITY;
}


void Universe::serialize(OutputBlob& serializer)
{
	serializer.write((int32_t)m_positions.size());
	serializer.write(&m_positions[0].x,
					 sizeof(m_positions[0]) * m_positions.size());
	serializer.write(&m_rotations[0].x,
					 sizeof(m_rotations[0]) * m_rotations.size());
	serializer.write((int32_t)m_id_to_name_map.size());
	for (int i = 0, c = m_id_to_name_map.size(); i < c; ++i)
	{
		serializer.write(m_id_to_name_map.getKey(i));
		serializer.writeString(m_id_to_name_map.at(i).c_str());
	}

	serializer.write((int32_t)m_free_slots.size());
	if (!m_free_slots.empty())
	{
		serializer.write(&m_free_slots[0],
						 sizeof(m_free_slots[0]) * m_free_slots.size());
	}
}


void Universe::deserialize(InputBlob& serializer)
{
	int32_t count;
	serializer.read(count);
	m_positions.resize(count);
	m_rotations.resize(count);

	serializer.read(&m_positions[0].x,
					sizeof(m_positions[0]) * m_positions.size());
	serializer.read(&m_rotations[0].x,
					sizeof(m_rotations[0]) * m_rotations.size());

	serializer.read(count);
	m_id_to_name_map.clear();
	m_name_to_id_map.clear();
	for (int i = 0; i < count; ++i)
	{
		uint32_t key;
		char name[50];
		serializer.read(key);
		serializer.readString(name, sizeof(name));
		m_id_to_name_map.insert(key, string(name, m_allocator));
		m_name_to_id_map.insert(crc32(name), key);
	}

	serializer.read(count);
	m_free_slots.resize(count);
	if (!m_free_slots.empty())
	{
		serializer.read(&m_free_slots[0], sizeof(m_free_slots[0]) * count);
	}
}


void Universe::destroyComponent(Entity entity,
								uint32_t component_type,
								IScene* scene,
								int index)
{
	m_component_destroyed.invoke(
		ComponentUID(entity, component_type, scene, index));
}


void Universe::addComponent(Entity entity,
									uint32_t component_type,
									IScene* scene,
									int index)
{
	ComponentUID cmp(entity, component_type, scene, index);
	if (m_component_added.isValid())
	{
		m_component_added.invoke(cmp);
	}
}


bool Universe::nameExists(const char* name) const
{
	return m_name_to_id_map.find(crc32(name)) != -1;
}


} // !namespace Lumix
