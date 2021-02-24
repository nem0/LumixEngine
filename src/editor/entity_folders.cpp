#include "entity_folders.h"

namespace Lumix {


EntityFolders::EntityFolders(Universe& universe, IAllocator& allocator)
	: m_entities(allocator)
	, m_universe(universe) 
	, m_folder_allocator(0xffFF)
{
	ASSERT(!universe.getFirstEntity().isValid());
	universe.entityDestroyed().bind<&EntityFolders::onEntityDestroyed>(this);
	universe.entityCreated().bind<&EntityFolders::onEntityCreated>(this);

	m_root = m_folder_allocator.alloc();
	m_root->name = "root";
	m_selected_folder = 0;
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
		Folder& f = m_folder_allocator.getObject(entity.folder);
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
	Folder* f = m_folder_allocator.alloc();
	return m_folder_allocator.getID(f);
}

void EntityFolders::destroyFolder(FolderID folder) {
	Folder& f = getFolder(folder);
	ASSERT(!f.first_entity.isValid());
	ASSERT(f.parent_folder != INVALID_FOLDER);
	Folder& parent = m_folder_allocator.getObject(f.parent_folder);
	if (parent.child_folder == folder) {
		parent.child_folder = f.next_folder;
	}

	if (f.next_folder != INVALID_FOLDER) {
		Folder& n = m_folder_allocator.getObject(f.next_folder);
		n.prev_folder = f.prev_folder;
	}
	if (f.prev_folder != INVALID_FOLDER) {
		Folder& p = m_folder_allocator.getObject(f.prev_folder);
		p.next_folder = f.next_folder;
	}
	m_folder_allocator.dealloc(&f);
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
	
	Folder& f = m_folder_allocator.getObject(folder);
	f.name = "Folder";
	f.parent_folder = parent;
	Folder& p = m_folder_allocator.getObject(parent);
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
	return m_folder_allocator.getObject(folder_id);
}

const EntityFolders::Folder& EntityFolders::getFolder(FolderID folder_id) const {
	return m_folder_allocator.getObject(folder_id);
}

void EntityFolders::serialize(OutputMemoryStream& blob) {
	blob.write(m_entities.size());
	blob.write(m_entities.begin(), m_entities.byte_size());
	const u32 size = m_folder_allocator.commited * m_folder_allocator.page_size;
	blob.write(size);
	blob.write(m_folder_allocator.mem, size);
	blob.write(m_folder_allocator.commited);
	blob.write(m_folder_allocator.first_free);
}

void EntityFolders::deserialize(InputMemoryStream& blob) {
	const u32 count = blob.read<u32>();
	m_entities.resize(count);
	blob.read(m_entities.begin(), m_entities.byte_size());

	os::memRelease(m_folder_allocator.mem);
	m_folder_allocator.mem = (u8*)os::memReserve(sizeof(Folder) * m_folder_allocator.max_count);
	const u32 size = blob.read<u32>();
	os::memCommit(m_folder_allocator.mem, size);

	blob.read(m_folder_allocator.mem, size);
	blob.read(m_folder_allocator.commited);
	blob.read(m_folder_allocator.first_free);
}

} // namespace Lumix