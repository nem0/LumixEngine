#pragma once


#include "lumix.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/delegate_list.h"
#include "core/quat.h"
#include "core/string.h"
#include "core/vec3.h"
#include "universe/component.h"


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
	void createEntity(Entity entity);
	Entity createEntity();
	void destroyEntity(Entity entity);
	void addComponent(Entity entity,
					  uint32_t component_type,
					  IScene* scene,
					  int index);
	void destroyComponent(Entity entity,
						  uint32_t component_type,
						  IScene* scene,
						  int index);
	int getEntityCount() const
	{
		return m_transformations.size() - m_free_slots.size();
	}

	Entity getFirstEntity();
	Entity getNextEntity(Entity entity);
	bool nameExists(const char* name) const;
	const char* getEntityName(Entity entity) const;
	void setEntityName(Entity entity, const char* name);
	bool hasEntity(Entity entity) const;

	void setMatrix(Entity entity, const Matrix& mtx);
	Matrix getMatrix(Entity entity) const;
	void setRotation(Entity entity, float x, float y, float z, float w);
	void setRotation(Entity entity, const Quat& rot);
	void setPosition(Entity entity, float x, float y, float z);
	void setPosition(Entity entity, const Vec3& pos);
	void setScale(Entity entity, float scale);
	float getScale(Entity entity);
	const Vec3& getPosition(Entity entity) const
	{
		return m_transformations[entity].position;
	}
	const Quat& getRotation(Entity entity) const
	{
		return m_transformations[entity].rotation;
	}

	DelegateList<void(Entity)>& entityTransformed() { return m_entity_moved; }
	DelegateList<void(Entity)>& entityCreated() { return m_entity_created; }
	DelegateList<void(Entity)>& entityDestroyed() { return m_entity_destroyed; }

	DelegateList<void(const ComponentUID&)>& componentDestroyed()
	{
		return m_component_destroyed;
	}

	Delegate<void(const ComponentUID&)>& componentAdded()
	{
		return m_component_added;
	}

	void serialize(OutputBlob& serializer);
	void deserialize(InputBlob& serializer);

private:
	struct Transformation
	{
		Vec3 position;
		Quat rotation;
		float scale;
	};

private:
	IAllocator& m_allocator;
	Array<Transformation> m_transformations;
	Array<int> m_free_slots;
	AssociativeArray<uint32_t, uint32_t> m_name_to_id_map;
	AssociativeArray<uint32_t, string> m_id_to_name_map;
	DelegateList<void(Entity)> m_entity_moved;
	DelegateList<void(Entity)> m_entity_created;
	DelegateList<void(Entity)> m_entity_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_destroyed;
	Delegate<void(const ComponentUID&)> m_component_added;
};


} // !namespace Lumix
