#pragma once


#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/delegate_list.h"
#include "engine/iplugin.h"
#include "engine/lumix.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/quat.h"
#include "engine/string.h"
#include "engine/universe/component.h"


namespace Lumix
{


class InputBlob;
struct IDeserializer;
struct ISerializer;
struct Matrix;
class OutputBlob;
struct Transform;
class Universe;
struct PrefabResource;


enum
{
	MAX_COMPONENTS_TYPES_COUNT = 64
};


class LUMIX_ENGINE_API Universe
{
public:
	typedef void (IScene::*Serialize)(ISerializer&, ComponentHandle);
	typedef void (IScene::*Deserialize)(IDeserializer&, Entity, int);
	struct ComponentTypeEntry
	{
		IScene* scene;
		void (IScene::*serialize)(ISerializer&, ComponentHandle);
		void (IScene::*deserialize)(IDeserializer&, Entity, int);
	};

public:
	explicit Universe(IAllocator& allocator);
	~Universe();

	IAllocator& getAllocator() { return m_allocator; }
	void createEntity(Entity entity);
	Entity createEntity(const Vec3& position, const Quat& rotation);
	void destroyEntity(Entity entity);
	void addComponent(Entity entity, ComponentType component_type, IScene* scene, ComponentHandle index);
	void destroyComponent(Entity entity, ComponentType component_type, IScene* scene, ComponentHandle index);
	bool hasComponent(Entity entity, ComponentType component_type) const;
	ComponentUID getComponent(Entity entity, ComponentType type) const;
	ComponentUID getFirstComponent(Entity entity) const;
	ComponentUID getNextComponent(const ComponentUID& cmp) const;
	ComponentTypeEntry& registerComponentType(ComponentType type) { return m_component_type_map[type.index]; }
	template <typename T1, typename T2>
	void registerComponentType(ComponentType type, IScene* scene, T1 serialize, T2 deserialize)
	{
		m_component_type_map[type.index].scene = scene;
		m_component_type_map[type.index].serialize = static_cast<Serialize>(serialize);
		m_component_type_map[type.index].deserialize = static_cast<Deserialize>(deserialize);
	}

	Entity getFirstEntity() const;
	Entity getNextEntity(Entity entity) const;
	bool nameExists(const char* name) const;
	const char* getEntityName(Entity entity) const;
	void setEntityName(Entity entity, const char* name);
	bool hasEntity(Entity entity) const;

	bool isDescendant(Entity ancestor, Entity descendant) const;
	Entity getParent(Entity entity) const;
	Entity getFirstChild(Entity entity) const;
	Entity getNextSibling(Entity entity) const;
	Transform getLocalTransform(Entity entity) const;
	float getLocalScale(Entity entity) const;
	void setParent(Entity parent, Entity child);
	void setLocalPosition(Entity entity, const Vec3& pos);
	void setLocalRotation(Entity entity, const Quat& rot);
	void setLocalTransform(Entity entity, const Transform& transform, float scale);
	Transform computeLocalTransform(Entity parent, const Transform& global_transform) const;

	void setMatrix(Entity entity, const Matrix& mtx);
	Matrix getPositionAndRotation(Entity entity) const;
	Matrix getMatrix(Entity entity) const;
	void setTransform(Entity entity, const Transform& transform);
	void setTransformKeepChildren(Entity entity, const Transform& transform, float scale);
	void setTransform(Entity entity, const Transform& transform, float scale);
	void setTransform(Entity entity, const Vec3& pos, const Quat& rot);
	Transform getTransform(Entity entity) const;
	void setRotation(Entity entity, float x, float y, float z, float w);
	void setRotation(Entity entity, const Quat& rot);
	void setPosition(Entity entity, float x, float y, float z);
	void setPosition(Entity entity, const Vec3& pos);
	void setScale(Entity entity, float scale);
	Entity instantiatePrefab(const PrefabResource& prefab,
		const Vec3& pos,
		const Quat& rot,
		float scale);
	float getScale(Entity entity);
	const Vec3& getPosition(Entity entity) const;
	const Quat& getRotation(Entity entity) const;
	const char* getName() const { return m_name; }
	void setName(const char* name) 
	{ 
		m_name = name; 
	}

	DelegateList<void(Entity)>& entityTransformed() { return m_entity_moved; }
	DelegateList<void(Entity)>& entityCreated() { return m_entity_created; }
	DelegateList<void(Entity)>& entityDestroyed() { return m_entity_destroyed; }
	DelegateList<void(const ComponentUID&)>& componentDestroyed() { return m_component_destroyed; }
	DelegateList<void(const ComponentUID&)>& componentAdded() { return m_component_added; }

	void serializeComponent(ISerializer& serializer, ComponentType type, ComponentHandle cmp);
	void deserializeComponent(IDeserializer& serializer, Entity entity, ComponentType type, int scene_version);
	void serialize(OutputBlob& serializer);
	void deserialize(InputBlob& serializer);

	IScene* getScene(ComponentType type) const;
	IScene* getScene(u32 hash) const;
	Array<IScene*>& getScenes();
	void addScene(IScene* scene);

private:
	void transformEntity(Entity entity, bool update_local);
	void updateGlobalTransform(Entity entity);

	struct Hierarchy
	{
		Entity entity;
		Entity parent;
		Entity first_child;
		Entity next_sibling;

		Transform local_transform;
		float local_scale;
	};


	struct EntityData
	{
		EntityData() {}

		Vec3 position;
		Quat rotation;
		
		int hierarchy;

		union
		{
			struct 
			{
				float scale;
				u64 components;
			};
			struct
			{
				int prev;
				int next;
			};
		};
		bool valid;
	};

private:
	IAllocator& m_allocator;
	ComponentTypeEntry m_component_type_map[MAX_COMPONENTS_TYPES_COUNT];
	Array<IScene*> m_scenes;
	Array<EntityData> m_entities;
	Array<Hierarchy> m_hierarchy;
	AssociativeArray<u32, u32> m_name_to_id_map;
	AssociativeArray<u32, string> m_id_to_name_map;
	DelegateList<void(Entity)> m_entity_moved;
	DelegateList<void(Entity)> m_entity_created;
	DelegateList<void(Entity)> m_entity_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_added;
	int m_first_free_slot;
	StaticString<64> m_name;
};


} // !namespace Lumix
