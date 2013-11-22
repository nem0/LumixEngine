#include "universe.h"
#include "core/event_manager.h"
//#include "physics/physics_system.h"
#include "physics/physics_scene.h"
#include "component_event.h"
#include "core/matrix.h"
#include "entity_moved_event.h"
#include "entity_destroyed_event.h"
#include "core/json_serializer.h"


namespace Lux
{


const Entity Entity::INVALID(0, -1);
const Component Component::INVALID(Entity(0, -1), 0, 0, -1);


Universe::~Universe()
{
	delete m_event_manager;
}


void Universe::destroy()
{
	m_free_slots.clear();
	m_positions.clear();
	m_rotations.clear();
	m_component_list.clear();
}


void Universe::create()
{
	m_positions.reserve(1000);
	m_rotations.reserve(1000);
	m_component_list.reserve(1000);
}


Universe::Universe()
{
	m_event_manager = new EventManager;
	m_event_manager->registerListener(ComponentEvent::type, this, &Universe::onEvent);
}


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
	assert(isValid());
	return universe->m_component_list[index];
}


const Component& Entity::getComponent(unsigned int type)
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
	universe->getEventManager()->emitEvent(EntityMovedEvent(*this));
}


void Entity::setMatrix(const Matrix& mtx)
{
	Quat rot;
	mtx.getRotation(rot);
	universe->m_positions[index] = mtx.getTranslation();
	universe->m_rotations[index] = rot;
	universe->getEventManager()->emitEvent(EntityMovedEvent(*this));
}


void Entity::setPosition(float x, float y, float z)
{
	universe->m_positions[index].set(x, y, z);
	universe->getEventManager()->emitEvent(EntityMovedEvent(*this));
}


void Entity::setPosition(const Vec3& pos)
{
	universe->m_positions[index] = pos;
	universe->getEventManager()->emitEvent(EntityMovedEvent(*this));
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
	universe->getEventManager()->emitEvent(EntityMovedEvent(*this));
}


void Entity::setRotation(const Quat& rot)
{
	universe->m_rotations[index] = rot;
	universe->getEventManager()->emitEvent(EntityMovedEvent(*this));
}


Entity Universe::createEntity()
{
	
	if(m_free_slots.empty())
	{
		m_positions.push_back(Vec3(0, 0, 0));
		m_rotations.push_back(Quat(0, 0, 0, 1));
		m_component_list.push_back_empty();
		return Entity(this, m_positions.size() - 1);
	}
	else
	{
		Entity e(this, m_free_slots.back());
		m_free_slots.pop_back();
		m_positions[e.index].set(0, 0, 0);
		m_rotations[e.index].set(0, 0, 0, 1);
		m_component_list[e.index].clear();
		return e;
	}
}


void Universe::destroyEntity(Entity entity)
{
	if(entity.isValid())
	{
		m_free_slots.push_back(entity.index);
		m_event_manager->emitEvent(EntityDestroyedEvent(entity));
		m_component_list[entity.index].clear();
	}
}


void Universe::onEvent(void* data, Event& event)
{
	static_cast<Universe*>(data)->onEvent(event);
}


void Universe::onEvent(Event& evt)
{
	if(evt.getType() == ComponentEvent::type)
	{
		ComponentEvent& e = static_cast<ComponentEvent&>(evt);
		if(e.is_created)
		{
			m_component_list[e.component.entity.index].push_back(e.component);
		}
		else
		{
			vector<Component>& list = m_component_list[e.component.entity.index];
			for(int i = 0, c = list.size(); i < c; ++i)
			{
				if(list[i] == e.component)
				{
					list.eraseFast(i);
					break;
				}
			}
		}
	}
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
	serializer.deserialize("count", count);
	m_component_list.resize(count);
	m_positions.resize(count);
	m_rotations.resize(count);
	serializer.deserializeArrayBegin("positions");
	for(int i = 0; i < count; ++i)
	{
		serializer.deserializeArrayItem(m_positions[i].x);
		serializer.deserializeArrayItem(m_positions[i].y);
		serializer.deserializeArrayItem(m_positions[i].z);
	}
	serializer.deserializeArrayEnd();
	serializer.deserializeArrayBegin("rotations");
	for(int i = 0; i < count; ++i)
	{
		serializer.deserializeArrayItem(m_rotations[i].x);
		serializer.deserializeArrayItem(m_rotations[i].y);
		serializer.deserializeArrayItem(m_rotations[i].z);
		serializer.deserializeArrayItem(m_rotations[i].w);
	}
	serializer.deserializeArrayEnd();
	serializer.deserialize("free_slot_count", count);
	m_free_slots.resize(count);
	serializer.deserializeArrayBegin("free_slots");
	for(int i = 0; i < count; ++i)
	{
		serializer.deserializeArrayItem(m_free_slots[i]);
	}
	serializer.deserializeArrayEnd();
}



} // !namespace Lux