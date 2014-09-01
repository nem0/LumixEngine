#pragma once


#include "core/lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/map.h"
#include "core/quat.h"
#include "core/string.h"
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

		Entity createEntity();
		void destroyEntity(Entity& entity);
		Vec3 getPosition(int index) { return m_positions[index]; }
		Quat getRotation(int index) { return m_rotations[index]; }
		Component addComponent(const Entity& entity, uint32_t component_type, IScene* scene, int index);
		void destroyComponent(const Component& cmp);
		int getEntityCount() const { return m_positions.size() - m_free_slots.size(); }
		Entity getFirstEntity();
		Entity getNextEntity(Entity entity);

		DelegateList<void(Entity&)>& entityMoved() { return m_entity_moved; }
		DelegateList<void(Entity&)>& entityCreated() { return m_entity_created; }
		DelegateList<void(Entity&)>& entityDestroyed() { return m_entity_destroyed; }
		DelegateList<void(Component&)>& componentCreated() { return m_component_created; }
		DelegateList<void(const Component&)>& componentDestroyed() { return m_component_destroyed; }

		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);

	private:
		Array<Vec3>		m_positions;
		Array<Quat>		m_rotations;
		Array<int>		m_free_slots;
		Map<uint32_t, uint32_t> m_name_to_id_map;
		Map<uint32_t, string> m_id_to_name_map;
		ComponentList	m_component_list;
		DelegateList<void(Entity&)> m_entity_moved;
		DelegateList<void(Entity&)> m_entity_created;
		DelegateList<void(Entity&)> m_entity_destroyed;
		DelegateList<void(Component&)> m_component_created;
		DelegateList<void(const Component&)> m_component_destroyed;
};


} // !namespace Lumix
