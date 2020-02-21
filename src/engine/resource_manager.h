#pragma once


#include "engine/hash_map.h"


namespace Lumix
{


struct LUMIX_ENGINE_API ResourceManager
{
	friend struct Resource;
	friend struct ResourceManagerHub;
public:
	using ResourceTable = HashMap<u32, struct Resource*, HashFuncDirect<u32>>;

public:
	void create(struct ResourceType type, struct ResourceManagerHub& owner);
	void destroy();

	void enableUnload(bool enable);

	void load(Resource& resource);
	void removeUnreferenced();

	void unload(const struct Path& path);
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

protected:
	IAllocator& m_allocator;
	ResourceTable m_resources;
	ResourceManagerHub* m_owner;
	bool m_is_unload_enabled;
};


struct LUMIX_ENGINE_API ResourceManagerHub
{
	using ResourceManagerTable = HashMap<u32, ResourceManager*>;

public:
	struct LUMIX_ENGINE_API LoadHook
	{
		enum class Action { IMMEDIATE, DEFERRED };
		virtual ~LoadHook() {}
		virtual Action onBeforeLoad(Resource& res) = 0;
		void continueLoad(Resource& res);
	};

public:
	explicit ResourceManagerHub(IAllocator& allocator);
	~ResourceManagerHub();

	void init(struct FileSystem& fs);

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
	LoadHook::Action onBeforeLoad(Resource& resource) const;
	void add(ResourceType type, ResourceManager* rm);
	void remove(ResourceType type);
	void reload(const Path& path);
	void removeUnreferenced();
	void enableUnload(bool enable);

	FileSystem& getFileSystem() { return *m_file_system; }

private:
	Resource* load(ResourceManager& manager, const Path& path);
	IAllocator& m_allocator;
	ResourceManagerTable m_resource_managers;
	FileSystem* m_file_system;
	LoadHook* m_load_hook;
};


}
