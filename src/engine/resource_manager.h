#pragma once

#include "engine/lumix.h"

#include "foundation/hash.h"
#include "foundation/hash_map.h"


namespace Lumix
{


struct LUMIX_ENGINE_API ResourceManager {
	friend struct Resource;
	friend struct ResourceManagerHub;
	using ResourceTable = HashMap<FilePathHash, struct Resource*>;

	void create(struct ResourceType type, struct ResourceManagerHub& owner);
	void destroy();

	void enableUnload(bool enable);

	void removeUnreferenced();

	void reload(const struct Path& path);
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


struct LUMIX_ENGINE_API ResourceManagerHub {
	using ResourceManagerTable = HashMap<ResourceType, ResourceManager*>;

	struct LUMIX_ENGINE_API LoadHook {
		enum class Action { IMMEDIATE, DEFERRED };
		virtual ~LoadHook() {}
		virtual void loadRaw(const Path& requester, const Path& path) = 0;
		virtual Action onBeforeLoad(Resource& res) = 0;
		void continueLoad(Resource& res, bool success);
	};

	explicit ResourceManagerHub(IAllocator& allocator);
	~ResourceManagerHub();
	ResourceManagerHub(const ResourceManagerHub& rhs) = delete;

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
	// use `loadRaw` to load nonresource files, synchronous
	// files loaded this way are tracked as dependencies 
	bool loadRaw(const Path& included_from, const Path& path, OutputMemoryStream& data);

	void setLoadHook(LoadHook* hook);
	bool isHooked() const { return m_load_hook; }
	LoadHook::Action onBeforeLoad(Resource& resource) const;
	void add(ResourceType type, ResourceManager* rm);
	void remove(ResourceType type);
	void reload(const Path& path);
	void reloadAll();
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
