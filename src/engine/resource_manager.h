#pragma once


#include "engine/hash_map.h"


namespace Lumix
{


class Path;
class Resource;


namespace FS
{
class FileSystem;
}


class ResourceManagerBase;


class LUMIX_ENGINE_API ResourceManager final
{
	typedef HashMap<uint32, ResourceManagerBase*> ResourceManagerTable;

public:
	explicit ResourceManager(IAllocator& allocator);
	~ResourceManager();

	void create(FS::FileSystem& fs);
	void destroy();

	IAllocator& getAllocator() { return m_allocator; }
	ResourceManagerBase* get(uint32 id);
	const ResourceManagerTable& getAll() const { return m_resource_managers; }

	void add(uint32 id, ResourceManagerBase* rm);
	void remove(uint32 id);
	void reload(const Path& path);
	void removeUnreferenced();

	FS::FileSystem& getFileSystem() { return *m_file_system; }

private:
	IAllocator& m_allocator;
	ResourceManagerTable m_resource_managers;
	FS::FileSystem* m_file_system;
};


} // namespace Lumix
