#include "entity_folders.h"
#include "engine/string.h"
#include "engine/stream.h"

namespace Lumix {


EntityFolders::EntityFolders(Universe& universe, IAllocator& allocator)
	: m_entities(allocator)
	, m_universe(universe) 
	, m_folders(allocator)
{
	ASSERT(!universe.getFirstEntity().isValid());
	universe.entityDestroyed().bind<&EntityFolders::onEntityDestroyed>(this);
	universe.entityCreated().bind<&EntityFolders::onEntityCreated>(this);

	const FolderID root_id = m_folders.alloc();
	Folder& root = m_folders.getObject(root_id);
	copyString(root.name, "root");
	m_selected_folder = root_id;
}

EntityFolders::~EntityFolders() {
	m_universe.entityCreated().unbind<&EntityFolders::onEntityCreated>(this);
	m_universe.entityDestroyed().unbind<&EntityFolders::onEntityDestroyed>(this);
}

void EntityFolders::onEntityDestroyed(EntityRef e) {
	Folder& parent = getFolder(m_entities[e.index].folder);
	Entity& entity = m_entities[e.index];
	if (parent.first_entity == e) {
		parent.first_entity = entity.next;
	}

	if (entity.prev.isValid()) {
		m_entities[entity.prev.index].next = entity.next;
	}

	if (entity.next.isValid()) {
		m_entities[entity.next.index].prev = entity.prev;
	}

	entity.folder = INVALID_FOLDER;
	entity.next = INVALID_ENTITY;
	entity.prev = INVALID_ENTITY;
}

void EntityFolders::onEntityCreated(EntityRef e) {
	moveToFolder(e, m_selected_folder);
}

EntityPtr EntityFolders::getNextEntity(EntityRef e) const {
	return m_entities[e.index].next;
}

void EntityFolders::moveToFolder(EntityRef e, FolderID folder_id) {
	ASSERT(folder_id != INVALID_FOLDER);
	while (m_entities.size() <= e.index) {
		m_entities.emplace();
	}
	Entity& entity = m_entities[e.index];
	if (entity.folder != INVALID_FOLDER) {
		Folder& f = m_folders.getObject(entity.folder);
		if (f.first_entity == e) {
			f.first_entity = entity.next;
		}

		if (entity.prev.isValid()) {
			m_entities[entity.prev.index].next = entity.next;
		}

		if (entity.next.isValid()) {
			m_entities[entity.next.index].prev = entity.prev;
		}
	}
	entity.folder = folder_id;
	
	Folder& f = getFolder(folder_id);
	entity.next = f.first_entity;
	entity.prev = INVALID_ENTITY;
	f.first_entity = e;
	if (entity.next.isValid()) {
		m_entities[entity.next.index].prev = e;
	}
}

EntityFolders::FolderID EntityFolders::allocFolder() {
	return m_folders.alloc();
}

void EntityFolders::destroyFolder(FolderID folder) {
	Folder& f = getFolder(folder);
	ASSERT(!f.first_entity.isValid());
	ASSERT(f.child_folder == INVALID_FOLDER);
	ASSERT(f.parent_folder != INVALID_FOLDER);
	Folder& parent = m_folders.getObject(f.parent_folder);
	if (parent.child_folder == folder) {
		parent.child_folder = f.next_folder;
	}

	if (f.next_folder != INVALID_FOLDER) {
		Folder& n = m_folders.getObject(f.next_folder);
		n.prev_folder = f.prev_folder;
	}
	if (f.prev_folder != INVALID_FOLDER) {
		Folder& p = m_folders.getObject(f.prev_folder);
		p.next_folder = f.next_folder;
	}
	m_folders.free(folder);
}

EntityFolders::FolderID EntityFolders::emplaceFolder(FolderID folder, FolderID parent) {
	ASSERT(parent != INVALID_FOLDER); // there's exactly 1 root folder
	if (folder == INVALID_FOLDER) {
		folder = allocFolder();
		if (folder == INVALID_FOLDER) {
			ASSERT(false); // no more free space for folders
			return INVALID_FOLDER;
		}
	}
	
	Folder& f = m_folders.getObject(folder);
	copyString(f.name, "Folder");
	f.parent_folder = parent;
	Folder& p = m_folders.getObject(parent);
	if (p.child_folder != INVALID_FOLDER) {
		f.next_folder = p.child_folder;
		getFolder(p.child_folder).prev_folder = folder;
	}
	p.child_folder = folder;

	return folder;
}

EntityFolders::FolderID EntityFolders::getFolder(EntityRef e) const {
	return m_entities[e.index].folder;
}

EntityFolders::Folder& EntityFolders::getFolder(FolderID folder_id) {
	return m_folders.getObject(folder_id);
}

const EntityFolders::Folder& EntityFolders::getFolder(FolderID folder_id) const {
	return m_folders.getObject(folder_id);
}

void EntityFolders::serialize(OutputMemoryStream& blob) {
	blob.write(m_entities.size());
	blob.write(m_entities.begin(), m_entities.byte_size());
	const u32 size = m_folders.data.byte_size();
	blob.write(size);
	blob.write(m_folders.data.begin(), size);
	u32 dummy = 0;
	blob.write(dummy);
	blob.write(m_folders.first_free);
}

void EntityFolders::fix(Folder& f, const EntityMap& entity_map) {
	f.first_entity = entity_map.get(f.first_entity);
	if (f.child_folder != INVALID_FOLDER) fix(m_folders.getObject(f.child_folder), entity_map);
	if (f.next_folder != INVALID_FOLDER) fix(m_folders.getObject(f.next_folder), entity_map);
}

void EntityFolders::deserialize(InputMemoryStream& blob, const EntityMap& entity_map) {
	const u32 count = blob.read<u32>();
	m_entities.reserve(count);

	for (u32 i = 0; i < count; ++i) {
		EntityPtr e = entity_map.get(EntityPtr{(i32)i});
		if (e.isValid()) {
			while (e.index >= m_entities.size()) m_entities.emplace();
			blob.read(m_entities[e.index]);
			m_entities[e.index].next = entity_map.get(m_entities[e.index].next);
			m_entities[e.index].prev = entity_map.get(m_entities[e.index].prev);
		}
		else {
			Entity tmp;
			blob.read(tmp);
		}
	}

	const u32 size = blob.read<u32>();
	m_folders.data.resize(size / sizeof(Folder));
	blob.read(m_folders.data.begin(), size);
	u32 dummy;
	blob.read(dummy);
	blob.read(m_folders.first_free);
	
	fix(m_folders.getObject(0), entity_map);
}

EntityFolders::FreeList::FreeList(IAllocator& allocator) 
	: data(allocator)
	, first_free(-1)
{}

EntityFolders::FolderID EntityFolders::FreeList::alloc() {
	if (first_free < 0) {
		ASSERT(data.size() < 0xffFF);
		data.emplace();
		return FolderID(data.size() - 1);
	}

	const FolderID id = (FolderID)first_free;
	memcpy(&first_free, &data[first_free], sizeof(first_free));

	new (NewPlaceholder(), &data[id]) Folder;

	return id;
}

void EntityFolders::FreeList::free(FolderID folder) {
	
	memcpy(&data[folder], &first_free, sizeof(first_free));
	first_free = folder;
}

EntityFolders::Folder& EntityFolders::FreeList::getObject(FolderID id) {
	return data[id];
}

const EntityFolders::Folder& EntityFolders::FreeList::getObject(FolderID id) const {
	return data[id];
}


} // namespace Lumix