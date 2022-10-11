#pragma once


#include "engine/array.h"
#include "engine/delegate_list.h"
#include "engine/lumix.h"
#include "engine/math.h"


namespace Lumix {

struct ComponentUID;
struct IScene;

enum class UniverseSerializedVersion : u32
{
	CAMERA,
	ENTITY_FOLDERS,
	HASH64,

	LATEST
};

#pragma pack(1)
	struct UniverseHeader {
		static const u32 MAGIC = 'LUNV';
		u32 magic;
		UniverseSerializedVersion version;
	};
#pragma pack()

struct LUMIX_ENGINE_API EntityMap final {
	EntityMap(IAllocator& allocator);
	~EntityMap() = default;
	void reserve(u32 count);
	EntityPtr get(EntityPtr e) const;
	EntityRef get(EntityRef e) const;
	void set(EntityRef src, EntityRef dst);

	Array<EntityPtr> m_map;
};


struct LUMIX_ENGINE_API Universe {
	enum { ENTITY_NAME_MAX_LENGTH = 32 };

	struct EntityData {
		EntityData() {}

		i32 hierarchy;
		i32 name;

		union {
			u64 components;
			struct {
				int prev;
				int next;
			};
		};
		bool valid;
	};

	explicit Universe(struct Engine& engine, IAllocator& allocator);
	~Universe();

	IAllocator& getAllocator() { return m_allocator; }
	const Transform* getTransforms() const { return m_transforms.begin(); }
	void emplaceEntity(EntityRef entity);
	EntityRef createEntity(const DVec3& position, const Quat& rotation);
	void destroyEntity(EntityRef entity);
	void createComponent(ComponentType type, EntityRef entity);
	void destroyComponent(EntityRef entity, ComponentType type);
	void onComponentCreated(EntityRef entity, ComponentType component_type, IScene* scene);
	void onComponentDestroyed(EntityRef entity, ComponentType component_type, IScene* scene);
    u64 getComponentsMask(EntityRef entity) const;
    bool hasComponent(EntityRef entity, ComponentType component_type) const;
	ComponentUID getComponent(EntityRef entity, ComponentType type) const;
	ComponentUID getFirstComponent(EntityRef entity) const;
	ComponentUID getNextComponent(const ComponentUID& cmp) const;

	bool isValid(EntityRef e) const { return m_entities[e.index].valid; }
	EntityPtr getFirstEntity() const;
	EntityPtr getNextEntity(EntityRef entity) const;
	const char* getEntityName(EntityRef entity) const;
	EntityPtr findByName(EntityPtr parent, const char* name);
	void setEntityName(EntityRef entity, const char* name);
	bool hasEntity(EntityRef entity) const;

	bool isDescendant(EntityRef ancestor, EntityRef descendant) const;
	EntityPtr getParent(EntityRef entity) const;
	EntityPtr getFirstChild(EntityRef entity) const;
	EntityPtr getNextSibling(EntityRef entity) const;
	Transform getLocalTransform(EntityRef entity) const;
	float getLocalScale(EntityRef entity) const;
	void setParent(EntityPtr parent, EntityRef child);
	void setLocalPosition(EntityRef entity, const DVec3& pos);
	void setLocalRotation(EntityRef entity, const Quat& rot);
	void setLocalTransform(EntityRef entity, const Transform& transform);

	Matrix getRelativeMatrix(EntityRef entity, const DVec3& base_pos) const;
	void setTransform(EntityRef entity, const RigidTransform& transform);
	void setTransform(EntityRef entity, const Transform& transform);
	void setTransformKeepChildren(EntityRef entity, const Transform& transform);
	void setTransform(EntityRef entity, const DVec3& pos, const Quat& rot, float scale);
	const Transform& getTransform(EntityRef entity) const;
	void setRotation(EntityRef entity, float x, float y, float z, float w);
	void setRotation(EntityRef entity, const Quat& rot);
	void setPosition(EntityRef entity, const DVec3& pos);
	void setScale(EntityRef entity, float scale);
	float getScale(EntityRef entity) const;
	const DVec3& getPosition(EntityRef entity) const;
	const Quat& getRotation(EntityRef entity) const;
	const char* getName() const { return m_name; }
	void setName(const char* name);

	DelegateList<void(EntityRef)>& entityCreated() { return m_entity_created; }
	DelegateList<void(EntityRef)>& entityTransformed() { return m_entity_moved; }
	DelegateList<void(EntityRef)>& entityDestroyed() { return m_entity_destroyed; }
	DelegateList<void(const ComponentUID&)>& componentDestroyed() { return m_component_destroyed; }
	DelegateList<void(const ComponentUID&)>& componentAdded() { return m_component_added; }

	void serialize(struct OutputMemoryStream& serializer);
	void deserialize(struct InputMemoryStream& serializer, EntityMap& entity_map);

	IScene* getScene(ComponentType type) const;
	IScene* getScene(const char* name) const;
	Array<UniquePtr<IScene>>& getScenes();
	void addScene(UniquePtr<IScene>&& scene);

private:
	void transformEntity(EntityRef entity, bool update_local);
	void updateGlobalTransform(EntityRef entity);

	struct Hierarchy {
		EntityRef entity;
		EntityPtr parent;
		EntityPtr first_child;
		EntityPtr next_sibling;

		Transform local_transform;
	};

	struct EntityName {
		EntityRef entity;
		char name[ENTITY_NAME_MAX_LENGTH];
	};

	struct ComponentTypeEntry {
		IScene* scene = nullptr;
		void (*create)(IScene*, EntityRef);
		void (*destroy)(IScene*, EntityRef);
	};

	IAllocator& m_allocator;
	Engine& m_engine;
	ComponentTypeEntry m_component_type_map[ComponentType::MAX_TYPES_COUNT];
	Array<UniquePtr<IScene>> m_scenes;
	Array<Transform> m_transforms;
	Array<EntityData> m_entities;
	Array<Hierarchy> m_hierarchy;
	Array<EntityName> m_names;
	DelegateList<void(EntityRef)> m_entity_created;
	DelegateList<void(EntityRef)> m_entity_moved;
	DelegateList<void(EntityRef)> m_entity_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_added;
	int m_first_free_slot;
	char m_name[64];
};

struct LUMIX_ENGINE_API ComponentUID final {
	ComponentUID() {
		scene = nullptr;
		entity = INVALID_ENTITY;
		type = {-1};
	}

	ComponentUID(EntityPtr _entity, ComponentType _type, IScene* _scene)
		: entity(_entity)
		, type(_type)
		, scene(_scene) {}

	EntityPtr entity;
	ComponentType type;
	IScene* scene;

	static const ComponentUID INVALID;

	bool operator==(const ComponentUID& rhs) const { return type == rhs.type && scene == rhs.scene && entity == rhs.entity; }
	bool isValid() const { return entity.isValid(); }
};

} // namespace Lumix
