#pragma once

#include "engine/array.h"
#include "engine/hash_map.h"
#include "engine/world.h"

namespace Lumix {

struct LUMIX_EDITOR_API EntityFolders final {
	using FolderID = u16;
	static constexpr FolderID INVALID_FOLDER = 0xffFF; 

	struct Folder {
		FolderID parent_folder = INVALID_FOLDER;
		FolderID child_folder = INVALID_FOLDER;
		FolderID next_folder = INVALID_FOLDER;
		FolderID prev_folder = INVALID_FOLDER;
		EntityPtr first_entity = INVALID_ENTITY;
		World::PartitionHandle partition;
		bool valid = true;
		char name[112];
	};

	static_assert(sizeof(Folder) == 128);

	struct Entity {
		FolderID folder = INVALID_FOLDER;
		EntityPtr next = INVALID_ENTITY;
		EntityPtr prev = INVALID_ENTITY;
	};

	EntityFolders(World& world, IAllocator& allocator);
	~EntityFolders();

	FolderID getRoot(World::PartitionHandle partition) const;
	Folder& getFolder(FolderID folder_id);
	const Folder& getFolder(FolderID folder_id) const;
	void moveToFolder(EntityRef e, FolderID folder);
	EntityPtr getNextEntity(EntityRef e) const;
	FolderID emplaceFolder(FolderID folder, FolderID parent);
	void destroyFolder(FolderID folder);
	FolderID getFolder(EntityRef e) const;
	void selectFolder(FolderID folder) { m_selected_folder = folder; }
	FolderID getSelectedFolder() const { return m_selected_folder; }
	void serialize(OutputMemoryStream& blob);
	void deserialize(InputMemoryStream& blob, const struct EntityMap& entity_map, bool additive, bool new_format);
	void cloneTo(EntityFolders& dst, World::PartitionHandle partition, HashMap<EntityPtr, EntityPtr>& entity_map);
	void destroyPartitionFolders(World::PartitionHandle partition);

private:
	struct FreeList {
		FreeList(IAllocator& allocator);
		
		FolderID alloc();
		void free(FolderID folder);
		Folder& getObject(FolderID id);
		const Folder& getObject(FolderID id) const;

		Array<Folder> data;
		i32 first_free;
	};

	FolderID allocFolder();
	void onEntityCreated(EntityRef e);
	void onEntityDestroyed(EntityRef e);
	void fix(Folder& folder, const EntityMap& entity_map);

	IAllocator& m_allocator;
	World& m_world;
	Array<Entity> m_entities;
	FreeList m_folders;
	FolderID m_selected_folder;
};

} // namespace Lumix