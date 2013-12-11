#include "entity_names_map.h"

#include "core/crc32.h"
#include "core/json_serializer.h"

namespace Lux
{

const static int MAX_ENTITY_NAME_SIZE = 32;

void EntityNamesMap::setUniverse(Universe* universe)
{
	m_universe = universe;
}

Entity EntityNamesMap::getEntityByName(const char* entity_name) const
{
	int index;
	uint32_t crc = crc32(entity_name);

	if(m_crc_names_map.find(crc, index))
	{
		return Entity(m_universe, index);
	}
	return Entity::INVALID;
}

//TODO: move it to the editor
const char* EntityNamesMap::getEntityName(const Entity& entity) const
{
	for(Lux::map<string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		if(it.second() == entity.index)
		{
			return it.first().c_str();
		}
	}
	return "";
}

void EntityNamesMap::removeEntityName(const Entity& entity)
{
	/// TODO: remove
	for(Lux::map<string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		if(it.second() == entity.index)
		{
			m_names_map.erase(it.first());
			break;
		}
	}
	///
	
	for(Lux::map<uint32_t, int>::iterator it = m_crc_names_map.begin(); it != m_crc_names_map.end(); ++it)
	{
		if(it.second() == entity.index)
		{
			m_crc_names_map.erase(it.first());
			break;
		}
	}
}

bool EntityNamesMap::setEntityName(const char* entity_name, const Entity& entity)
{
	if(entity_name[0] == 0)
	{
		removeEntityName(entity);
		return true;
	}
	
	if(strlen(entity_name) >= MAX_ENTITY_NAME_SIZE - 1)
	{
		return false;
	}
	
	int temp;
	int crc = crc32(entity_name);
	if(m_crc_names_map.find(crc, temp))
	{
		//is there already another entity with the same name?
		return temp == entity.index;
	}

	/// TODO: remove
	for(map<string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		if(it.second() == entity.index)
		{
			//update name
			it.first() = entity_name;
			break;
		}
	}
	//
	
	for(map<uint32_t, int>::iterator it = m_crc_names_map.begin(); it != m_crc_names_map.end(); ++it)
	{
		if(it.second() == entity.index)
		{
			//update name
			it.first() = crc;
			return true;
		}
	}
	m_crc_names_map.insert(crc, entity.index);

	//TODO: remove
	m_names_map.insert(entity_name, entity.index);
	return true;
}

void EntityNamesMap::serialize(ISerializer& serializer)
{	
	serializer.serialize("count", m_crc_names_map.size());
	for(map<uint32_t, int>::iterator it = m_crc_names_map.begin(); it != m_crc_names_map.end(); ++it)
	{
		serializer.serialize("key", it.first());
		serializer.serialize("id", it.second());
	}

	//TODO: remove
	serializer.serialize("count", m_names_map.size());
	for(map<Lux::string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		serializer.serialize("key", it.first().c_str());
		serializer.serialize("id", it.second());
	}
	//
}

void EntityNamesMap::deserialize(ISerializer& serializer)
{	
	m_crc_names_map.clear();
	int count = 0;
	int id;
	uint32_t key;
	serializer.deserialize("count", count);
	for(int i = 0; i < count; ++i)
	{
		serializer.deserialize("key", key);
		serializer.deserialize("id", id);

		m_crc_names_map.insert(key, id);
	}

	//TODO: remove
	m_names_map.clear();
	char key_string[MAX_ENTITY_NAME_SIZE];
	serializer.deserialize("count", count);
	for(int i = 0; i < count; ++i)
	{
		serializer.deserialize("key", key_string);
		serializer.deserialize("id", id);

		m_names_map.insert(key_string, id);
	}
	//	
}

} // !namespace Lux