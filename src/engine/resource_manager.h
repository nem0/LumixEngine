#pragma once


#include "engine/hash_map.h"


namespace Lumix
{


namespace FS
{
class FileSystem;
}


class Path;
class Resource;
struct ResourceType;
class ResourceManagerHub;


class LUMIX_ENGINE_API ResourceManager
{
	friend class Resource;
	friend class ResourceManagerHub;
public:
	typedef HashMap<u32, Resource*> ResourceTable;

public:
	void create(ResourceType type, ResourceManagerHub& owner);
	void destroy();

	void enableUnload(bool enable);

	void load(Resource& resource);
	void removeUnreferenced();

	void unload(const Path& path);
	void unload(Resource& resource);

	void reload(const Path& path);
	void reload(Resource& resource);
	ResourceTable& getResourceTable() { return m_resources; }

	explicit ResourceManager(IAllocator& allocator);
	virtual ~ResourceManager();
	ResourceManagerHub& getOwner() const { return *m_owner; }

protected:
	Resource* load(const Path& path);
	virtual Resource* createResource(const Path& path) = 0;
	virtual void destroyResource(Resource& resource) = 0;
	Resource* get(const Path& path);

private:
	IAllocator& m_allocator;
	ResourceTable m_resources;
	ResourceManagerHub* m_owner;
	bool m_is_unload_enabled;
};


class LUMIX_ENGINE_API ResourceManagerHub
{
	typedef HashMap<u32, ResourceManager*> ResourceManagerTable;

public:
	struct LoadHook
	{
		virtual ~LoadHook() {}
		virtual bool onBeforeLoad(Resource& res) = 0;
		void continueLoad(Resource& res);
	};

public:
	explicit ResourceManagerHub(IAllocator& allocator);
	~ResourceManagerHub();

	void init(FS::FileSystem& fs);

	IAllocator& getAllocator() { return m_allocator; }
	ResourceManager* get(ResourceType type);
	const ResourceManagerTable& getAll() const { return m_resource_managers; }

	template <typename R> 
	R* load(const Path& path)
	{
		return static_cast<R*>(load(R::TYPE, path));
	}

	Resource* load(ResourceType type, const Path& path);

	void setLoadHook(LoadHook* hook);
	bool onBeforeLoad(Resource& resource) const;
	void add(ResourceType type, ResourceManager* rm);
	void remove(ResourceType type);
	void reload(const Path& path);
	void removeUnreferenced();
	void enableUnload(bool enable);

	FS::FileSystem& getFileSystem() { return *m_file_system; }

private:
	Resource* load(ResourceManager& manager, const Path& path);
	IAllocator& m_allocator;
	ResourceManagerTable m_resource_managers;
	FS::FileSystem* m_file_system;
	LoadHook* m_load_hook;
};


}
