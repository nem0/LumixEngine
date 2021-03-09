#pragma once

#include "engine/array.h"
#include "engine/allocators.h"
#include "engine/universe.h"

namespace Lumix {

struct EntityFolders final {
	using FolderID = u16;
	static constexpr FolderID INVALID_FOLDER = 0xffFF; 

	struct Folder {
		FolderID parent_folder = INVALID_FOLDER;
		FolderID child_folder = INVALID_FOLDER;
		FolderID next_folder = INVALID_FOLDER;
		FolderID prev_folder = INVALID_FOLDER;
		EntityPtr first_entity = INVALID_ENTITY;
		char name[116];
	};

	struct Entity {
		FolderID folder = INVALID_FOLDER;
		EntityPtr next = INVALID_ENTITY;
		EntityPtr prev = INVALID_ENTITY;
	};

	EntityFolders(Universe& universe, IAllocator& allocator);
	~EntityFolders();

	const Folder& getRoot() const { return *m_root; }
	Folder& getFolder(FolderID folder_id);
	const Folder& getFolder(FolderID folder_id) const;
	void moveToFolder(EntityRef e, FolderID folder);
	EntityPtr getNextEntity(EntityRef e) const;
	FolderID emplaceFolder(FolderID folder, FolderID parent);
	void destroyFolder(FolderID folder);
	FolderID getFolderID(const Folder& folder) const { return (FolderID)m_folder_allocator.getID(&folder); }
	FolderID getFolder(EntityRef e) const;
	void selectFolder(FolderID folder) { m_selected_folder = folder; }
	FolderID getSelectedFolder() const { return m_selected_folder; }
	void serialize(OutputMemoryStream& blob);
	void deserialize(InputMemoryStream& blob);

private:
	FolderID allocFolder();
	void onEntityCreated(EntityRef e);
	void onEntityDestroyed(EntityRef e);

	Universe& m_universe;
	Array<Entity> m_entities;
	Folder* m_root;
	VirtualAllocator<Folder> m_folder_allocator;
	FolderID m_selected_folder;
};

} // namespace Lumix