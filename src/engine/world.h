#pragma once

#include "engine/black.h.h"

#include "core/array.h"
#include "core/delegate_list.h"
#include "core/math.h"
#include "core/tag_allocator.h"


namespace black {

struct ComponentUID;
struct IModule;
struct ChildrenRange;

enum class WorldVersion : u32 {
	EDITOR_CAMERA,
	ENTITY_FOLDERS,
	HASH64,
	NEW_ENTITY_FOLDERS,
	MERGED_HEADERS,
	COMPRESSED,

	LATEST
};

enum class WorldSerializeFlags : u32 { 
	HAS_PARTITIONS = 1 << 0,

	NONE = 0
};

// map one EntityPtr to another, used e.g. during additive loading or when instancing a prefab
struct BLACK_ENGINE_API EntityMap final {
	EntityMap(IAllocator& allocator);
	~EntityMap() = default;
	void reserve(u32 count);
	EntityPtr get(EntityPtr e) const;
	EntityRef get(EntityRef e) const;
	void set(EntityRef src, EntityRef dst);

	Array<EntityPtr> m_map;
};

// manages entities - contains basic entity data such as transforms or names
// most of the components (rendering, animation, navigation, ...) are implemented in `IModule`s 
// Each world has one instance of every module inherited from `IModule`
struct BLACK_ENGINE_API World {
	enum { ENTITY_NAME_MAX_LENGTH = 32 };
	using PartitionHandle = u16;
	using ArchetypeHandle = u16;

	// `Partition` is a set of entities, single world can have multiple partitions
	// used for additive loading/unloading
	struct Partition {
		PartitionHandle handle;
		char name[64];
	};

	explicit World(struct Engine& engine);
	~World();

	IAllocator& getAllocator() { return m_allocator; }
	const Transform* getTransforms() const { return m_transforms.begin(); }
	void emplaceEntity(EntityRef entity);
	EntityRef createEntity(const DVec3& position, const Quat& rotation);
	void destroyEntity(EntityRef entity);
	void createComponent(ComponentType type, EntityRef entity);
	void destroyComponent(EntityRef entity, ComponentType type);
	void onComponentCreated(EntityRef entity, ComponentType component_type, IModule* module);
	void onComponentDestroyed(EntityRef entity, ComponentType component_type, IModule* module);
	bool hasComponent(EntityRef entity, ComponentType component_type) const;
	Span<const ComponentType> getComponents(EntityRef entity) const;

	PartitionHandle createPartition(const char* name);
	void destroyPartition(PartitionHandle partition);
	void setActivePartition(PartitionHandle partition);
	PartitionHandle getActivePartition() const { return m_active_partition; }
	Array<Partition>& getPartitions() { return m_partitions; }
	Partition& getPartition(PartitionHandle partition);
	PartitionHandle getPartition(EntityRef entity);
	void setPartition(EntityRef entity, PartitionHandle partition);

	EntityPtr getFirstEntity() const;
	EntityPtr getNextEntity(EntityRef entity) const;
	const char* getEntityName(EntityRef entity) const;
	EntityPtr findByName(EntityPtr parent, const char* name);
	void setEntityName(EntityRef entity, struct StringView name);
	bool hasEntity(EntityRef entity) const;

	bool isDescendant(EntityRef ancestor, EntityRef descendant) const;
	EntityPtr getParent(EntityRef entity) const;
	EntityPtr getFirstChild(EntityRef entity) const;
	EntityPtr getNextSibling(EntityRef entity) const;
	ChildrenRange childrenOf(EntityRef entity) const;

	Transform getLocalTransform(EntityRef entity) const;
	Vec3 getLocalScale(EntityRef entity) const;
	void setParent(EntityPtr parent, EntityRef child);
	void setLocalPosition(EntityRef entity, const DVec3& pos);
	void setLocalRotation(EntityRef entity, const Quat& rot);
	void setLocalTransform(EntityRef entity, const Transform& transform);

	Matrix getRelativeMatrix(EntityRef entity, const DVec3& base_pos) const;
	void setTransform(EntityRef entity, const RigidTransform& transform);
	void setTransform(EntityRef entity, const Transform& transform);
	void setTransformKeepChildren(EntityRef entity, const Transform& transform);
	void setTransform(EntityRef entity, const DVec3& pos, const Quat& rot, const Vec3& scale);
	const Transform& getTransform(EntityRef entity) const;
	void setRotation(EntityRef entity, float x, float y, float z, float w);
	void setRotation(EntityRef entity, const Quat& rot);
	void setPosition(EntityRef entity, const DVec3& pos);
	void setScale(EntityRef entity, const Vec3& scale);
	const Vec3& getScale(EntityRef entity) const;
	const DVec3& getPosition(EntityRef entity) const;
	const Quat& getRotation(EntityRef entity) const;

	DelegateList<void(EntityRef)>& entityCreated() { return m_entity_created; }
	DelegateList<void(EntityRef)>& entityDestroyed() { return m_entity_destroyed; }
	DelegateList<void(const ComponentUID&)>& componentDestroyed() { return m_component_destroyed; }
	DelegateList<void(const ComponentUID&)>& componentAdded() { return m_component_added; }
	DelegateList<void(EntityRef)>& componentTransformed(ComponentType type);

	void serialize(struct OutputMemoryStream& serializer, WorldSerializeFlags flags);
	[[nodiscard]] bool deserialize(struct InputMemoryStream& serializer, EntityMap& entity_map, WorldVersion& version);

	IModule* getModule(ComponentType type) const;
	IModule* getModule(const char* name) const;
	Array<UniquePtr<IModule>>& getModules();
	void addModule(UniquePtr<IModule>&& moudle);

private:
	void transformEntity(EntityRef entity, bool update_local);
	void updateGlobalTransform(EntityRef entity);

	struct EntityData {
		EntityData() {}

		i32 hierarchy; // index into m_hierarchy, < 0 if no hierarchy (== no parent & no children) 
		i32 name; // index into m_names, < 0 if no name

		union {
			struct {
				PartitionHandle partition;
				ArchetypeHandle archetype;
			};
			struct {
				// freelist indices
				int prev; 
				int next;
			};
		};
		bool valid = false;
	};

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
		ComponentTypeEntry(IAllocator& allocator) : transformed(allocator) {}
		IModule* module = nullptr;
		void (*create)(IModule*, EntityRef);
		void (*destroy)(IModule*, EntityRef);
		DelegateList<void(EntityRef)> transformed;
	};


	TagAllocator m_allocator;
	Engine& m_engine;
	Local<ComponentTypeEntry> m_component_type_map[ComponentType::MAX_TYPES_COUNT];
	Array<UniquePtr<IModule>> m_modules;
	struct ArchetypeManager;
	UniquePtr<ArchetypeManager> m_archetype_manager;
	
	// m_entities/m_transforms are indexed by EntityRef::index
	// not in single array (==EntityData does not contain Transform) because of cache/performance
	Array<EntityData> m_entities;
	Array<Transform> m_transforms;
	
	// indexed by EntityData::hierarchy
	Array<Hierarchy> m_hierarchy;
	// indexed by EntityData::name
	Array<EntityName> m_names;
	
	Array<Partition> m_partitions;
	PartitionHandle m_partition_generator = 0;
	// all new entities are created in active partition
	PartitionHandle m_active_partition = 0;
	
	DelegateList<void(EntityRef)> m_entity_created;
	DelegateList<void(EntityRef)> m_entity_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_destroyed;
	DelegateList<void(const ComponentUID&)> m_component_added;
	
	// freelist for m_entities/m_transforms
	int m_first_free_slot;
};

// to iterate children with range-based for loop: for (EntityRef child : world->childrenOf(parent))
struct BLACK_ENGINE_API ChildrenRange {
	struct BLACK_ENGINE_API Iterator {
		void operator ++();
		bool operator !=(const Iterator& rhs);
		EntityRef operator*();

		const struct World* world;
		EntityPtr entity;
	};
	ChildrenRange(const World& world, EntityRef parent);
	Iterator begin() const;
	Iterator end() const;

	const World& world;
	EntityRef parent;
};

} // namespace black
