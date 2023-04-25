#pragma once

#include "engine/array.h"
#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/world.h"

namespace Lumix {

struct LUMIX_EDITOR_API EntityFolders final {
	using FolderID = u64;
	static constexpr FolderID INVALID_FOLDER = 0;

	struct Folder {
		FolderID id;
		FolderID parent = INVALID_FOLDER;
		FolderID first_child = INVALID_FOLDER;
		FolderID next = INVALID_FOLDER;
		FolderID prev = INVALID_FOLDER;
		EntityPtr first_entity = INVALID_ENTITY;
		World::PartitionHandle partition;
		char name[80];
	};

	struct Entity {
		FolderID folder = INVALID_FOLDER;
		EntityPtr next = INVALID_ENTITY;
		EntityPtr prev = INVALID_ENTITY;
	};

	EntityFolders(World& world, IAllocator& allocator);
	~EntityFolders();

	void ignoreNewEntities(bool ignore) { m_ignore_new_entities = ignore; }
	FolderID getRoot(World::PartitionHandle partition) const;
	Folder& getFolder(FolderID folder_id);
	const Folder& getFolder(FolderID folder_id) const;
	void moveToFolder(EntityRef e, FolderID folder);
	EntityPtr getNextEntity(EntityRef e) const;
	FolderID emplaceFolder(FolderID folder, FolderID parent);
	void destroyFolder(FolderID folder);
	FolderID getFolder(EntityRef e) const;
	void selectFolder(FolderID folder);
	FolderID getSelectedFolder() const { return m_selected_folder; }
	void serialize(OutputMemoryStream& blob);
	void deserialize(InputMemoryStream& blob, const struct EntityMap& entity_map, bool is_additive, WorldEditorHeaderVersion version);
	void cloneTo(EntityFolders& dst, World::PartitionHandle partition, HashMap<EntityPtr, EntityPtr>& entity_map);
	void destroyPartitionFolders(World::PartitionHandle partition);
	Array<Folder>& getFolders() { return m_folders; }


private:
	void onEntityCreated(EntityRef e);
	void onEntityDestroyed(EntityRef e);
	FolderID generateUniqueID();

	IAllocator& m_allocator;
	World& m_world;
	Array<Entity> m_entities;
	Array<Folder> m_folders;
	FolderID m_selected_folder;
	bool m_ignore_new_entities = false;
};

} // namespace Lumix