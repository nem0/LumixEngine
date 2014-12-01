#include "universe/entity.h"
#include "core/crc32.h"
#include "universe/universe.h"

namespace Lumix
{

	const Entity Entity::INVALID(0, -1);


	bool Entity::existsInUniverse() const
	{

		for(int i = 0; i < universe->m_free_slots.size(); ++i)
		{
			if(universe->m_free_slots[i] == index)
				return false;
		}	
		return index != -1;
	}


	const Quat& Entity::getRotation() const
	{
		return universe->m_rotations[index];
	}


	const Vec3& Entity::getPosition() const
	{
		return universe->m_positions[index];
	}


	Matrix Entity::getMatrix() const
	{
		Matrix mtx;
		universe->m_rotations[index].toMatrix(mtx);
		mtx.setTranslation(universe->m_positions[index]);
		return mtx;
	}


	void Entity::getMatrix(Matrix& mtx) const
	{
		universe->m_rotations[index].toMatrix(mtx);
		mtx.setTranslation(universe->m_positions[index]);
	}


	void Entity::setMatrix(const Vec3& pos, const Quat& rot)
	{
		universe->m_positions[index] = pos;
		universe->m_rotations[index] = rot;
		universe->entityMoved().invoke(*this);
	}


	void Entity::setMatrix(const Matrix& mtx)
	{
		Quat rot;
		mtx.getRotation(rot);
		universe->m_positions[index] = mtx.getTranslation();
		universe->m_rotations[index] = rot;
		universe->entityMoved().invoke(*this);
	}


	void Entity::setPosition(float x, float y, float z) const
	{
		universe->m_positions[index].set(x, y, z);
		universe->entityMoved().invoke(*this);
	}


	void Entity::setPosition(const Vec3& pos) const
	{
		universe->m_positions[index] = pos;
		universe->entityMoved().invoke(*this);
	}


	bool Entity::operator ==(const Entity& rhs) const
	{
		return index == rhs.index && universe == rhs.universe;
	}


	void Entity::translate(const Vec3& t)
	{
		universe->m_positions[index] += t;
	}


	void Entity::setRotation(float x, float y, float z, float w) const
	{
		universe->m_rotations[index].set(x, y, z, w);
		universe->entityMoved().invoke(*this);
	}


	void Entity::setRotation(const Quat& rot) const
	{
		universe->m_rotations[index] = rot;
		universe->entityMoved().invoke(*this);
	}

	const char* Entity::getName() const
	{
		int name_index = universe->m_id_to_name_map.find(index);
		return name_index < 0 ? "" : universe->m_id_to_name_map.at(name_index).c_str();
	}


	void Entity::setName(const char* name)
	{
		int name_index = universe->m_id_to_name_map.find(index);
		if (name_index >= 0)
		{
			uint32_t hash = crc32(universe->m_id_to_name_map.at(name_index).c_str());
			universe->m_name_to_id_map.erase(hash);
			universe->m_id_to_name_map.eraseAt(name_index);
		}

		if (name && name[0] != '\0')
		{
			universe->m_name_to_id_map.insert(crc32(name), index);
			universe->m_id_to_name_map.insert(index, string(name, universe->getAllocator()));
		}
	}


} // ~namespace Lumix

