#include "universe.h"
#include "core/event_manager.h"
#include "component_event.h"
#include "core/matrix.h"
#include "entity_moved_event.h"
#include "entity_destroyed_event.h"
#include "core/json_serializer.h"


namespace Lux
{


Universe::~Universe()
{
	LUX_DELETE(m_event_manager);
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
	m_event_manager = LUX_NEW(EventManager);
	m_event_manager->addListener(ComponentEvent::type).bind<Universe, &Universe::onEvent>(this);
}





Entity Universe::createEntity()
{
	
	if(m_free_slots.empty())
	{
		m_positions.push(Vec3(0, 0, 0));
		m_rotations.push(Quat(0, 0, 0, 1));
		m_component_list.pushEmpty();
		return Entity(this, m_positions.size() - 1);
	}
	else
	{
		Entity e(this, m_free_slots.back());
		m_free_slots.pop();
		m_positions[e.index].set(0, 0, 0);
		m_rotations[e.index].set(0, 0, 0, 1);
		m_component_list[e.index].clear();
		return e;
	}
}


void Universe::destroyEntity(const Entity& entity)
{
	if(entity.isValid())
	{
		m_free_slots.push(entity.index);
		EntityDestroyedEvent evt(entity);
		m_event_manager->emitEvent(evt);
		m_component_list[entity.index].clear();
	}
}


void Universe::onEvent(Event& evt)
{
	if(evt.getType() == ComponentEvent::type)
	{
		ComponentEvent& e = static_cast<ComponentEvent&>(evt);
		if(e.is_created)
		{
			m_component_list[e.component.entity.index].push(e.component);
		}
		else
		{
			Array<Component>& list = m_component_list[e.component.entity.index];
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
	m_component_list.clear();
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



void Universe::addComponent(const Entity& entity, uint32_t component_type, void* system, int index)
{
	Component cmp(entity, component_type, system, index);
	m_component_list[entity.index].push(cmp);
}


} // !namespace Lux