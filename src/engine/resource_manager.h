#pragma once


#include "engine/hash_map.h"


namespace Lumix
{


class Path;
class Resource;
struct ResourceType;


namespace FS
{
class FileSystem;
}


class ResourceManagerBase;


class LUMIX_ENGINE_API ResourceManager
{
	typedef HashMap<uint32, ResourceManagerBase*> ResourceManagerTable;

public:
	explicit ResourceManager(IAllocator& allocator);
	~ResourceManager();

	void create(FS::FileSystem& fs);
	void destroy();

	IAllocator& getAllocator() { return m_allocator; }
	ResourceManagerBase* get(ResourceType type);
	const ResourceManagerTable& getAll() const { return m_resource_managers; }

	void add(ResourceType type, ResourceManagerBase* rm);
	void remove(ResourceType type);
	void reload(const Path& path);
	void removeUnreferenced();

	FS::FileSystem& getFileSystem() { return *m_file_system; }

private:
	IAllocator& m_allocator;
	ResourceManagerTable m_resource_managers;
	FS::FileSystem* m_file_system;
};


} // namespace Lumix
