#pragma once


#include "core/lux.h"
#include "core/vector.h"
#include "core/vec3.h"
#include "core/quat.h"



namespace Lux
{


class EventManager;
struct Vec3;
struct Quat;
class Event;
class Universe;
struct Matrix;
struct Component;
class ISerializer;


struct LUX_ENGINE_API Entity LUX_FINAL
{
	typedef vector<Component> ComponentList;

	Entity() {}
	Entity(Universe* uni, int i) : index(i), universe(uni) {}

	Matrix getMatrix() const;
	void getMatrix(Matrix& mtx) const;
	void setMatrix(const Vec3& pos, const Quat& rot);
	void setMatrix(const Matrix& mtx);
	void setPosition(float x, float y, float z);
	void setPosition(const Vec3& v);
	const Vec3& getPosition() const;
	const Quat& getRotation() const;
	void setRotation(float x, float y, float z, float w);
	void setRotation(const Quat& rot);
	void translate(const Vec3& t);
	bool isValid() const { return index >= 0; }
	const Component& getComponent(uint32_t type);
	const ComponentList& getComponents() const;
	bool existsInUniverse() const;

	bool operator ==(const Entity& rhs) const;

	int index;
	Universe* universe;

	static const Entity INVALID;
};


struct LUX_ENGINE_API Component LUX_FINAL
{
	typedef uint32_t Type;

	Component() { index = -1; }
	Component(Entity _entity, uint32_t _type, void* _system, int _index)
		: entity(_entity)
		, type(_type)
		, system(_system)
		, index(_index)
	{
	}

	Entity entity;
	Type type;
	void* system;
	int index;

	static const Component INVALID;

	bool operator ==(const Component& rhs) const { return type == rhs.type && system == rhs.system && index == rhs.index; }
	bool operator !=(const Component& rhs) const { return type != rhs.type || system != rhs.system || index != rhs.index; }
	bool isValid() const  { return index >= 0; }
};


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
		void destroyEntity(Entity entity);
		Vec3 getPosition(int index) { return m_positions[index]; }
		Quat getRotation(int index) { return m_rotations[index]; }
		EventManager* getEventManager() const { return m_event_manager; }

		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);

	private:
		static void onEvent(void* data, Event& event);
		void onEvent(Event& event);
	
	private:
		vector<Vec3>		m_positions;		//< entity positions
		vector<Quat>		m_rotations;		//< entity rotations
		vector<int>			m_free_slots;
		ComponentList		m_component_list;
		EventManager*		m_event_manager;
};


} // !namespace Lux
