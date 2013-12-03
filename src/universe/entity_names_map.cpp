#include "entity_names_map.h"

#include "core/json_serializer.h"

namespace Lux
{

const int MAX_ENTITY_NAME_SIZE = 32;

EntityNamesMap::EntityNamesMap(Universe* universe)
{
	m_universe = universe;
}

Entity EntityNamesMap::getEntityByName(const char* entity_name) const
{
	int index;
	if(m_names_map.find(entity_name, index))
	{
		return Entity(m_universe, index);
	}
	return Entity::INVALID;
}

const char* EntityNamesMap::getEntityName(int uid) const
{
	for(Lux::map<Lux::string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		if(it.second() == uid)
		{
			return it.first().c_str();
		}
	}
	return "";
}

void EntityNamesMap::removeEntityName(int uid)
{
	for(Lux::map<Lux::string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		if(it.second() == uid)
		{
			m_names_map.erase(it.first());
			break;
		}
	}
}

bool EntityNamesMap::setEntityName(const char* entity_name, int uid)
{
	int temp;
	if(m_names_map.find(entity_name, temp))
	{
		//is there already another entity with the same name?
		return temp == uid;
	}
	
	for(Lux::map<Lux::string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		if(it.second() == uid)
		{
			//update name
			it.first() = entity_name;
			return true;
		}
	}

	m_names_map.insert(entity_name, uid);
	return true;
}

void EntityNamesMap::serialize(ISerializer& serializer)
{
	serializer.serialize("count", m_names_map.size());
	for(Lux::map<Lux::string, int>::iterator it = m_names_map.begin(); it != m_names_map.end(); ++it)
	{
		serializer.serialize("key", it.first().c_str());
		serializer.serialize("id", it.second());
	}
}

void EntityNamesMap::deserialize(ISerializer& serializer)
{
	m_names_map.clear();
	
	int count = 0;
	int id;
	char key[MAX_ENTITY_NAME_SIZE];
	serializer.deserialize("count", count);
	for(int i = 0; i < count; ++i)
	{
		serializer.deserialize("key", key);
		serializer.deserialize("id", id);

		m_names_map.insert(key, id);
	}
}

} // !namespace Lux