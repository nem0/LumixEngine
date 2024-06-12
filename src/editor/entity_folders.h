#pragma once

#include "core/array.h"
#include "core/hash_map.h"

#include "engine/world.h"

namespace Lumix {

// Group entities in virtual folders (unrelated to filesystem) for better organization of entities
// This is editor only concept, you should prefer folders to entity hierarchy for performance reasons
struct LUMIX_EDITOR_API EntityFolders final {
	using FolderHandle = u64;
	static constexpr FolderHandle INVALID_FOLDER = 0;

	struct Folder {
		FolderHandle id;
		FolderHandle parent = INVALID_FOLDER;
		FolderHandle first_child = INVALID_FOLDER;
		FolderHandle next = INVALID_FOLDER;
		FolderHandle prev = INVALID_FOLDER;
		EntityPtr first_entity = INVALID_ENTITY;
		World::PartitionHandle partition;
		char name[80];
	};

	struct Entity {
		FolderHandle folder = INVALID_FOLDER;
		EntityPtr next = INVALID_ENTITY;
		EntityPtr prev = INVALID_ENTITY;
	};

	EntityFolders(World& world, IAllocator& allocator);
	~EntityFolders();

	void ignoreNewEntities(bool ignore) { m_ignore_new_entities = ignore; }
	FolderHandle getRoot(World::PartitionHandle partition);
	Folder& getFolder(FolderHandle folder_handle);
	const Folder& getFolder(FolderHandle folder_handle) const;
	void moveToFolder(EntityRef e, FolderHandle folder);
	EntityPtr getNextEntity(EntityRef e) const;
	FolderHandle emplaceFolder(FolderHandle folder, FolderHandle parent);
	void destroyFolder(FolderHandle folder);
	FolderHandle getFolder(EntityRef e) const;
	void selectFolder(FolderHandle folder);
	FolderHandle getSelectedFolder() const { return m_selected_folder; }
	void serialize(OutputMemoryStream& blob);
	void deserialize(InputMemoryStream& blob, const struct EntityMap& entity_map, bool is_additive, WorldVersion version);
	void cloneTo(EntityFolders& dst, World::PartitionHandle partition, HashMap<EntityPtr, EntityPtr>& entity_map);
	void destroyPartitionFolders(World::PartitionHandle partition);
	Array<Folder>& getFolders() { return m_folders; }
	void moveFolder(FolderHandle folder, FolderHandle new_parent);
	void renameFolder(FolderHandle folder, StringView new_name);

private:
	void onEntityCreated(EntityRef e);
	void onEntityDestroyed(EntityRef e);
	void unlink(Folder& folder);
	FolderHandle generateUniqueID();

	World& m_world;
	Array<Entity> m_entities;
	Array<Folder> m_folders;
	FolderHandle m_selected_folder;
	bool m_ignore_new_entities = false;
};

} // namespace Lumix