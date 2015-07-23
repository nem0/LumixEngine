#pragma once


#include "core/lumix.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/delegate_list.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec3.h"
#include "universe/component.h"
#include "universe/entity.h"


namespace Lumix
{


class InputBlob;
class Event;
struct Matrix;
class OutputBlob;
struct Quat;
class Universe;
struct Vec3;


class LUMIX_ENGINE_API Universe final
{
	public:
		Universe(IAllocator& allocator);
		~Universe();

		IAllocator& getAllocator() { return m_allocator; }
		void createEntity(const Entity& entity);
		Entity createEntity();
		void destroyEntity(Entity& entity);
		ComponentOld addComponent(const Entity& entity, uint32_t component_type, IScene* scene, int index);
		void destroyComponent(const ComponentOld& cmp);
		int getEntityCount() const { return m_positions.size() - m_free_slots.size(); }
		Entity getFirstEntity();
		Entity getNextEntity(Entity entity);
		bool nameExists(const char* name) const;
		const char* getEntityName(Entity entity) const;
		void setEntityName(Entity entity, const char* name);
		bool hasEntity(Entity entity) const;

		void setMatrix(int entity_index, const Matrix& mtx);
		Matrix getMatrix(int entity_index) const;
		void setRotation(int entity_index, float x, float y, float z, float w);
		void setRotation(int entity_index, const Quat& rot);
		void setPosition(int entity_index, float x, float y, float z);
		void setPosition(int entity_index, const Vec3& pos);
		const Vec3& getPosition(int index) const { return m_positions[index]; }
		const Quat& getRotation(int index) const { return m_rotations[index]; }

		DelegateList<void(const Entity&)>& entityMoved() { return m_entity_moved; }
		DelegateList<void(const Entity&)>& entityCreated() { return m_entity_created; }
		DelegateList<void(const Entity&)>& entityDestroyed() { return m_entity_destroyed; }
		DelegateList<void(const ComponentOld&)>& componentCreated() { return m_component_created; }
		DelegateList<void(const ComponentOld&)>& componentDestroyed() { return m_component_destroyed; }

		Delegate<void(const ComponentOld&)>& componentAdded() { return m_component_added; }

		void serialize(OutputBlob& serializer);
		void deserialize(InputBlob& serializer);

	private:
		IAllocator&		m_allocator;
		Array<Vec3>		m_positions;
		Array<Quat>		m_rotations;
		Array<int>		m_free_slots;
		AssociativeArray<uint32_t, uint32_t> m_name_to_id_map;
		AssociativeArray<uint32_t, string> m_id_to_name_map;
		DelegateList<void(const Entity&)> m_entity_moved;
		DelegateList<void(const Entity&)> m_entity_created;
		DelegateList<void(const Entity&)> m_entity_destroyed;
		DelegateList<void(const ComponentOld&)> m_component_created;
		DelegateList<void(const ComponentOld&)> m_component_destroyed;
		Delegate<void(const ComponentOld&)> m_component_added;
};


} // !namespace Lumix
