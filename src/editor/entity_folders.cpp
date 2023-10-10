#include "entity_folders.h"
#include "engine/string.h"
#include "engine/stream.h"

namespace Lumix {

EntityFolders::EntityFolders(World& world, IAllocator& allocator)
	: m_entities(allocator)
	, m_world(world) 
	, m_folders(allocator)
{
	ASSERT(!world.getFirstEntity().isValid());
	world.entityDestroyed().bind<&EntityFolders::onEntityDestroyed>(this);
	world.entityCreated().bind<&EntityFolders::onEntityCreated>(this);

	Folder& f = m_folders.emplace();
	f.id = randGUID();
	copyString(f.name, "root");
	m_selected_folder = f.id;
}

EntityFolders::~EntityFolders() {
	m_world.entityCreated().unbind<&EntityFolders::onEntityCreated>(this);
	m_world.entityDestroyed().unbind<&EntityFolders::onEntityDestroyed>(this);
}

EntityFolders::FolderHandle EntityFolders::getRoot(World::PartitionHandle partition) {
	for (const Folder& f : m_folders) {
		if (f.parent != INVALID_FOLDER) continue;
		ASSERT(f.next == INVALID_FOLDER);
		ASSERT(f.prev == INVALID_FOLDER);
		if (f.partition == partition) return f.id;
	}
	// partition created at runtime, create folder for it
	Folder& f = m_folders.emplace();
	f.id = randGUID();
	f.partition = partition;
	copyString(f.name, "runtime folder");
	return f.id;
}

EntityFolders::FolderHandle EntityFolders::generateUniqueID() {
	for (;;) {
		const FolderHandle id = randGUID();
		bool found = false;
		for (const Folder& f : m_folders) {
			if (f.id == id) {
				found = true;
				break;
			}
		}
		if (!found) return id;
	}
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
	if (!m_ignore_new_entities) moveToFolder(e, m_selected_folder);
}

EntityPtr EntityFolders::getNextEntity(EntityRef e) const {
	return m_entities[e.index].next;
}

void EntityFolders::moveToFolder(EntityRef e, FolderHandle folder_id) {
	ASSERT(folder_id != INVALID_FOLDER);
	while (m_entities.size() <= e.index) m_entities.emplace();

	Entity& entity = m_entities[e.index];
	Folder& dst_folder = getFolder(folder_id);
	if (entity.folder != INVALID_FOLDER) {
		Folder& src_folder = getFolder(entity.folder);
		if (src_folder.first_entity == e) src_folder.first_entity = entity.next;
		if (entity.prev.isValid()) m_entities[entity.prev.index].next = entity.next;
		if (entity.next.isValid()) m_entities[entity.next.index].prev = entity.prev;
		m_world.setPartition(e, dst_folder.partition);
	}

	entity.folder = folder_id;
	entity.next = dst_folder.first_entity;
	entity.prev = INVALID_ENTITY;
	dst_folder.first_entity = e;
	if (entity.next.isValid()) {
		m_entities[entity.next.index].prev = e;
	}
}

void EntityFolders::destroyFolder(FolderHandle folder_id) {
	Folder& f = getFolder(folder_id);
	ASSERT(!f.first_entity.isValid());
	ASSERT(f.first_child == INVALID_FOLDER);
	ASSERT(f.parent != INVALID_FOLDER);
	unlink(f);

	m_folders.eraseItems([&](const Folder& f){ return f.id == folder_id; });

	if (m_selected_folder == folder_id) {
		m_selected_folder = m_folders.empty() ? INVALID_FOLDER : m_folders[0].id;
	}
}

EntityFolders::FolderHandle EntityFolders::emplaceFolder(FolderHandle folder, FolderHandle parent) {
	ASSERT(parent != INVALID_FOLDER); // there's exactly 1 root folder
	if (folder == INVALID_FOLDER) {
		folder = generateUniqueID();
	}
	
	Folder& new_folder = m_folders.emplace();
	new_folder.id = folder;
	copyString(new_folder.name, "Folder");
	new_folder.parent = parent;
	Folder& p = getFolder(parent);
	if (p.first_child != INVALID_FOLDER) {
		new_folder.next = p.first_child;
		getFolder(p.first_child).prev = folder;
	}
	p.first_child = folder;
	return folder;
}

void EntityFolders::selectFolder(FolderHandle folder)
{
	m_selected_folder = folder;
	World::PartitionHandle partition = getFolder(folder).partition;
	m_world.setActivePartition(partition);
}

EntityFolders::FolderHandle EntityFolders::getFolder(EntityRef e) const {
	return m_entities[e.index].folder;
}

EntityFolders::Folder& EntityFolders::getFolder(FolderHandle folder_id) {
	for (Folder& folder : m_folders) {
		if (folder.id == folder_id) return folder;
	}
	ASSERT(false);
	return m_folders[0];
}

const EntityFolders::Folder& EntityFolders::getFolder(FolderHandle folder_id) const {
	return const_cast<EntityFolders*>(this)->getFolder(folder_id);
}

void EntityFolders::serialize(OutputMemoryStream& blob) {
	blob.write(m_entities.size());
	blob.write(m_entities.begin(), m_entities.byte_size());
	const u32 size = m_folders.size();
	blob.write(size);
	blob.write(m_folders.begin(), m_folders.byte_size());
}

void EntityFolders::destroyPartitionFolders(World::PartitionHandle partition) {
	m_folders.eraseItems([&](const Folder& f){ return f.partition == partition; });
}

void EntityFolders::cloneTo(EntityFolders& dst, World::PartitionHandle partition, HashMap<EntityPtr, EntityPtr>& entity_map) {
	dst.m_entities.clear();
	auto get_mapped = [&](EntityPtr e){
		if (!e.isValid()) return e;
		auto iter = entity_map.find(e);
		if (iter.isValid()) return iter.value();
		return INVALID_ENTITY;
	};
	
	for (const Folder& f : m_folders) {
		if (f.partition == partition) {
			dst.m_folders.push(f);
		}
	}

	for (auto iter = entity_map.begin(), end = entity_map.end(); iter != end; ++iter) {
		EntityPtr src_e = iter.key();
		EntityPtr dst_e = iter.value();
		if (dst.m_entities.size() <= dst_e.index) dst.m_entities.resize(dst_e.index + 1);
		dst.m_entities[dst_e.index].next = get_mapped(m_entities[src_e.index].next);
		dst.m_entities[dst_e.index].prev = get_mapped(m_entities[src_e.index].prev);
	}
}

void EntityFolders::unlink(Folder& folder) {
	Folder& old_parent = getFolder(folder.parent);
	if (old_parent.first_child == folder.id) old_parent.first_child = folder.next;
	if (folder.prev != INVALID_FOLDER) getFolder(folder.prev).next = folder.next;
	if (folder.next != INVALID_FOLDER) getFolder(folder.next).prev = folder.prev;
	folder.next = folder.prev = INVALID_FOLDER;
}

void EntityFolders::renameFolder(FolderHandle folder, StringView new_name) {
	Folder& f = getFolder(folder);
	copyString(f.name, new_name);
	// reinsert so it's sorted
	moveFolder(folder, f.parent);
}


void EntityFolders::moveFolder(FolderHandle folder_id, FolderHandle new_parent_id) {
	Folder& folder = getFolder(folder_id);
	Folder& new_parent = getFolder(new_parent_id);
	#ifdef LUMIX_DEBUG
		Folder& old_parent = getFolder(folder.parent);
		ASSERT(new_parent.partition == old_parent.partition);
	#endif

	unlink(folder);	
	
	folder.parent = new_parent.id;
	if (new_parent.first_child == INVALID_FOLDER) {
		new_parent.first_child = folder.id;
	}
	else {
		FolderHandle i = new_parent.first_child;
		for (;;) {
			Folder& n = getFolder(i);
			if (strcmp(folder.name, n.name) < 0) {
				if (new_parent.first_child == n.id) new_parent.first_child = folder.id;
				folder.prev = n.prev;
				if (folder.prev != INVALID_FOLDER) getFolder(folder.prev).next = folder.id;
				folder.next = n.id;
				n.prev = folder.id;
				break;
			}
			if (n.next == INVALID_FOLDER) {
				n.next = folder.id;
				folder.prev = n.id;
				break;
			}
			i = n.next;
		}
	}
}

void EntityFolders::deserialize(InputMemoryStream& blob, const EntityMap& entity_map, bool additive, WorldVersion version) {
	if (version <= WorldVersion::NEW_ENTITY_FOLDERS) {
		// ignore old format folders
		i32 count;
		blob.read(count);
		blob.skip(count * 12);

		i32 size;
		blob.read(size);
		blob.skip(size);
		blob.skip(sizeof(i32) * 2);

		Folder* folder;
		if (additive) {
			folder = &m_folders.emplace();
			folder->id = generateUniqueID();
			folder->partition = m_world.getActivePartition();
			copyString(folder->name, "root");
		}
		else {
			ASSERT(m_folders.size() == 1);
			folder = &m_folders[0];
		}

		for (EntityPtr e : entity_map.m_map) {
			if (!e.isValid()) continue;

			while (m_entities.size() <= e.index) m_entities.emplace();
			Entity& entity = m_entities[e.index];
			entity.folder = folder->id;
			if (folder->first_entity.isValid()) m_entities[folder->first_entity.index].prev = e;
			entity.next = folder->first_entity;
			folder->first_entity = e;
		}
		return;
	}

	if (!additive) m_folders.clear();
	const u32 folder_offset = m_folders.size();
	const u32 count = blob.read<u32>();
	m_entities.reserve(count + m_entities.size());

	for (u32 i = 0; i < count; ++i) {
		EntityPtr e = entity_map.get(EntityPtr{(i32)i});
		if (e.isValid()) {
			while (e.index >= m_entities.size()) m_entities.emplace();
			Entity& entity = m_entities[e.index];
			blob.read(entity);
			entity.next = entity_map.get(entity.next);
			entity.prev = entity_map.get(entity.prev);
		}
		else {
			Entity tmp;
			blob.read(tmp);
		}
	}

	const u32 folder_count = blob.read<u32>();
	m_folders.resize(folder_count + folder_offset);
	blob.read(&m_folders[folder_offset], sizeof(Folder) * folder_count);
	for (u32 i = 0; i < folder_count; ++i) {
		Folder& f = m_folders[folder_offset + i];
		f.first_entity = entity_map.get(f.first_entity);
	}
}

} // namespace Lumix