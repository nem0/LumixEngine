#include "entity_folders.h"
#include "engine/string.h"
#include "engine/stream.h"

namespace Lumix {


EntityFolders::EntityFolders(World& world, IAllocator& allocator)
	: m_entities(allocator)
	, m_world(world) 
	, m_folders(allocator)
	, m_allocator(allocator)
{
	ASSERT(!world.getFirstEntity().isValid());
	world.entityDestroyed().bind<&EntityFolders::onEntityDestroyed>(this);
	world.entityCreated().bind<&EntityFolders::onEntityCreated>(this);

	const FolderID root_id = m_folders.alloc();
	Folder& root = m_folders.getObject(root_id);
	copyString(root.name, "root");
	m_selected_folder = root_id;
}

EntityFolders::~EntityFolders() {
	m_world.entityCreated().unbind<&EntityFolders::onEntityCreated>(this);
	m_world.entityDestroyed().unbind<&EntityFolders::onEntityDestroyed>(this);
}

EntityFolders::FolderID EntityFolders::getRoot(World::PartitionHandle partition) const {
	for (i32 i = m_folders.data.size() - 1; i >= 0; --i) {
		const Folder& f = m_folders.data[i];
		if (f.parent_folder != INVALID_FOLDER) continue;
		if (f.prev_folder != INVALID_FOLDER) continue;
		if (!f.valid) continue;
		if (f.partition == partition) return i;
	}
	return INVALID_FOLDER;
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
	f.valid = false;
	m_folders.free(folder);
	if (m_selected_folder == folder) m_selected_folder = 0;
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
	blob.write(m_folders.data.begin(), m_folders.data.byte_size());
	blob.write(m_folders.first_free);
}

void EntityFolders::fix(Folder& f, const EntityMap& entity_map) {
	f.first_entity = entity_map.get(f.first_entity);
	if (f.child_folder != INVALID_FOLDER) fix(m_folders.getObject(f.child_folder), entity_map);
	if (f.next_folder != INVALID_FOLDER) fix(m_folders.getObject(f.next_folder), entity_map);
}

void EntityFolders::destroyPartitionFolders(World::PartitionHandle partition) {
	for (i32 i = 0; i < m_folders.data.size(); ++i) {
		Folder& f = m_folders.data[i];
		if (f.valid && f.partition == partition) {
			f.valid = false;
			memcpy(&f, &m_folders.first_free, sizeof(m_folders.first_free));
			m_folders.first_free = i;
		}
	}
}

void EntityFolders::cloneTo(EntityFolders& dst, World::PartitionHandle partition, HashMap<EntityPtr, EntityPtr>& entity_map) {
	dst.m_entities.clear();
	auto get_mapped = [&](EntityPtr e){
		if (!e.isValid()) return e;
		auto iter = entity_map.find(e);
		if (iter.isValid()) return iter.value();
		return INVALID_ENTITY;
	};
	HashMap<FolderID, FolderID> folder_map(m_allocator);
	folder_map.insert(INVALID_FOLDER, INVALID_FOLDER);
	dst.m_folders.data[0].valid = false;
	*(i32*)&dst.m_folders.data[0] = -1;
	dst.m_folders.first_free = 0;
	
	for (Folder& f : m_folders.data) {
		if (f.partition == partition && f.valid) {
			FolderID src_folder = FolderID(&f - m_folders.data.begin());
			FolderID dst_folder = dst.allocFolder();
			Folder& dst_folder_obj = dst.m_folders.data[dst_folder];
			dst_folder_obj.first_entity = get_mapped(f.first_entity);
			copyString(dst_folder_obj.name, m_folders.data[dst_folder].name);
			folder_map.insert(src_folder, dst_folder);
		}
	}

	for (auto iter = folder_map.begin(), end = folder_map.end(); iter != end; ++iter) {
		if (iter.key() == INVALID_FOLDER) continue;
		Folder& src_folder = m_folders.data[iter.key()];
		Folder& dst_folder = dst.m_folders.data[iter.value()];
		dst_folder.parent_folder = folder_map[src_folder.parent_folder];
		dst_folder.child_folder = folder_map[src_folder.child_folder];
		dst_folder.next_folder = folder_map[src_folder.next_folder];
		dst_folder.prev_folder = folder_map[src_folder.prev_folder];
	}

	for (auto iter = entity_map.begin(), end = entity_map.end(); iter != end; ++iter) {
		EntityPtr src_e = iter.key();
		EntityPtr dst_e = iter.value();
		if (dst.m_entities.size() <= dst_e.index) dst.m_entities.resize(dst_e.index + 1);
		dst.m_entities[dst_e.index].next = get_mapped(m_entities[src_e.index].next);
		dst.m_entities[dst_e.index].prev = get_mapped(m_entities[src_e.index].prev);
		dst.m_entities[dst_e.index].folder = folder_map[m_entities[src_e.index].folder];
	}
}

void EntityFolders::deserialize(InputMemoryStream& blob, const EntityMap& entity_map, bool additive, bool new_format) {
	if (!additive) m_folders.data.clear();
	const u32 folder_offset = m_folders.data.size();
	const u32 count = blob.read<u32>();
	m_entities.reserve(count + m_entities.size());

	auto offsetFolder = [folder_offset](FolderID& id) {
		if (id != INVALID_FOLDER) id += folder_offset;
	};

	for (u32 i = 0; i < count; ++i) {
		EntityPtr e = entity_map.get(EntityPtr{(i32)i});
		if (e.isValid()) {
			while (e.index >= m_entities.size()) m_entities.emplace();
			Entity& entity = m_entities[e.index];
			if (additive) {
				// entity has already been placed in wrong folder by onEntityCreated, we need to "fix" that
				Folder& f = m_folders.data[entity.folder];
				if (f.first_entity == e) f.first_entity = entity.next;
				if (entity.prev.isValid()) m_entities[entity.prev.index].next = entity.next;
				if (entity.next.isValid()) m_entities[entity.next.index].prev = entity.prev;
			}
			blob.read(entity);
			entity.next = entity_map.get(entity.next);
			entity.prev = entity_map.get(entity.prev);
			offsetFolder(entity.folder);
		}
		else {
			Entity tmp;
			blob.read(tmp);
		}
	}

	const u32 size = blob.read<u32>();
	const u32 folder_count = size / sizeof(Folder);
	m_folders.data.resize(folder_count + folder_offset);
	for (u32 i = 0; i < folder_count; ++i) {
		Folder& f = m_folders.data[folder_offset + i];
		if (new_format) {
			blob.read(f);
		}
		else {
			blob.read(f.parent_folder);
			blob.read(f.child_folder);
			blob.read(f.next_folder);
			blob.read(f.prev_folder);
			blob.read(f.first_entity);
			blob.read(f.name);
			f.name[lengthOf(f.name) - 1] = 0;
			u32 dummy;
			blob.read(dummy);
		}
		offsetFolder(f.parent_folder);
		offsetFolder(f.child_folder);
		offsetFolder(f.prev_folder);
		offsetFolder(f.next_folder);
		f.partition = m_world.getActivePartition();
	}

	if (!new_format) {
		u32 dummy;
		blob.read(dummy);
	}
	i32 new_first_free;
	blob.read(new_first_free);
	if (new_first_free >= 0) {
		if (m_folders.first_free < 0) {
			m_folders.first_free = new_first_free + folder_offset;
		}
		else {
			i32* tmp = &m_folders.first_free;
			while (*tmp >= 0) {
				tmp = (i32*)&m_folders.data[*tmp];
			}
			*tmp = folder_offset + new_first_free;
		}
	}
	
	fix(m_folders.data[folder_offset], entity_map);
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