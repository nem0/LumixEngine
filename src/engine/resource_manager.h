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
	typedef HashMap<u32, ResourceManagerBase*> ResourceManagerTable;

public:
	struct LoadHook
	{
		virtual ~LoadHook() {}
		virtual bool onBeforeLoad(Resource& res) = 0;
		void continueLoad(Resource& res);
	};

public:
	explicit ResourceManager(IAllocator& allocator);
	~ResourceManager();

	void create(FS::FileSystem& fs);
	void destroy();

	IAllocator& getAllocator() { return m_allocator; }
	ResourceManagerBase* get(ResourceType type);
	const ResourceManagerTable& getAll() const { return m_resource_managers; }

	void setLoadHook(LoadHook* hook);
	bool onBeforeLoad(Resource& resource) const;
	void add(ResourceType type, ResourceManagerBase* rm);
	void remove(ResourceType type);
	void reload(const Path& path);
	void removeUnreferenced();
	void enableUnload(bool enable);

	FS::FileSystem& getFileSystem() { return *m_file_system; }

private:
	IAllocator& m_allocator;
	ResourceManagerTable m_resource_managers;
	FS::FileSystem* m_file_system;
	LoadHook* m_load_hook;
};


} // namespace Lumix
