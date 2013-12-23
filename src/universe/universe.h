#pragma once


#include "core/lux.h"
#include "core/vector.h"
#include "core/vec3.h"
#include "core/quat.h"
#include "universe/component.h"
#include "universe/entity.h"


namespace Lux
{


class Event;
class EventManager;
class ISerializer;
struct Matrix;
struct Quat;
class Universe;
struct Vec3;


class LUX_ENGINE_API Universe LUX_FINAL
{
	friend struct Entity;
	public:
		typedef vector<Entity::ComponentList> ComponentList;

	public:
		Universe();
		~Universe();

		void create();
		void destroy();

		Entity createEntity();
		void destroyEntity(const Entity& entity);
		Vec3 getPosition(int index) { return m_positions[index]; }
		Quat getRotation(int index) { return m_rotations[index]; }
		EventManager* getEventManager() const { return m_event_manager; }

		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);

	private:
		void onEvent(Event& event);
	
	private:
		vector<Vec3>		m_positions;		//< entity positions
		vector<Quat>		m_rotations;		//< entity rotations
		vector<int>			m_free_slots;
		ComponentList		m_component_list;
		EventManager*		m_event_manager;
};


} // !namespace Lux
