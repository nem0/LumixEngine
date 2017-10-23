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
class ResourceManager;


class LUMIX_ENGINE_API ResourceManagerBase
{
	friend class Resource;
public:
	typedef HashMap<u32, Resource*> ResourceTable;

	struct LUMIX_ENGINE_API LoadHook
	{
		LoadHook(ResourceManagerBase& manager) : m_manager(manager) {}

		virtual bool onBeforeLoad(Resource& resource) = 0;
		void continueLoad(Resource& resource);

		ResourceManagerBase& m_manager;
	};

public:
	void create(ResourceType type, ResourceManager& owner);
	void destroy();

	void setLoadHook(LoadHook& load_hook);
	void enableUnload(bool enable);

	Resource* load(const Path& path);
	void load(Resource& resource);
	void removeUnreferenced();

	void unload(const Path& path);
	void unload(Resource& resource);

	void reload(const Path& path);
	void reload(Resource& resource);
	ResourceTable& getResourceTable() { return m_resources; }

	ResourceManagerBase(IAllocator& allocator);
	virtual ~ResourceManagerBase();
	ResourceManager& getOwner() const { return *m_owner; }

protected:
	virtual Resource* createResource(const Path& path) = 0;
	virtual void destroyResource(Resource& resource) = 0;
	Resource* get(const Path& path);

private:
	IAllocator& m_allocator;
	LoadHook* m_load_hook;
	ResourceTable m_resources;
	ResourceManager* m_owner;
	bool m_is_unload_enabled;
};


}
