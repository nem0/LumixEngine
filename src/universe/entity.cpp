#include "universe/entity.h"
#include "universe/universe.h"
#include "core/event_manager.h"
#include "universe/entity_moved_event.h"

namespace Lux
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


	const Entity::ComponentList& Entity::getComponents() const
	{
		ASSERT(isValid());
		return universe->m_component_list[index];
	}


	const Component& Entity::getComponent(uint32_t type) const
	{
		const Entity::ComponentList& cmps = getComponents();
		for(int i = 0, c = cmps.size(); i < c; ++i)
		{
			if(cmps[i].type == type)
			{
				return cmps[i];
			}
		}
		return Component::INVALID;
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
		EntityMovedEvent evt(*this);
		universe->getEventManager()->emitEvent(evt);
	}


	void Entity::setMatrix(const Matrix& mtx)
	{
		Quat rot;
		mtx.getRotation(rot);
		universe->m_positions[index] = mtx.getTranslation();
		universe->m_rotations[index] = rot;
		EntityMovedEvent evt(*this);
		universe->getEventManager()->emitEvent(evt);
	}


	void Entity::setPosition(float x, float y, float z)
	{
		universe->m_positions[index].set(x, y, z);
		EntityMovedEvent evt(*this);
		universe->getEventManager()->emitEvent(evt);
	}


	void Entity::setPosition(const Vec3& pos)
	{
		universe->m_positions[index] = pos;
		EntityMovedEvent evt(*this);
		universe->getEventManager()->emitEvent(evt);
	}


	bool Entity::operator ==(const Entity& rhs) const
	{
		return index == rhs.index && universe == rhs.universe;
	}


	void Entity::translate(const Vec3& t)
	{
		universe->m_positions[index] += t;
	}


	void Entity::setRotation(float x, float y, float z, float w)
	{
		universe->m_rotations[index].set(x, y, z, w);
		EntityMovedEvent evt(*this);
		universe->getEventManager()->emitEvent(evt);
	}


	void Entity::setRotation(const Quat& rot)
	{
		universe->m_rotations[index] = rot;
		EntityMovedEvent evt(*this);
		universe->getEventManager()->emitEvent(evt);
	} 


} // ~namespace Lux

