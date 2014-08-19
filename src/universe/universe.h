#pragma once


#include "core/lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/quat.h"
#include "core/vec3.h"
#include "universe/component.h"
#include "universe/entity.h"


namespace Lumix
{


class Event;
class ISerializer;
struct Matrix;
struct Quat;
class Universe;
struct Vec3;


class LUMIX_ENGINE_API Universe final
{
	friend struct Entity;
	public:
		typedef Array<Entity::ComponentList> ComponentList;

	public:
		Universe();
		~Universe();

		void create();
		void destroy();

		Entity createEntity();
		void destroyEntity(Entity& entity);
		Vec3 getPosition(int index) { return m_positions[index]; }
		Quat getRotation(int index) { return m_rotations[index]; }
		Component addComponent(const Entity& entity, uint32_t component_type, void* system, int index);
		void removeComponent(const Component& cmp);
		int getEntityCount() const { return m_positions.size(); }

		DelegateList<void(Entity&)>& entityMoved() { return m_entity_moved; }
		DelegateList<void(Entity&)>& entityCreated() { return m_entity_created; }
		DelegateList<void(Entity&)>& entityDestroyed() { return m_entity_destroyed; }
		DelegateList<void(Component&)>& componentCreated() { return m_component_created; }
		DelegateList<void(const Component&)>& componentDestroyed() { return m_component_destroyed; }

		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);

	private:
		void onEvent(Event& event);
	
	private:
		Array<Vec3>		m_positions;		//< entity positions
		Array<Quat>		m_rotations;		//< entity rotations
		Array<int>		m_free_slots;
		ComponentList	m_component_list;
		DelegateList<void(Entity&)> m_entity_moved;
		DelegateList<void(Entity&)> m_entity_created;
		DelegateList<void(Entity&)> m_entity_destroyed;
		DelegateList<void(Component&)> m_component_created;
		DelegateList<void(const Component&)> m_component_destroyed;
};


} // !namespace Lumix
